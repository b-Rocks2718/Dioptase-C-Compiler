#ifndef INTERFERENCE_GRAPH_H
#define INTERFERENCE_GRAPH_H

#include "asm_gen.h"
#include "asm_cfg.h"
#include "bitset.h"
#include "slice_index.h"

#include <stdbool.h>
#include <stddef.h>

// Types of interference nodes: physical register or pseudo-register.
enum InterferenceNodeType {
  INTERFERENCE_REG,
  INTERFERENCE_PSEUDO
};

// Represents the identifier of an interference node, 
// which can be either a physical register or a pseudo-register.
union InterferenceNodeIdVariant {
  enum Reg reg;
  struct Slice* pseudo;
};

// Represents a node in the interference graph,
// corresponding to a register or pseudo-register.
struct InterferenceNode {
  enum InterferenceNodeType node_type;
  union InterferenceNodeIdVariant id;
  // Row of InterferenceGraph.adjacency: bit j is set iff this node
  // interferes with nodes[j]. Empty (words == NULL) until
  // build_interference_graph allocates the matrix.
  struct Bitset neighbors;
  unsigned num_neighbors; // number of bits set in neighbors
  int spill_cost;
  int color;
  bool pruned;
};

// Number of registers the allocator may assign: R1-R8 and R11-R27. R0 is
// hardwired to zero, R9-R10 are codegen's scratch registers, R28 is reserved
// as the future TLS base, and R29-R31 are RA, BP and SP (docs/abi.md).
#define NUM_ALLOCATABLE_REGS 25

// Represents the interference graph used for register allocation.
//
// Every node has a dense id: its index in nodes. Ids
// 0..NUM_ALLOCATABLE_REGS-1 are the allocatable registers in register order;
// pseudos follow in first-occurrence order, so pseudo k (its pseudo_ids id)
// is node NUM_ALLOCATABLE_REGS + k. Edges are a num_nodes x num_nodes bit
// matrix, one Bitset row per node.
//
// All storage is heap-backed; release it with destroy_interference_graph.
// Pointers into nodes are invalidated while pseudos are being added (the
// array grows) and stay valid once build_interference_graph returns.
struct InterferenceGraph {
  struct InterferenceNode* nodes;
  size_t num_nodes;
  size_t nodes_capacity;
  // Register-allocatable pseudos. Pseudos that must stay in memory (see
  // add_pseudos) are absent.
  struct SliceIndex pseudo_ids;
  uint64_t* adjacency; // num_nodes rows of neighbors.word_count words
};

// Builds the interference graph for the given assembly instructions.
struct InterferenceGraph* build_interference_graph(struct AsmInstr* instrs);

// Release the graph's storage, including ig itself.
void destroy_interference_graph(struct InterferenceGraph* ig);

// Node for an allocatable register, or NULL for a register the allocator
// never assigns.
struct InterferenceNode* interference_graph_reg_node(struct InterferenceGraph* ig, enum Reg reg);

// Node for the pseudo named name, or NULL if that pseudo is not a candidate
// for a register (it stays in memory and gets a stack slot or data label).
struct InterferenceNode* interference_graph_pseudo_node(struct InterferenceGraph* ig,
                                                        const struct Slice* name);

// Record that nodes a and b interfere. Self-edges and repeated edges are
// ignored; num_neighbors counts each distinct neighbor once.
void interference_graph_add_edge(struct InterferenceGraph* ig, size_t a, size_t b);

// Adds spill costs to the interference graph based on the given assembly instructions.
void add_spill_costs(struct InterferenceGraph* ig, struct AsmInstr* instrs);

// Colors the interference graph, assigning registers to pseudos where possible.
void color_graph(struct InterferenceGraph* ig);

#endif // INTERFERENCE_GRAPH_H