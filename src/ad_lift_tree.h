#ifndef AD_LIFT_TREE_H
#define AD_LIFT_TREE_H 1

#include <stddef.h>
#include <stdint.h>

#include "ordered_tree.h"
#include "pg_game.h"

/* The host (search) tree of the lifting algorithm: the base ordered tree T
 * with the leaves of leafy_∞(T) added and flattened into its depth-first
 * order. For a regular node η with children c_1, ..., c_m the order is
 *
 *   REGULAR(η) MINUS_INF(η) subtree(c_1) PLUS(c_1) ... subtree(c_m) PLUS(c_m)
 *
 * and TOP follows the root's subtree. Regular levels have the parity of the
 * player and drop by two per tree edge; MINUS_INF(η) and every PLUS(c_i) sit
 * one level below η. The positions of η's subtree form the half-open interval
 * [regular(η), after(η)), where after(η) is PLUS(η), or TOP at the root.
 *
 * This is only the space searched by the lifting. The attractor decomposition
 * itself is the part of it that a final labelling occupies. */

typedef enum {
  AD_POS_REGULAR,
  AD_POS_MINUS_INF,
  AD_POS_PLUS,
  AD_POS_TOP,
} ADPositionKind;

typedef struct {
  ADPositionKind kind;
  /* False for TOP and for the MINUS_INF child of a level-zero node, which lies
   * at level -1 and so accepts no priority. */
  bool has_level;
  uint64_t level;
  /* REGULAR and MINUS_INF: their node. PLUS: the child c whose c+ this is.
   * TOP: SIZE_MAX. */
  size_t node;
} ADPosition;

typedef struct {
  uint64_t level;
  size_t parent;
  size_t first_child;
  size_t next_sibling;
  size_t child_count;
  size_t regular;
  size_t minus_inf;
  size_t after;
} ADLiftNode;

/* Node 0 is the root and nodes are numbered in depth-first preorder. Missing
 * parents, children, and siblings are SIZE_MAX. */
typedef struct {
  PGPlayer player;
  ADPosition *positions;
  size_t position_count;
  ADLiftNode *nodes;
  size_t node_count;
  size_t leaf_count;
  size_t height;
  size_t top;
} ADLiftTree;

typedef struct {
  char message[256];
} ADLiftError;

/* Flatten base with its root at root_level, which must have the player's
 * parity and leave every regular level nonnegative. */
[[nodiscard]]
bool ad_lift_tree_build(OrderedTreeNode const *base, PGPlayer player,
                        uint64_t root_level, ADLiftTree *tree,
                        ADLiftError *error);

/* The full tree with levels regular levels, where every node above the
 * bottom level has arity children, flattened as above. */
[[nodiscard]]
bool ad_lift_tree_build_full(size_t arity, size_t levels, PGPlayer player,
                             uint64_t root_level, ADLiftTree *tree,
                             ADLiftError *error);

/* Node and leaf counts of that full tree; false if they overflow. */
[[nodiscard]]
bool ad_lift_full_tree_size(size_t arity, size_t levels, size_t *nodes,
                            size_t *leaves);

void ad_lift_tree_destroy(ADLiftTree *tree);

/* Regular positions accept exactly their level, MINUS_INF and PLUS positions
 * accept every priority up to their level, and TOP accepts all priorities. */
[[nodiscard]]
bool ad_position_accepts_priority(ADLiftTree const *tree, size_t position,
                                  uint64_t priority);

/* The first position at or after start that accepts priority. It exists
 * because TOP accepts everything. Returns SIZE_MAX if start is past TOP. */
[[nodiscard]]
size_t ad_lift_tree_first_compatible(ADLiftTree const *tree, size_t start,
                                     uint64_t priority);

#endif
