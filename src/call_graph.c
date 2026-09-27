#include "call_graph.h"
#include "arena.h"
#include "stack.h"
#include "math.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

// Create a new call graph node for a given TAC function.
static struct CallGraphNode* call_graph_node_create(const struct TACFunc* func, unsigned node_index) {
  struct CallGraphNode* node = arena_alloc(sizeof(struct CallGraphNode));
  node->body = func->body;
  node->func_name = func->name;
  node->contains_indirect_calls = false;
  node->can_recurse = false;
  node->num_instrs = 0;
  for (struct TACInstr* instr = func->body.head; 
      instr != NULL; 
      instr = instr->next) {
    node->num_instrs++;
  }
  node->node_index = node_index;
  node->callers.head = NULL;
  node->callers.tail = NULL;
  node->callees.head = NULL;
  node->callees.tail = NULL;
  return node;
}

// Create a new call graph entry for a given node.
static struct CallGraphEntry* call_graph_entry_create(struct CallGraphNode* node) {
  struct CallGraphEntry* entry = arena_alloc(sizeof(struct CallGraphEntry));
  entry->node = node;
  entry->next = NULL;
  return entry;
}

// Add a node to a call graph node list.
static void call_graph_node_list_add(struct CallGraphNodeList* list,
                                     struct CallGraphNode* node) {
  struct CallGraphEntry* entry = call_graph_entry_create(node);
  if (list->tail) {
    list->tail->next = entry;
  } else {
    list->head = entry;
  }
  list->tail = entry;
}

// Return true when list already contains node.
static bool call_graph_node_list_contains(const struct CallGraphNodeList* list,
                                          const struct CallGraphNode* node) {
  for (const struct CallGraphEntry* entry = list->head;
       entry != NULL;
       entry = entry->next) {
    if (entry->node == node) {
      return true;
    }
  }
  return false;
}

// Link caller and callee in both directions. A call graph records relationships,
// so multiple call instructions between the same functions share one edge.
static void link_call_graph_nodes(struct CallGraphNode* caller,
                                  struct CallGraphNode* callee) {
  if (call_graph_node_list_contains(&caller->callees, callee)) {
    return;
  }
  call_graph_node_list_add(&caller->callees, callee);
  call_graph_node_list_add(&callee->callers, caller);
}

// Find a call graph node by its function name in the given call graph.
// Returns NULL if not found.
static struct CallGraphNode* find_call_graph_node(const struct CallGraph* call_graph,
                                                  const struct Slice* func_name) {
  for (const struct CallGraphEntry* entry = call_graph->nodes.head;
       entry != NULL;
       entry = entry->next) {
    if (compare_slice_to_slice(entry->node->func_name, func_name)) {
      return entry->node;
    }
  }
  return NULL;
}

// Link all call graph nodes based on the function calls in their bodies.
static void link_call_graph(struct CallGraph* call_graph) {
  // iterate over all functions in the call graph
  for (struct CallGraphEntry* entry = call_graph->nodes.head;
       entry != NULL;
       entry = entry->next) {
    struct CallGraphNode* caller = entry->node;
    // iterate over all instructions in the function body
    for (struct TACInstr* instr = caller->body.head; instr != NULL; instr = instr->next) {
      // if the instruction is a function call,
      // find the callee node and link it to the caller
      switch (instr->type) {
        case TACCALL: {
          struct CallGraphNode* callee =
              find_call_graph_node(call_graph, instr->instr.tac_call.func_name);
          if (callee) {
            link_call_graph_nodes(caller, callee);
          }
          break;
        }
        case TACTAIL_CALL: {
          struct CallGraphNode* callee =
              find_call_graph_node(call_graph,
                                   instr->instr.tac_tail_call.func_name);
          if (callee) {
            link_call_graph_nodes(caller, callee);
          }
          break;
        }
        case TACCALL_INDIRECT:
        case TACTAIL_CALL_INDIRECT: {
          // The target cannot be resolved statically from this TAC instruction.
          caller->contains_indirect_calls = true;
          break;
        }
        default:
          break;
      }
    }
  }
}

// Visit one node with Tarjan's algorithm. Discovery indices are unique across
// the entire DFS forest, while component_nodes is scratch storage for exactly
// the nodes popped when an SCC is completed.
static void tarjan_dfs(
    struct CallGraphNode* node, int* next_index,
    int* indices, int* lowlinks, bool* on_stack, struct Stack* stack,
    int* scc_sizes, int* component_nodes) {
  int node_index = node->node_index;
  indices[node_index] = *next_index;
  lowlinks[node_index] = *next_index;
  (*next_index)++;
  stack_push(stack, node_index);
  on_stack[node_index] = true;

  // iterate over all callee nodes of the current caller node
  for (struct CallGraphEntry* callee_entry = node->callees.head;
       callee_entry != NULL;
       callee_entry = callee_entry->next) {
    struct CallGraphNode* callee = callee_entry->node;
    int callee_index = callee->node_index;

    if (indices[callee_index] == -1) {
      // recursively visit the callee node if it has not been visited yet
      tarjan_dfs(callee, next_index, indices, lowlinks, on_stack, stack,
                 scc_sizes, component_nodes);
      lowlinks[node_index] = int_min(lowlinks[node_index],
                                     lowlinks[callee_index]);
    } else if (on_stack[callee_index]) {
      // A back edge can only reach as far back as the callee's discovery.
      // A callee no longer on the stack already belongs to another SCC.
      lowlinks[node_index] = int_min(lowlinks[node_index],
                                     indices[callee_index]);
    }
  }

  // if we are the root of an SCC, pop all nodes in this SCC from the stack
  if (lowlinks[node_index] == indices[node_index]) {
    int w;
    int component_size = 0;
    do {
      // always pop at least the current node
      w = stack_pop(stack);
      on_stack[w] = false;
      component_nodes[component_size++] = w;
    } while (w != node_index);

    // Assign the size only to members just popped from this SCC. Across the
    // whole traversal, each node participates in this loop exactly once.
    for (int i = 0; i < component_size; i++) {
      scc_sizes[component_nodes[i]] = component_size;
    }
  }
}

// Find strongly connected components (SCCs) in the call graph
// and mark nodes that can recurse
static void find_sccs(struct CallGraph* call_graph) {
  if (call_graph->num_nodes == 0) {
    return;
  }

  // initialize Tarjan's algorithm data structures
  int* indices = malloc(call_graph->num_nodes * sizeof(int));
  int* lowlinks = malloc(call_graph->num_nodes * sizeof(int));
  int* scc_sizes = malloc(call_graph->num_nodes * sizeof(int));
  int* component_nodes = malloc(call_graph->num_nodes * sizeof(int));
  // At most every graph node can be waiting on Tarjan's stack at once.
  struct Stack* stack = create_stack(call_graph->num_nodes);
  bool* on_stack = malloc(call_graph->num_nodes * sizeof(bool));

  // -1 => unvisited
  for (unsigned i = 0; i < call_graph->num_nodes; i++) {
    indices[i] = -1;
    lowlinks[i] = -1;
    scc_sizes[i] = 0;
    on_stack[i] = false;
  }

  // compute lowlinks for all nodes using Tarjan's algorithm
  int next_index = 0;
  for (struct CallGraphEntry* entry = call_graph->nodes.head; entry != NULL; entry = entry->next) {
    int node_index = entry->node->node_index;
    if (indices[node_index] == -1) { /* if node has not been visited */
      tarjan_dfs(entry->node, &next_index, indices, lowlinks, on_stack,
                 stack, scc_sizes, component_nodes);
    }
  }

  // a function can recurse iff it is part of an SCC with more than one node
  // or it calls itself directly
  // we ignore indirect calls for this analysis, we will never inline indirect calls
  for (struct CallGraphEntry* entry = call_graph->nodes.head; entry != NULL; entry = entry->next) {
    struct CallGraphNode* node = entry->node;
    if (scc_sizes[node->node_index] > 1) {
      // is part of nontrivial SCC
      node->can_recurse = true;
    } else {
      for (const struct CallGraphEntry* e = node->callees.head; e != NULL; e = e->next) {
        if (e->node == node) {
          // function calls itself directly
          node->can_recurse = true;
          break;
        }
      }
    }
  }

  free(indices);
  free(lowlinks);
  free_stack(stack);
  free(on_stack);
  free(scc_sizes);
  free(component_nodes);
}

struct CallGraph build_call_graph(struct TACProg* program) {
  struct CallGraph call_graph = {{NULL, NULL}, 0}; // initialize an empty call graph with zero nodes
  
  // iterate over all functions and add them to call graph
  for (struct TopLevel* top = program->head; top != NULL; top = top->next) {
    if (top->type != FUNC) {
      continue;
    }

    struct CallGraphNode* node = call_graph_node_create(&top->top.tac_func, call_graph.num_nodes);
    call_graph_node_list_add(&call_graph.nodes, node);
    call_graph.num_nodes++;
  }

  link_call_graph(&call_graph);
  find_sccs(&call_graph);

  return call_graph;
}

// Count nodes without storing a second size that could disagree with the list.
static unsigned call_graph_node_count(const struct CallGraphNodeList* nodes) {
  unsigned count = 0;
  for (const struct CallGraphEntry* entry = nodes->head;
       entry != NULL;
       entry = entry->next) {
    count++;
  }
  return count;
}

// Print one function name as a graph node.
static void print_call_graph_node_name(const struct CallGraphNode* node) {
  if (node == NULL || node->func_name == NULL) {
    printf("[UNKNOWN]");
    return;
  }
  printf("[");
  print_slice(node->func_name);
  printf("]");
}

// Print a stable adjacency-list representation. Function and edge order follow
// TAC definition and instruction order, respectively.
void print_call_graph(const struct CallGraph* call_graph) {
  printf("\n");
  if (call_graph == NULL) {
    printf("Call graph <null>\n");
    return;
  }

  unsigned node_count = call_graph_node_count(&call_graph->nodes);
  if (node_count == 0) {
    printf("Call graph: no function definitions\n");
    return;
  }

  printf("Call graph (%u function%s)\n", node_count,
         node_count == 1 ? "" : "s");
  printf("========================\n");

  for (const struct CallGraphEntry* function = call_graph->nodes.head;
       function != NULL;
       function = function->next) {
    const struct CallGraphNode* node = function->node;
    print_call_graph_node_name(node);
    printf("\n");

    bool has_direct_calls = node != NULL && node->callees.head != NULL;
    bool has_indirect_calls = node != NULL && node->contains_indirect_calls;
    if (!has_direct_calls && !has_indirect_calls) {
      printf("    calls: none\n\n");
      continue;
    }

    printf("    calls:\n");
    if (has_direct_calls) {
      for (const struct CallGraphEntry* callee = node->callees.head;
           callee != NULL;
           callee = callee->next) {
        bool is_last = callee->next == NULL && !has_indirect_calls;
        printf("      %s-- direct --> ", is_last ? "+" : "|");
        print_call_graph_node_name(callee->node);
        printf("\n");
      }
    }
    if (has_indirect_calls) {
      printf("      +-- indirect --> [UNKNOWN]\n");
    }
    printf("\n");
  }
}
