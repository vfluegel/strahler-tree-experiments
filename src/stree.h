#ifndef STREE_H
#define STREE_H 1

[[nodiscard]]
unsigned stree_count_leaves(int k, int t, int h);

[[nodiscard]]
char *stree_leaf_stream(int k, int t, int h);

[[nodiscard]]
char *stree_leaf_label(int k, int t, int h, int leaf_number);

#endif
