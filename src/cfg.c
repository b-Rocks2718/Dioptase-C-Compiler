#include "cfg.h"
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

// Copy an instruction without retaining links owned by another TAC list.
static struct TACInstr* copy_instr(const struct TACInstr* instr) {
  // Operand pointers continue to refer to the original TAC values, but list
  // links belong exclusively to the new instruction list.
  struct TACInstr* instr_copy = (struct TACInstr*)arena_alloc(sizeof(struct TACInstr));
  *instr_copy = *instr;
  instr_copy->next = NULL;
  instr_copy->last = instr_copy;
  return instr_copy;
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

  struct TACInstr* instr_copy = copy_instr(instr);

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

struct CFGAsciiCanvas {
  char* cells;
  unsigned rows;
  unsigned columns;
};

enum CFGAsciiEdgeKind {
  CFG_ASCII_ADJACENT_EDGE,
  CFG_ASCII_FORWARD_EDGE,
  CFG_ASCII_BACK_EDGE,
  CFG_ASCII_SELF_EDGE,
};

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

static unsigned cfg_ascii_label_end(const struct CFG* cfg,
                                    const unsigned* node_columns,
                                    unsigned node_index) {
  char label[16];
  unsigned length = cfg_ascii_node_label(cfg, node_index, label);
  return node_columns[node_index] - length / 2 + length - 1;
}

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

  print_cfg_ascii_graph(cfg);
}

// Rebuild a detached TAC function body from basic blocks in layout order. The
// copies keep CFG block lists independent, so rebuilding does not consume or
// otherwise mutate the graph.
struct TACInstr* rebuild_body(struct CFG* cfg) {
  if (cfg == NULL || cfg->nodes == NULL) {
    fprintf(stderr,
            "CFG error: cannot rebuild a TAC body from a null or uninitialized CFG\n");
    exit(1);
  }

  struct TACInstr* head = NULL;
  struct TACInstr* tail = NULL;

  // add instructions from each basic block in layout order
  for (unsigned i = 0; i < cfg->num_nodes; i++) {
    struct CFGNode* node = cfg->nodes[i];
    if (node == NULL || node->type != CFG_BASIC_BLOCK || node->body == NULL) {
      continue;
    }
    if (node->last_instr == NULL) {
      fprintf(stderr,
              "CFG error: cannot rebuild basic block %u because its non-empty "
              "body has no last instruction\n",
              i);
      exit(1);
    }

    struct TACInstr* instr = node->body;
    // add each instruction from a basic block
    while (true) {
      struct TACInstr* instr_copy = copy_instr(instr);
      if (head == NULL) {
        head = instr_copy;
      } else {
        tail->next = instr_copy;
      }
      tail = instr_copy;

      if (instr == node->last_instr) {
        break;
      }
      instr = instr->next;
      if (instr == NULL) {
        fprintf(stderr,
                "CFG error: cannot rebuild basic block %u because its last "
                "instruction is not reachable from its body\n",
                i);
        exit(1);
      }
    }
  }

  if (head != NULL) {
    head->last = tail;
  }
  return head;
}
