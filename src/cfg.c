#include "cfg.h"
#include "slice_index.h"
#include "exit_codes.h"
#include "arena.h"
#include "slice.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Allocate a disconnected entry node.
static struct CFGNode* make_start_node(void) {
  struct CFGNode* node = (struct CFGNode*)arena_alloc(sizeof(struct CFGNode));
  node->type = CFG_ENTRY;
  node->index = 0; // assigned when the node is placed in a CFG
  node->predecessors.head = NULL;
  node->predecessors.tail = NULL;
  node->successors.head = NULL;
  node->successors.tail = NULL;
  node->body = tac_instr_list(NULL);
  node->asm_head = NULL;
  node->asm_last = NULL;
  node->marked = false;
  return node;
}

// Allocate a disconnected exit node.
static struct CFGNode* make_exit_node(void) {
  struct CFGNode* node = (struct CFGNode*)arena_alloc(sizeof(struct CFGNode));
  node->type = CFG_EXIT;
  node->index = 0; // assigned when the node is placed in a CFG
  node->predecessors.head = NULL;
  node->predecessors.tail = NULL;
  node->successors.head = NULL;
  node->successors.tail = NULL;
  node->body = tac_instr_list(NULL);
  node->asm_head = NULL;
  node->asm_last = NULL;
  node->marked = false;
  return node;
}

// Allocate an empty basic block node.
static struct CFGNode* make_basic_block_node(void) {
  struct CFGNode* node = (struct CFGNode*)arena_alloc(sizeof(struct CFGNode));
  node->type = CFG_BASIC_BLOCK;
  node->index = 0; // assigned when the node is placed in a CFG
  node->predecessors.head = NULL;
  node->predecessors.tail = NULL;
  node->successors.head = NULL;
  node->successors.tail = NULL;
  node->body = tac_instr_list(NULL);
  node->asm_head = NULL;
  node->asm_last = NULL;
  node->marked = false;
  return node;
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

// Return whether list contains no nodes.
bool cfg_node_list_is_empty(const struct CFGNodeList* list) {
  return list->head == NULL;
}

// Return whether list holds node. Membership is pointer identity.
bool cfg_node_list_contains(const struct CFGNodeList* list, const struct CFGNode* node) {
  for (const struct CFGNodeEntry* entry = list->head; entry != NULL; entry = entry->next) {
    if (entry->node == node) {
      return true;
    }
  }
  return false;
}

// Append node at the end of list, keeping earlier entries in order.
void cfg_node_list_append(struct CFGNodeList* list, struct CFGNode* node) {
  struct CFGNodeEntry* entry = make_cfg_node_entry(node);
  if (cfg_node_list_is_empty(list)) {
    list->head = entry;
    list->tail = entry;
  } else {
    list->tail->next = entry;
    list->tail = entry;
  }
}

// Remove the oldest node. Append adds at the tail, so repeated removal drains in insertion order.
// An empty list returns NULL and is left with both head and tail NULL.
struct CFGNode* cfg_node_list_remove_front(struct CFGNodeList* list) {
  if (cfg_node_list_is_empty(list)) {
    return NULL;
  }
  struct CFGNodeEntry* entry = list->head;
  list->head = entry->next;
  if (list->head == NULL) {
    list->tail = NULL;
  }
  return entry->node;
}

// Skip CFG nodes that already have predecessor/successor links.
static bool nodes_already_linked(const struct CFGNode* parent,
                                 const struct CFGNode* child) {
  return cfg_node_list_contains(&parent->successors, child);
}

// Link two CFGNodes bidirectionally:
// `parent` a predecessor of `child` and `child` a successor of `parent`
static void link_nodes(struct CFGNode* parent, struct CFGNode* child) {
  if (!nodes_already_linked(parent, child)) {
    cfg_node_list_append(&parent->successors, child);
    cfg_node_list_append(&child->predecessors, parent);
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
  cfg_number_nodes(cfg);
  return cfg;
}

// Partition a sequence of instructions into basic blocks. The returned CFG
// contains entry and exit nodes, but its nodes have not yet been linked.
static struct CFG* partition_into_basic_blocks(const void* body, const struct CFGInstrOps* ops) {
  struct CFGNodeList* list = make_cfg_node_list();

  struct CFGNode* cur_block = make_basic_block_node();

  // loop over function body
  unsigned num_blocks = 0;
  for (const void* instr = body; instr != NULL; instr = ops->next(instr)) {
    const struct Slice* label = NULL;
    switch (ops->classify(instr, &label)) {
      case CFG_INSTR_LABEL:
        // start a new basic block and add the label to it
        if (ops->block_first(cur_block) != NULL) {
          cfg_node_list_append(list, cur_block);
          num_blocks++;
          cur_block = make_basic_block_node();
        }
        ops->append(cur_block, instr);
        break;
      case CFG_INSTR_JUMP:
      case CFG_INSTR_COND_JUMP:
      case CFG_INSTR_RETURN:
      case CFG_INSTR_TAIL_CALL:
        // append jump/return/tail call to current block, then begin a new one
        ops->append(cur_block, instr);
        cfg_node_list_append(list, cur_block);
        num_blocks++;
        cur_block = make_basic_block_node();
        break;
      case CFG_INSTR_OTHER:
        // most instructions just get added to the current block
        ops->append(cur_block, instr);
        break;
    }
  }

  if (ops->block_first(cur_block) != NULL) {
    cfg_node_list_append(list, cur_block);
    num_blocks++;
  }

  return make_cfg_from_basic_blocks(list, num_blocks);
}

// Return the label node starts with, or NULL if it does not start with one.
// Labels only ever begin a block, so the first instruction is the only one
// that can be a jump target.
static const struct Slice* block_leading_label(const struct CFGNode* node,
                                               const struct CFGInstrOps* ops) {
  const void* first = ops->block_first(node);
  const struct Slice* label = NULL;
  if (first == NULL || ops->classify(first, &label) != CFG_INSTR_LABEL) {
    return NULL;
  }
  return label;
}

// Return the first block (in layout order) that starts with label, or NULL.
// A linear scan; link_cfg uses a LabelIndex instead.
static struct CFGNode* find_block_with_label(const struct CFG* cfg, const struct CFGInstrOps* ops,
                                             const struct Slice* label) {
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    const struct Slice* leading = block_leading_label(cfg->nodes[i], ops);
    if (leading != NULL && compare_slice_to_slice(leading, label)) {
      return cfg->nodes[i];
    }
  }
  return NULL;
}

// Map a block's leading label (by content) to the block, so resolving each
// jump is O(1) instead of a scan over every block. Pass-local to link_cfg.
struct LabelIndex {
  struct SliceIndex ids;   // dense ids in layout order
  struct CFGNode** blocks; // blocks[id]: first block that starts with the label
};

// Index every block that begins with a label. Like find_block_with_label, the
// first block (in layout order) with a given label wins.
static struct LabelIndex label_index_build(const struct CFG* cfg, const struct CFGInstrOps* ops) {
  struct LabelIndex index;
  slice_index_init(&index.ids, cfg->num_nodes);
  index.blocks = calloc(cfg->num_nodes == 0 ? 1 : cfg->num_nodes, sizeof(*index.blocks));
  if (index.blocks == NULL) {
    fprintf(stderr,
            "CFG error: unable to allocate a label index while linking a %u-node CFG\n",
            cfg->num_nodes);
    exit(BCC_EXIT_INTERNAL);
  }
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    const struct Slice* label = block_leading_label(cfg->nodes[i], ops);
    if (label == NULL) {
      continue;
    }
    bool added = false;
    uint32_t id = slice_index_add(&index.ids, label, &added);
    if (added) {
      index.blocks[id] = cfg->nodes[i];
    }
  }
  return index;
}

// Return the block that starts with label, or NULL.
static struct CFGNode* label_index_find(const struct LabelIndex* index, const struct Slice* label) {
  uint32_t id = slice_index_get(&index->ids, label);
  return id == SLICE_INDEX_NONE ? NULL : index->blocks[id];
}

// Release a label index; blocks and labels are not owned.
static void label_index_free(struct LabelIndex* index) {
  slice_index_free(&index->ids);
  free(index->blocks);
}

// Link basic blocks and populate their CFG edges.
static struct CFG* link_cfg(struct CFG* cfg, const struct CFGInstrOps* ops) {
  struct CFGNode* start_node = cfg->nodes[0];
  struct CFGNode* exit_node = cfg->nodes[cfg->num_nodes - 1];
  struct LabelIndex labels = label_index_build(cfg, ops);

  // link ENTRY to first basic block
  link_nodes(start_node, cfg->nodes[1]);

  // loop over the basic block nodes
  for (unsigned i = 1; i < cfg->num_nodes - 1; i++) {
    struct CFGNode* cur = cfg->nodes[i];
    struct CFGNode* next = cfg->nodes[i + 1];
    const void* last_instr = ops->block_last(cur);
    if (last_instr == NULL) {
      continue;
    }
    const struct Slice* label = NULL;
    switch (ops->classify(last_instr, &label)) {
      case CFG_INSTR_COND_JUMP: // link to next and jump target
        link_nodes(cur, next);
        /* fall through */
      case CFG_INSTR_JUMP: { // link to only jump target
        struct CFGNode* target = label_index_find(&labels, label);
        if (target != NULL) {
          link_nodes(cur, target);
        } else {
          fprintf(stderr, "CFG error: cannot resolve jump target label '");
          fwrite(label->start, sizeof(char), label->len, stderr);
          fprintf(stderr, "' while linking basic block %u\n", i);
          exit(BCC_EXIT_INTERNAL);
        }
        break;
      }
      case CFG_INSTR_RETURN: // link to only exit
      case CFG_INSTR_TAIL_CALL:
        link_nodes(cur, exit_node);
        break;
      case CFG_INSTR_LABEL:
      case CFG_INSTR_OTHER: // link to only next basic block
        link_nodes(cur, next);
        break;
    }
  }

  label_index_free(&labels);
  return cfg;
}

struct CFG* build_cfg_with(const void* body, const struct CFGInstrOps* ops) {
  struct CFG* cfg = partition_into_basic_blocks(body, ops);
  return link_cfg(cfg, ops);
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
                                 const struct CFGInstrOps* ops,
                                 const struct CFGNode* parent,
                                 const struct CFGNode* child) {
  if (parent->type == CFG_ENTRY) {
    return "entry";
  }
  const void* last_instr = parent->type == CFG_BASIC_BLOCK ? ops->block_last(parent) : NULL;
  if (last_instr == NULL) {
    return "edge";
  }

  const struct Slice* label = NULL;
  switch (ops->classify(last_instr, &label)) {
    case CFG_INSTR_COND_JUMP: {
      const struct CFGNode* target = find_block_with_label(cfg, ops, label);
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
    case CFG_INSTR_JUMP:
      return "jump";
    case CFG_INSTR_RETURN:
      return "return";
    case CFG_INSTR_TAIL_CALL:
      return "tail call";
    case CFG_INSTR_LABEL:
    case CFG_INSTR_OTHER:
      return "fallthrough";
  }
  return "edge";
}

// ASCII visualization of the CFG

enum {
  CFG_ASCII_MAX_COLUMNS = 120,
  CFG_ASCII_MAX_ROWS = 72,
  CFG_ASCII_NODE_WIDTH = 7,
  CFG_ASCII_NODE_SPACING = 13,
  CFG_ASCII_LANE_GAP = 3,
  CFG_ASCII_LANE_SPACING = 3,
  CFG_ASCII_SELF_LOOP_PADDING = 3,
};

// Own the character grid used while routing an ASCII CFG rendering.
struct CFGAsciiCanvas {
  char* cells;
  unsigned rows;
  unsigned columns;
};

// Classify edges by the routing shape needed in the ASCII visualization.
enum CFGAsciiEdgeKind {
  CFG_ASCII_ADJACENT_EDGE,
  CFG_ASCII_FORWARD_EDGE,
  CFG_ASCII_BACK_EDGE,
  CFG_ASCII_SELF_EDGE,
};

// Record endpoints and assigned routing coordinates for one rendered CFG edge.
struct CFGAsciiEdge {
  unsigned source;
  unsigned target;
  enum CFGAsciiEdgeKind kind;
  unsigned departure_row;
  unsigned approach_row;
  unsigned lane;
};

// Merge a character into the routing canvas. A real bend is added explicitly
// as '+'. At an unrelated crossing, the vertical route passes over the
// horizontal route and remains visible as '|'.
static void cfg_ascii_put(struct CFGAsciiCanvas* canvas,
                          unsigned row,
                          unsigned column,
                          char value) {
  if (row >= canvas->rows || column >= canvas->columns) {
    return;
  }

  char* cell = &canvas->cells[row * canvas->columns + column];
  if (*cell == ' ' || *cell == value) {
    *cell = value;
    return;
  }

  bool old_is_arrow = *cell == '<' || *cell == '>' || *cell == '^' || *cell == 'v';
  bool new_is_arrow = value == '<' || value == '>' || value == '^' || value == 'v';
  if (new_is_arrow) {
    *cell = old_is_arrow && *cell != value ? '*' : value;
    return;
  }
  if (old_is_arrow) {
    return;
  }

  if (value == '+') {
    *cell = '+';
    return;
  }
  if (*cell == '+') {
    return;
  }
  if ((*cell == '-' && value == '|') || (*cell == '|' && value == '-')) {
    *cell = '|';
  }
}

// Draw a horizontal edge segment in the CFG visualization.
static void cfg_ascii_horizontal(struct CFGAsciiCanvas* canvas,
                                 unsigned row,
                                 unsigned first_column,
                                 unsigned last_column) {
  if (first_column > last_column) {
    return;
  }
  for (unsigned column = first_column; column <= last_column; column++) {
    cfg_ascii_put(canvas, row, column, '-');
  }
}

// Draw a vertical edge segment in the CFG visualization.
static void cfg_ascii_vertical(struct CFGAsciiCanvas* canvas,
                               unsigned column,
                               unsigned first_row,
                               unsigned last_row) {
  if (first_row > last_row) {
    return;
  }
  for (unsigned row = first_row; row <= last_row; row++) {
    cfg_ascii_put(canvas, row, column, '|');
  }
}

// Format a stable node label and return its length.
static unsigned cfg_ascii_node_label(const struct CFG* cfg,
                                     unsigned node_index,
                                     char label[16]) {
  const struct CFGNode* node = cfg->nodes[node_index];
  if (node->type == CFG_ENTRY) {
    memcpy(label, "[ENTRY]", 8);
    return 7;
  }
  if (node->type == CFG_EXIT) {
    memcpy(label, "[EXIT]", 7);
    return 6;
  }

  int written = snprintf(label, 16, "[B%u]", node_index);
  return written > 0 ? (unsigned)written : 0;
}

// Draw the terminating segment of a labeled CFG edge.
static unsigned cfg_ascii_label_end(const struct CFG* cfg,
                                    const unsigned* node_columns,
                                    unsigned node_index) {
  char label[16];
  unsigned length = cfg_ascii_node_label(cfg, node_index, label);
  return node_columns[node_index] - length / 2 + length - 1;
}

// Free the temporary rank, routing, and edge arrays used by the ASCII renderer.
static void cfg_ascii_free_layout(unsigned* ranks,
                                  unsigned* rank_sizes,
                                  unsigned* rank_positions,
                                  unsigned* node_columns,
                                  unsigned* node_rows,
                                  unsigned* gap_routes,
                                  unsigned* gap_cursors,
                                  bool* rank_has_self_edge,
                                  struct CFGAsciiEdge* edges) {
  free(ranks);
  free(rank_sizes);
  free(rank_positions);
  free(node_columns);
  free(node_rows);
  free(gap_routes);
  free(gap_cursors);
  free(rank_has_self_edge);
  free(edges);
}

// Print a compact layered graph beneath the detailed block listing. Forward
// edges determine node ranks, which allows independent branches to sit beside
// one another. Backedges and edges that skip ranks use distinct outer lanes.
static void print_cfg_ascii_graph(const struct CFG* cfg) {
  if (cfg->num_nodes < 2) {
    printf("Graph omitted: a CFG requires distinct entry and exit nodes.\n");
    return;
  }

  unsigned edge_count = 0;
  for (unsigned source = 0; source < cfg->num_nodes; source++) {
    const struct CFGNode* node = cfg->nodes[source];
    if (node == NULL) {
      printf("Graph omitted: CFG node %u is null.\n", source);
      return;
    }
    for (const struct CFGNodeEntry* edge = node->successors.head;
         edge != NULL;
         edge = edge->next) {
      unsigned target = cfg_node_index(cfg, edge->node);
      if (target == cfg->num_nodes) {
        printf("Graph omitted: CFG node %u has an unregistered successor.\n", source);
        return;
      }
      edge_count++;
    }
  }

  unsigned* ranks = (unsigned*)calloc(cfg->num_nodes, sizeof(unsigned));
  unsigned* rank_sizes = (unsigned*)calloc(cfg->num_nodes + 1, sizeof(unsigned));
  unsigned* rank_positions = (unsigned*)calloc(cfg->num_nodes + 1, sizeof(unsigned));
  unsigned* node_columns = (unsigned*)calloc(cfg->num_nodes, sizeof(unsigned));
  unsigned* node_rows = (unsigned*)calloc(cfg->num_nodes, sizeof(unsigned));
  unsigned* gap_routes = (unsigned*)calloc(cfg->num_nodes + 1, sizeof(unsigned));
  unsigned* gap_cursors = (unsigned*)calloc(cfg->num_nodes + 1, sizeof(unsigned));
  bool* rank_has_self_edge =
      (bool*)calloc(cfg->num_nodes + 1, sizeof(bool));
  struct CFGAsciiEdge* edges =
      edge_count == 0
          ? NULL
          : (struct CFGAsciiEdge*)calloc(edge_count, sizeof(struct CFGAsciiEdge));
  if (ranks == NULL || rank_sizes == NULL || rank_positions == NULL ||
      node_columns == NULL || node_rows == NULL || gap_routes == NULL ||
      gap_cursors == NULL || rank_has_self_edge == NULL ||
      (edge_count != 0 && edges == NULL)) {
    printf("Graph omitted: unable to allocate layout data.\n");
    cfg_ascii_free_layout(ranks, rank_sizes, rank_positions, node_columns,
                          node_rows, gap_routes, gap_cursors,
                          rank_has_self_edge, edges);
    return;
  }

  // Ignoring backedges makes the source-ordered CFG acyclic. Longest-path
  // ranks then place joins below every forward predecessor. Unreachable basic
  // blocks begin on the first rank so they remain visible without lengthening
  // the reachable graph unnecessarily.
  for (unsigned node_index = 1; node_index + 1 < cfg->num_nodes; node_index++) {
    ranks[node_index] = 1;
  }
  unsigned max_basic_rank = 0;
  for (unsigned source = 0; source + 1 < cfg->num_nodes; source++) {
    if (ranks[source] > max_basic_rank) {
      max_basic_rank = ranks[source];
    }
    for (const struct CFGNodeEntry* edge = cfg->nodes[source]->successors.head;
         edge != NULL;
         edge = edge->next) {
      unsigned target = cfg_node_index(cfg, edge->node);
      if (target > source && target + 1 < cfg->num_nodes &&
          ranks[target] < ranks[source] + 1) {
        ranks[target] = ranks[source] + 1;
      }
    }
  }
  ranks[cfg->num_nodes - 1] = max_basic_rank + 1;
  unsigned rank_count = max_basic_rank + 2;

  unsigned max_rank_size = 0;
  for (unsigned node_index = 0; node_index < cfg->num_nodes; node_index++) {
    unsigned rank = ranks[node_index];
    rank_sizes[rank]++;
    if (rank_sizes[rank] > max_rank_size) {
      max_rank_size = rank_sizes[rank];
    }
  }

  unsigned left_edges = 0;
  unsigned right_edges = 0;
  unsigned edge_index = 0;
  for (unsigned source = 0; source < cfg->num_nodes; source++) {
    for (const struct CFGNodeEntry* entry = cfg->nodes[source]->successors.head;
         entry != NULL;
         entry = entry->next) {
      unsigned target = cfg_node_index(cfg, entry->node);
      struct CFGAsciiEdge* edge = &edges[edge_index++];
      edge->source = source;
      edge->target = target;
      if (target == source) {
        edge->kind = CFG_ASCII_SELF_EDGE;
        rank_has_self_edge[ranks[source]] = true;
      } else if (ranks[target] == ranks[source] + 1) {
        edge->kind = CFG_ASCII_ADJACENT_EDGE;
        gap_routes[ranks[source]]++;
      } else if (ranks[target] > ranks[source]) {
        edge->kind = CFG_ASCII_FORWARD_EDGE;
        gap_routes[ranks[source]]++;
        gap_routes[ranks[target] - 1]++;
        right_edges++;
      } else {
        edge->kind = CFG_ASCII_BACK_EDGE;
        gap_routes[ranks[source]]++;
        gap_routes[ranks[target]]++;
        left_edges++;
      }
    }
  }

  unsigned rows = 1;
  for (unsigned rank = 0; rank + 1 < rank_count; rank++) {
    if (rank_has_self_edge[rank]) {
      gap_routes[rank]++;
      gap_cursors[rank] = 1;
    }
    if (gap_routes[rank] == 0) {
      gap_routes[rank] = 1;
    }
    rows += gap_routes[rank] + 2;
  }

  unsigned node_area_columns =
      CFG_ASCII_NODE_WIDTH + (max_rank_size - 1) * CFG_ASCII_NODE_SPACING;
  unsigned self_loop_padding = 0;
  for (unsigned rank = 0; rank < rank_count; rank++) {
    if (rank_has_self_edge[rank]) {
      self_loop_padding = CFG_ASCII_SELF_LOOP_PADDING;
      break;
    }
  }
  unsigned node_origin =
      left_edges == 0
          ? 0
          : (left_edges - 1) * CFG_ASCII_LANE_SPACING + 1 + CFG_ASCII_LANE_GAP;
  unsigned columns = node_origin + node_area_columns + self_loop_padding;
  if (right_edges != 0) {
    columns += CFG_ASCII_LANE_GAP +
               (right_edges - 1) * CFG_ASCII_LANE_SPACING + 1;
  }
  if (rows > CFG_ASCII_MAX_ROWS || columns > CFG_ASCII_MAX_COLUMNS) {
    printf("Graph omitted: layout requires %u rows by %u columns; limit is %u by %u.\n",
           rows, columns, CFG_ASCII_MAX_ROWS, CFG_ASCII_MAX_COLUMNS);
    cfg_ascii_free_layout(ranks, rank_sizes, rank_positions, node_columns,
                          node_rows, gap_routes, gap_cursors,
                          rank_has_self_edge, edges);
    return;
  }

  unsigned row = 0;
  for (unsigned rank = 0; rank < rank_count; rank++) {
    for (unsigned node_index = 0; node_index < cfg->num_nodes; node_index++) {
      if (ranks[node_index] == rank) {
        node_rows[node_index] = row;
      }
    }
    if (rank + 1 < rank_count) {
      row += gap_routes[rank] + 2;
    }
  }

  for (unsigned node_index = 0; node_index < cfg->num_nodes; node_index++) {
    char label[16];
    unsigned label_length = cfg_ascii_node_label(cfg, node_index, label);
    if (label_length == 0 || label_length > CFG_ASCII_NODE_WIDTH) {
      printf("Graph omitted: label for CFG node %u exceeds the %u-column node width.\n",
             node_index, CFG_ASCII_NODE_WIDTH);
      cfg_ascii_free_layout(ranks, rank_sizes, rank_positions, node_columns,
                            node_rows, gap_routes, gap_cursors,
                            rank_has_self_edge, edges);
      return;
    }
    unsigned rank = ranks[node_index];
    unsigned position = rank_positions[rank]++;
    unsigned relative_column = node_area_columns / 2;
    if (rank_sizes[rank] > 1) {
      relative_column = CFG_ASCII_NODE_WIDTH / 2 +
                        position * (node_area_columns - CFG_ASCII_NODE_WIDTH) /
                            (rank_sizes[rank] - 1);
    }
    node_columns[node_index] = node_origin + relative_column;
  }

  unsigned left_edge_index = 0;
  unsigned right_edge_index = 0;
  for (unsigned i = 0; i < edge_count; i++) {
    struct CFGAsciiEdge* edge = &edges[i];
    unsigned source_rank = ranks[edge->source];
    unsigned target_rank = ranks[edge->target];
    if (edge->kind == CFG_ASCII_ADJACENT_EDGE) {
      edge->departure_row =
          node_rows[edge->source] + 1 + gap_cursors[source_rank]++;
    } else if (edge->kind == CFG_ASCII_FORWARD_EDGE) {
      edge->departure_row =
          node_rows[edge->source] + 1 + gap_cursors[source_rank]++;
      edge->approach_row =
          node_rows[edge->target] - gap_routes[target_rank - 1] - 1 +
          gap_cursors[target_rank - 1]++;
      edge->lane = node_origin + node_area_columns + self_loop_padding +
                   CFG_ASCII_LANE_GAP +
                   right_edge_index++ * CFG_ASCII_LANE_SPACING;
    } else if (edge->kind == CFG_ASCII_BACK_EDGE) {
      edge->departure_row =
          node_rows[edge->source] + 1 + gap_cursors[source_rank]++;
      edge->approach_row =
          node_rows[edge->target] + 1 + gap_cursors[target_rank]++;
      edge->lane = left_edge_index++ * CFG_ASCII_LANE_SPACING;
    }
  }

  struct CFGAsciiCanvas canvas = {
      .cells = (char*)malloc((size_t)rows * columns),
      .rows = rows,
      .columns = columns,
  };
  if (canvas.cells == NULL) {
    printf("Graph omitted: unable to allocate a %u-row by %u-column canvas.\n",
           rows, columns);
    cfg_ascii_free_layout(ranks, rank_sizes, rank_positions, node_columns,
                          node_rows, gap_routes, gap_cursors,
                          rank_has_self_edge, edges);
    return;
  }
  memset(canvas.cells, ' ', (size_t)rows * columns);

  // Draw routes first. Corners and arrowheads are overlaid in a second pass so
  // only genuine connections replace the vertical-over-horizontal crossing.
  for (unsigned i = 0; i < edge_count; i++) {
    const struct CFGAsciiEdge* edge = &edges[i];
    unsigned source_column = node_columns[edge->source];
    unsigned target_column = node_columns[edge->target];
    unsigned source_row = node_rows[edge->source];
    unsigned target_row = node_rows[edge->target];

    if (edge->kind == CFG_ASCII_SELF_EDGE) {
      unsigned source_end = cfg_ascii_label_end(cfg, node_columns, edge->source);
      unsigned loop_column = source_column + CFG_ASCII_NODE_WIDTH / 2 + 2;
      cfg_ascii_horizontal(&canvas, source_row, source_end + 1, loop_column);
      cfg_ascii_vertical(&canvas, loop_column, source_row, source_row + 1);
      cfg_ascii_horizontal(&canvas, source_row + 1,
                           source_column, loop_column);
    } else if (edge->kind == CFG_ASCII_ADJACENT_EDGE) {
      cfg_ascii_vertical(&canvas, source_column, source_row + 1,
                         edge->departure_row);
      if (source_column != target_column) {
        cfg_ascii_horizontal(&canvas, edge->departure_row,
                             source_column < target_column ? source_column
                                                           : target_column,
                             source_column < target_column ? target_column
                                                           : source_column);
      }
      cfg_ascii_vertical(&canvas, target_column, edge->departure_row,
                         target_row - 1);
    } else {
      cfg_ascii_vertical(&canvas, source_column, source_row + 1,
                         edge->departure_row);
      cfg_ascii_horizontal(&canvas, edge->departure_row,
                           source_column < edge->lane ? source_column : edge->lane,
                           source_column < edge->lane ? edge->lane : source_column);
      cfg_ascii_vertical(&canvas, edge->lane,
                         edge->departure_row < edge->approach_row
                             ? edge->departure_row
                             : edge->approach_row,
                         edge->departure_row < edge->approach_row
                             ? edge->approach_row
                             : edge->departure_row);
      cfg_ascii_horizontal(&canvas, edge->approach_row,
                           target_column < edge->lane ? target_column : edge->lane,
                           target_column < edge->lane ? edge->lane : target_column);
      if (edge->kind == CFG_ASCII_FORWARD_EDGE) {
        cfg_ascii_vertical(&canvas, target_column, edge->approach_row,
                           target_row - 1);
      } else {
        cfg_ascii_vertical(&canvas, target_column, target_row + 1,
                           edge->approach_row);
      }
    }
  }

  for (unsigned i = 0; i < edge_count; i++) {
    const struct CFGAsciiEdge* edge = &edges[i];
    unsigned source_column = node_columns[edge->source];
    unsigned target_column = node_columns[edge->target];
    unsigned source_row = node_rows[edge->source];
    unsigned target_row = node_rows[edge->target];

    if (edge->kind == CFG_ASCII_SELF_EDGE) {
      unsigned loop_column = source_column + CFG_ASCII_NODE_WIDTH / 2 + 2;
      cfg_ascii_put(&canvas, source_row, loop_column, '+');
      cfg_ascii_put(&canvas, source_row + 1, loop_column, '+');
      cfg_ascii_put(&canvas, source_row + 1, source_column, '^');
    } else if (edge->kind == CFG_ASCII_ADJACENT_EDGE) {
      if (source_column != target_column) {
        cfg_ascii_put(&canvas, edge->departure_row, source_column, '+');
        cfg_ascii_put(&canvas, edge->departure_row, target_column, '+');
      }
      cfg_ascii_put(&canvas, target_row - 1, target_column, 'v');
    } else {
      cfg_ascii_put(&canvas, edge->departure_row, source_column, '+');
      cfg_ascii_put(&canvas, edge->departure_row, edge->lane, '+');
      cfg_ascii_put(&canvas, edge->approach_row, edge->lane, '+');
      cfg_ascii_put(&canvas, edge->approach_row, target_column, '+');
      cfg_ascii_put(&canvas,
                    edge->kind == CFG_ASCII_FORWARD_EDGE ? target_row - 1
                                                         : target_row + 1,
                    target_column,
                    edge->kind == CFG_ASCII_FORWARD_EDGE ? 'v' : '^');
    }
  }

  for (unsigned node_index = 0; node_index < cfg->num_nodes; node_index++) {
    char label[16];
    unsigned length = cfg_ascii_node_label(cfg, node_index, label);
    unsigned start_column = node_columns[node_index] - length / 2;
    unsigned node_row = node_rows[node_index];
    for (unsigned i = 0; i < length; i++) {
      canvas.cells[node_row * columns + start_column + i] = label[i];
    }
  }

  for (unsigned row = 0; row < rows; row++) {
    unsigned line_length = columns;
    while (line_length > 0 && canvas.cells[row * columns + line_length - 1] == ' ') {
      line_length--;
    }
    if (line_length > 0) {
      fwrite(&canvas.cells[row * columns], sizeof(char), line_length, stdout);
    }
    printf("\n");
  }
  free(canvas.cells);
  cfg_ascii_free_layout(ranks, rank_sizes, rank_positions, node_columns,
                        node_rows, gap_routes, gap_cursors,
                        rank_has_self_edge, edges);
}

// Print an ASCII representation of a CFG. Blocks appear in source order,
// instructions are indented below them, and labeled arrows name all outgoing
// edges. Backedges remain easy to spot because block names use stable indices.
void print_cfg_with(const struct CFG* cfg, const struct CFGInstrOps* ops) {
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
      if (ops->block_first(node) == NULL) {
        printf("    <empty block>\n");
      } else {
        ops->print_block(node);
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
               cfg_edge_kind(cfg, ops, node, edge->node));
        print_cfg_node_name(cfg, edge->node);
        printf("\n");
      }
    }
    printf("\n");
  }

  print_cfg_ascii_graph(cfg);
}

// Record each node's position in cfg->nodes in its index field.
void cfg_number_nodes(struct CFG* cfg) {
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    cfg->nodes[i]->index = i;
  }
}

// Reset all marked flags in the CFG for traversal.
void reset_marks(struct CFG* cfg) {
  if (cfg == NULL) {
    return;
  }
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    struct CFGNode* block = cfg->nodes[i];
    block->marked = false;
  }
}
