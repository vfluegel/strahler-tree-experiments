#ifndef STREE_H
#define STREE_H 1

/* Returns 0 on invalid input or allocation failure. The count saturates at
 * UINT_MAX, which therefore means "at least UINT_MAX leaves". */
[[nodiscard]]
unsigned stree_count_leaves(int k, int t, int h);

[[nodiscard]]
char *stree_leaf_stream(int k, int t, int h);

/* Fails when the leaf count saturates. */
[[nodiscard]]
char *stree_leaf_label(int k, int t, int h, int leaf_number);

#endif
