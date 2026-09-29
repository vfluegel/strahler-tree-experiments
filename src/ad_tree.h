#ifndef AD_TREE_H
#define AD_TREE_H 1

#include <stddef.h>
#include <stdint.h>

#include "pg_game.h"
#include "pg_set.h"

typedef struct ADNode ADNode;

typedef struct {
  PGSet trap;
  PGSet attractor;
  ADNode *subtree;
} ADChild;

struct ADNode {
  PGPlayer player;
  uint64_t priority_bound;
  PGSet top_attractor;
  size_t child_count;
  size_t child_capacity;
  ADChild *children;
};

typedef enum {
  /* The two regions partition the domain, so they are the winning regions. */
  AD_RESULT_COMPLETE,
  /* The regions are disjoint certified dominions, and unresolved is the rest
   * of the domain. Nothing is claimed about who wins there. */
  AD_RESULT_PARTIAL,
} ADResultKind;

/* region[p] is decomposed by decomposition[p], which is nullptr exactly when
 * the region is empty. */
typedef struct {
  PGSet region[2];
  PGSet unresolved;
  ADNode *decomposition[2];
  ADResultKind kind;
} ADResult;

typedef struct {
  size_t nodes;
  size_t leaves;
  size_t height;
  size_t strahler;
} ADTreeMetrics;

typedef struct {
  char message[256];
} ADVerifyError;

typedef struct {
  PGSet highest;
  PGSet top;
  PGSet side;
} ADTreeRelativeParts;

[[nodiscard]] ADNode *ad_node_create(PGPlayer player, uint64_t priority_bound,
                                     size_t vertex_count);
void ad_node_destroy(ADNode *node);

/* On success, ownership of all fields in child moves into parent. */
[[nodiscard]] bool ad_node_append_child(ADNode *parent, ADChild *child);

void ad_result_destroy(ADResult *result);

[[nodiscard]] ADTreeMetrics ad_tree_metrics(ADNode const *root);

[[nodiscard]] bool ad_tree_verify(PGGame const *game, PGSet const *domain,
                                  ADNode const *root, ADVerifyError *error);

/* Derive the tree-relative parts for a classic node: H is the priority-bound
 * target in core, T is the rest of the stored top attractor, and S is
 * outer \ core. The caller owns the returned sets and must destroy them with
 * ad_tree_relative_parts_destroy. */
[[nodiscard]] bool ad_tree_relative_parts(PGGame const *game,
                                          PGSet const *outer, PGSet const *core,
                                          ADNode const *node,
                                          ADTreeRelativeParts *parts);
void ad_tree_relative_parts_destroy(ADTreeRelativeParts *parts);

[[nodiscard]] bool ad_tree_relative_verify(PGGame const *game,
                                           PGSet const *domain,
                                           ADNode const *root,
                                           ADVerifyError *error);

/* Each nonempty region must be an opponent trap in domain with a verified
 * classic decomposition, the regions must be disjoint, and unresolved must be
 * the rest of domain. Complete results pass as well. */
[[nodiscard]] bool ad_result_verify_partial(PGGame const *game,
                                            PGSet const *domain,
                                            ADResult const *result,
                                            ADVerifyError *error);

/* The partial conditions, plus a complete kind and regions that partition
 * domain. */
[[nodiscard]] bool ad_result_verify_complete(PGGame const *game,
                                             PGSet const *domain,
                                             ADResult const *result,
                                             ADVerifyError *error);

#endif
