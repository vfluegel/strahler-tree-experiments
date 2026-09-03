#ifndef AD_TREE_DOT_H
#define AD_TREE_DOT_H 1

#include <stddef.h>
#include <stdio.h>

#include "ad_tree.h"

typedef enum {
  AD_DOT_PLAYER_BOTH,
  AD_DOT_PLAYER_EVEN,
  AD_DOT_PLAYER_ODD,
} ADDotPlayer;

typedef enum {
  AD_DOT_LABEL_COUNTS,
  AD_DOT_LABEL_SETS,
  AD_DOT_LABEL_NONE,
} ADDotLabels;

/* A non-null priority map translates stored compact bounds back to the input
 * priority scale without modifying the decomposition. */
[[nodiscard]]
bool ad_tree_write_dot(FILE *out, PGGame const *game,
                       ZielonkaResult const *result, ADDotPlayer player,
                       ADDotLabels labels, size_t max_set_items,
                       PGPriorityMap const *priority_map);

#endif
