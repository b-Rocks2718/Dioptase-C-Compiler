#include "cfg.h"
#include "arena.h"
#include "slice.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Allocate a disconnected entry node.
static struct CFGNode* make_start_node(void) {
  struct CFGNode* node = (struct CFGNode*)arena_alloc(sizeof(struct CFGNode));
  node->type = CFG_ENTRY;
  node->predecessors.head = NULL;
  node->predecessors.tail = NULL;
  node->successors.head = NULL;
  node->successors.tail = NULL;
  node->body = NULL;
  node->last_instr = NULL;
  return node;
}

// Allocate a disconnected exit node.
static struct CFGNode* make_exit_node(void) {
  struct CFGNode* node = (struct CFGNode*)arena_alloc(sizeof(struct CFGNode));
  node->type = CFG_EXIT;
  node->predecessors.head = NULL;
  node->predecessors.tail = NULL;
  node->successors.head = NULL;
  node->successors.tail = NULL;
  node->body = NULL;
  node->last_instr = NULL;
  return node;
}

// Allocate an empty basic block node.
static struct CFGNode* make_basic_block_node(void) {
  struct CFGNode* node = (struct CFGNode*)arena_alloc(sizeof(struct CFGNode));
  node->type = CFG_BASIC_BLOCK;
  node->predecessors.head = NULL;
  node->predecessors.tail = NULL;
  node->successors.head = NULL;
  node->successors.tail = NULL;
  node->body = NULL;
  node->last_instr = NULL;
  return node;
}

// Add a detached copy of an instruction to a basic block node.
static void append_instr(struct CFGNode* block, const struct TACInstr* instr) {
  if (block->type != CFG_BASIC_BLOCK) {
    fprintf(stderr,
            "CFG error: cannot append TAC instruction type %d to CFG node type %d; "
            "expected a basic block\n",
            instr->type, block->type);
    exit(1);
  }

  // Operand pointers continue to refer to the original TAC values, but list
  // links belong exclusively to the block so building the CFG cannot mutate
  // or create cycles in the input instruction list.
  struct TACInstr* instr_copy = (struct TACInstr*)arena_alloc(sizeof(struct TACInstr));
  *instr_copy = *instr;
  instr_copy->next = NULL;
  instr_copy->last = instr_copy;

  if (block->body == NULL) {
    block->body = instr_copy;
  } else {
    block->last_instr->next = instr_copy;
    block->body->last = instr_copy;
  }
  block->last_instr = instr_copy;
}

// Allocate an empty CFGNodeList.
static struct CFGNodeList* make_cfg_node_list(void) {
  struct CFGNodeList* list = (struct CFGNodeList*)arena_alloc(sizeof(struct CFGNodeList));
  list->head = NULL;
  list->tail = NULL;
  return list;
}

// create a new CFGNodeEntry for a given CFGNode, allowing it to be added to linked lists
static struct CFGNodeEntry* make_cfg_node_entry(struct CFGNode* node) {
  struct CFGNodeEntry* entry = (struct CFGNodeEntry*)arena_alloc(sizeof(struct CFGNodeEntry));
  entry->node = node;
  entry->next = NULL;
  return entry;
}

// append a CFGNode to a CFGNodeList
static void append_cfg_node(struct CFGNodeList* list, struct CFGNode* node) {
  struct CFGNodeEntry* entry = make_cfg_node_entry(node);
  if (list->tail == NULL) {
    list->head = entry;
    list->tail = entry;
  } else {
    list->tail->next = entry;
    list->tail = entry;
  }
}

static bool nodes_already_linked(const struct CFGNode* parent,
                                 const struct CFGNode* child) {
  for (struct CFGNodeEntry* entry = parent->successors.head; entry != NULL; entry = entry->next) {
    if (entry->node == child) {
      return true;
    }
  }
  return false;
}

// Link two CFGNodes bidirectionally:
// `parent` a predecessor of `child` and `child` a successor of `parent`
static void link_nodes(struct CFGNode* parent, struct CFGNode* child) {
  if (!nodes_already_linked(parent, child)) {
    append_cfg_node(&parent->successors, child);
    append_cfg_node(&child->predecessors, parent);
  }
}

// create a CFG from a list of basic blocks, adding in ENTRY and EXIT nodes
static struct CFG* make_cfg_from_basic_blocks(struct CFGNodeList* list,
                                               size_t num_nodes) {
  struct CFG* cfg = (struct CFG*)arena_alloc(sizeof(struct CFG));
  cfg->num_nodes = num_nodes + 2; // ENTRY and EXIT nodes
  cfg->nodes = (struct CFGNode**)arena_alloc(sizeof(struct CFGNode*) * cfg->num_nodes);
  unsigned i = 1; // first node is always ENTRY node
  cfg->nodes[0] = make_start_node();
  for (struct CFGNodeEntry* entry = list->head; entry != NULL; entry = entry->next) {
    cfg->nodes[i++] = entry->node;
  }
  cfg->nodes[i++] = make_exit_node(); // final node is always EXIT node
  return cfg;
}

// Partition a sequence of TAC instructions into basic blocks. The returned CFG
// contains entry and exit nodes, but its nodes have not yet been linked.
static struct CFG* partition_into_basic_blocks(const struct TACInstr* body) {
  struct CFGNodeList* list = make_cfg_node_list();

  struct CFGNode* cur_block = make_basic_block_node();

  // loop over function body
  unsigned num_blocks = 0;
  for (const struct TACInstr* instr = body; instr != NULL; instr = instr->next) {
    switch (instr->type) {
      case TACLABEL:
        // start a new basic block and add the label to it
        if (cur_block->body != NULL) {
          append_cfg_node(list, cur_block);
          num_blocks++;
          cur_block = make_basic_block_node();
        }
        append_instr(cur_block, instr);
        break;
      case TACJUMP:
      case TACCOND_JUMP:
      case TACRETURN:
        // append jump/return to current block, then begin a new one
        append_instr(cur_block, instr);
        append_cfg_node(list, cur_block);
        num_blocks++;
        cur_block = make_basic_block_node();
        break;
      default:
        // most instructions just get added to the current block
        append_instr(cur_block, instr);
        break;
    }
  }

  if (cur_block->body != NULL) {
    append_cfg_node(list, cur_block);
    num_blocks++;
  }

  return make_cfg_from_basic_blocks(list, num_blocks);
}

// find the basic block that is the target of a jump instruction
// returns NULL on failure (this should never happen)
static struct CFGNode* find_target_of_jump(const struct CFG* cfg,
                                           const struct TACInstr* jump_instr) {
  const struct Slice* target_label = NULL;

  switch (jump_instr->type) {
    case TACJUMP:
      target_label = jump_instr->instr.tac_jump.label;
      break;
    case TACCOND_JUMP:
      target_label = jump_instr->instr.tac_cond_jump.label;
      break;
    default:
      return NULL;
  }

  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    struct CFGNode* node = cfg->nodes[i];
    if (node->body != NULL) {
      // need only check first instruction of the block
      // labels cannot appear in the middle of a basic block, only at the beginning
      struct TACInstr* instr = node->body;
      if (instr->type == TACLABEL &&
          compare_slice_to_slice(instr->instr.tac_label.label, target_label)) {
        return node;
      }
    }
  }
  return NULL;
}

static struct CFG* link_cfg(struct CFG* cfg) {
  struct CFGNode* start_node = cfg->nodes[0];
  struct CFGNode* exit_node = cfg->nodes[cfg->num_nodes - 1];

  // link ENTRY to first basic block
  link_nodes(start_node, cfg->nodes[1]);

  // loop over the basic block nodes
  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    struct CFGNode* cur = cfg->nodes[i];
    struct CFGNode* next = cfg->nodes[i + 1];
    if (cur->body != NULL) {
      struct TACInstr* last_instr = cur->last_instr;
      switch (last_instr->type) {
        case TACCOND_JUMP: // link to next and jump target
          link_nodes(cur, next);
          /* fall through */
        case TACJUMP: { // link to only jump target
          struct CFGNode* target = find_target_of_jump(cfg, last_instr);
          if (target != NULL) {
            link_nodes(cur, target);
          } else {
            const struct Slice* label = last_instr->type == TACJUMP
                                            ? last_instr->instr.tac_jump.label
                                            : last_instr->instr.tac_cond_jump.label;
            fprintf(stderr, "CFG error: cannot resolve jump target label '");
            fwrite(label->start, sizeof(char), label->len, stderr);
            fprintf(stderr, "' while linking basic block %u\n", i);
            exit(1);
          }
          break;
        }
        case TACRETURN: // link to only exit
          link_nodes(cur, exit_node);
          break;
        default: // link to only next basic block
          link_nodes(cur, next);
          break;
      }
    }
  }

  return cfg;
}

// build a CFG for the body of a TAC function
struct CFG* build_cfg(struct TACInstr* body) {
  struct CFG* cfg = partition_into_basic_blocks(body);
  return link_cfg(cfg);
}

// Return the array index of a node, or cfg->num_nodes when it is not registered
// in the graph. Node indices give basic blocks stable names in printed output.
static unsigned cfg_node_index(const struct CFG* cfg, const struct CFGNode* node) {
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    if (cfg->nodes[i] == node) {
      return i;
    }
  }
  return cfg->num_nodes;
}

// Print a compact, stable name for a CFG node.
static void print_cfg_node_name(const struct CFG* cfg, const struct CFGNode* node) {
  if (node == NULL) {
    printf("[NULL]");
    return;
  }

  unsigned index = cfg_node_index(cfg, node);
  if (index == cfg->num_nodes) {
    printf("[UNREGISTERED]");
    return;
  }

  switch (node->type) {
    case CFG_ENTRY:
      printf("[ENTRY]");
      break;
    case CFG_EXIT:
      printf("[EXIT]");
      break;
    case CFG_BASIC_BLOCK:
      printf("[B%u]", index);
      break;
    default:
      printf("[UNKNOWN %u]", index);
      break;
  }
}

// Describe why an edge exists. Conditional jumps can target the immediately
// following block, in which case duplicate-edge suppression leaves one edge
// carrying both meanings.
static const char* cfg_edge_kind(const struct CFG* cfg,
                                 const struct CFGNode* parent,
                                 const struct CFGNode* child) {
  if (parent->type == CFG_ENTRY) {
    return "entry";
  }
  if (parent->type != CFG_BASIC_BLOCK || parent->last_instr == NULL) {
    return "edge";
  }

  switch (parent->last_instr->type) {
    case TACCOND_JUMP: {
      const struct CFGNode* target = find_target_of_jump(cfg, parent->last_instr);
      unsigned parent_index = cfg_node_index(cfg, parent);
      const struct CFGNode* fallthrough =
          parent_index + 1 < cfg->num_nodes ? cfg->nodes[parent_index + 1] : NULL;
      bool is_branch = child == target;
      bool is_fallthrough = child == fallthrough;
      if (is_branch && is_fallthrough) {
        return "branch + fallthrough";
      }
      if (is_branch) {
        return "branch";
      }
      if (is_fallthrough) {
        return "fallthrough";
      }
      return "edge";
    }
    case TACJUMP:
      return "jump";
    case TACRETURN:
      return "return";
    default:
      return "fallthrough";
  }
}

// Print an ASCII representation of a CFG. Blocks appear in source order, TAC
// instructions are indented below them, and labeled arrows name all outgoing
// edges. Backedges remain easy to spot because block names use stable indices.
void print_cfg(const struct CFG* cfg) {
  if (cfg == NULL) {
    printf("CFG <null>\n");
    return;
  }
  if (cfg->nodes == NULL || cfg->num_nodes == 0) {
    printf("CFG <invalid: no nodes>\n");
    return;
  }

  printf("CFG (%u nodes)\n", cfg->num_nodes);
  printf("===============\n");

  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    const struct CFGNode* node = cfg->nodes[i];
    print_cfg_node_name(cfg, node);
    printf("\n");

    if (node == NULL) {
      printf("    edges: unavailable\n\n");
      continue;
    }

    if (node->type == CFG_BASIC_BLOCK) {
      if (node->body == NULL) {
        printf("    <empty block>\n");
      } else {
        for (const struct TACInstr* instr = node->body;
             instr != NULL;
             instr = instr->next) {
          print_tac_instr(instr, 1);
        }
      }
    }

    if (node->successors.head == NULL) {
      printf("    edges: none\n");
    } else {
      printf("    edges:\n");
      for (const struct CFGNodeEntry* edge = node->successors.head;
           edge != NULL;
           edge = edge->next) {
        printf("      %s-- %s --> ", edge->next == NULL ? "+" : "|",
               cfg_edge_kind(cfg, node, edge->node));
        print_cfg_node_name(cfg, edge->node);
        printf("\n");
      }
    }
    printf("\n");
  }
}

// rebuild the body of a TAC function from its CFG
struct TACInstr* rebuild_body(struct CFG* cfg) {
  (void)cfg;
  // TODO
  return NULL;
}
