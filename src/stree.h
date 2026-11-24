#ifndef STREE_H
#define STREE_H 1

#include <stdbool.h>
#include <stdint.h>

/* Returns 0 on invalid input or allocation failure. The count saturates at
 * UINT_MAX, which therefore means "at least UINT_MAX leaves". */
[[nodiscard]]
unsigned stree_count_leaves(int k, int t, int h);

/* Exact bulk counts of U for 1<=k<=min(k_max,h), 0<=t<=t_max and
 * 1<=h<=h_max. Index is (h*(k_max+1)+k)*(t_max+1)+t; other cells are zero.
 * The caller frees *counts. Fails explicitly on uint64 overflow (ERANGE),
 * invalid dimensions, or allocation failure; the old capped API is unchanged.
 */
[[nodiscard]] bool stree_count_table(int k_max, int t_max, int h_max,
                                     uint64_t **counts);

[[nodiscard]]
char *stree_leaf_stream(int k, int t, int h);

/* Fails when the leaf count saturates. */
[[nodiscard]]
char *stree_leaf_label(int k, int t, int h, int leaf_number);

#endif
