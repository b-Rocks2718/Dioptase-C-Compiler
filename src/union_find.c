#include "union_find.h"

#include "checked_alloc.h"

#include <stdlib.h>

// Creates a new union-find data structure with the specified number of nodes.
struct UnionFind uf_create(unsigned num_nodes){
  struct UnionFind uf;
  uf.num_nodes = num_nodes;
  uf.parent = checked_calloc(num_nodes, sizeof(unsigned), "Union-find", "allocating the parent array");
  uf.reps = checked_calloc(num_nodes, sizeof(unsigned), "Union-find", "allocating the reps array");
  uf.size = checked_calloc(num_nodes, sizeof(unsigned), "Union-find", "allocating the size array");
  for (unsigned i = 0; i < num_nodes; i++) {
    // initialize where each set is a singleton
    uf.parent[i] = i;
    uf.reps[i] = i;
    uf.size[i] = 1;
  }
  uf.num_unions = 0;
  return uf;
}

// Destroys the union-find data structure and frees its resources.
void uf_destroy(struct UnionFind* uf) {
  free(uf->parent);
  free(uf->reps);
  free(uf->size);
  uf->num_nodes = 0;
  uf->num_unions = 0;
}

// Finds the root of the set containing the specified node.
unsigned uf_root(struct UnionFind* uf, unsigned node){
  while (uf->parent[node] != node) {
    uf->parent[node] = uf->parent[uf->parent[node]]; // path compression
    node = uf->parent[node]; // traverse up the tree
  }
  return node;
}

unsigned uf_rep(struct UnionFind* uf, unsigned node){
  return uf->reps[uf_root(uf, node)];
}

// Unites the sets containing the two specified nodes.
void uf_union(struct UnionFind* uf, unsigned keep_node, unsigned merge_node){
  unsigned keep_idx = uf_root(uf, keep_node);
  unsigned merge_idx = uf_root(uf, merge_node);
  if (keep_idx == merge_idx) return;

  uf->num_unions++;

  // attach the smaller tree to the root of the larger tree
  if (uf->size[keep_idx] < uf->size[merge_idx]) {
    uf->parent[keep_idx] = merge_idx;
    uf->size[merge_idx] += uf->size[keep_idx];
  } else {
    uf->parent[merge_idx] = keep_idx;
    uf->size[keep_idx] += uf->size[merge_idx];
  }

  // point merged node at the kept node for the set
  uf->reps[merge_idx] = uf->reps[keep_idx];
}
