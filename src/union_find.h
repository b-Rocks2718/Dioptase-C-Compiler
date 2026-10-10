#ifndef UNION_FIND_H
#define UNION_FIND_H

// Union-Find (Disjoint Set) data structure with union by size 
// and path compression.
struct UnionFind {
  unsigned num_nodes;
  unsigned* parent; // array of parent indices for each node
  unsigned* reps; // allow 'representative' to differ from the root used by the dsu
  unsigned* size;   // array of sizes for each node
  unsigned num_unions; // count of union operations performed
};

// Creates a new union-find data structure with the specified number of nodes.
struct UnionFind uf_create(unsigned num_nodes);

// Returns the root of the set containing the specified node.
unsigned uf_root(struct UnionFind* uf, unsigned node);

// Finds the representative of the set containing the specified node.
// The representative may differ from the root used by the DSU; it is whatever
// the caller kept in uf_union, so registers win over pseudos only because the
// caller keeps the lower id.
unsigned uf_rep(struct UnionFind* uf, unsigned node);

// Unites the sets containing the two specified nodes. The merged set's
// representative becomes the representative of keep_node's set, so keep_node
// should itself be a representative (as returned by uf_rep).
void uf_union(struct UnionFind* uf, unsigned keep_node, unsigned merge_node);

// Destroys the union-find data structure and frees its resources.
void uf_destroy(struct UnionFind* uf);

#endif // UNION_FIND_H