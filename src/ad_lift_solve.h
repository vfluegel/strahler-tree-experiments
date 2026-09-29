#ifndef AD_LIFT_SOLVE_H
#define AD_LIFT_SOLVE_H 1

#include <stddef.h>
#include <stdint.h>

#include "ad_lift.h"
#include "ordered_tree.h"

/* Both players are lifted independently over the same host shape. Player P's
 * root level is the least level of P's parity that bounds every priority in
 * the domain, so the Even and Odd roots are m and m + 1 in some order, where
 * m is the maximum priority. The host has H_P regular levels, from the root
 * down to P's parity.
 *
 * The Strahler tree U^K_{t,H_P} contains every ordered tree with fewer than
 * 2^(t+1) leaves, height H_P, and Strahler number at most K. With n domain
 * vertices, t_full = floor(log2 n), and k_full = min(H_P, t_full + 1), every
 * decomposition of a winning region fits, so that host is universal and its
 * lifting yields P's whole winning region. The full n-ary tree of height H_P
 * is universal as well. A smaller host yields a dominion of P that may be
 * smaller than P's winning region. */

typedef enum {
  /* The universal host U^{k_full}_{t_full,H}. */
  AD_LIFT_HOST_FULL,
  /* U^K_{t_full,H} for a fixed K, clamped to k_full. */
  AD_LIFT_HOST_STRAHLER,
  /* U^{min(H,t+1)}_{t,H} for t = t_start, t_start + 1, ..., t_full, until the
   * dominions cover the domain. K may be capped. */
  AD_LIFT_HOST_ADAPTIVE,
  /* The full tree of height H in which every internal node has the same
   * arity. */
  AD_LIFT_HOST_KARY,
  /* A caller-supplied base tree. */
  AD_LIFT_HOST_TREE,
} ADLiftHostMode;

typedef struct {
  ADLiftHostMode mode;
  /* STRAHLER: K, at least one. ADAPTIVE: a cap on K, or zero for none. */
  size_t k;
  /* ADAPTIVE: the first t. */
  size_t t;
  /* KARY: the arity, at least one. */
  size_t arity;
  /* TREE: the base tree, which must fit below both players' root levels. */
  OrderedTreeNode const *tree;
  /* Strahler and k-ary hosts with more leaves are not built. */
  size_t max_host_leaves;
  ADLiftSchedule schedule;
  /* Check every labelling directly and every materialized tree with both
   * decomposition verifiers. */
  bool verify;
} ADLiftOptions;

typedef struct {
  uint64_t root_level;
  /* Regular levels of a full host for this player. */
  size_t height;
  size_t k_full;
  /* The Strahler host U^k_{t,H} used; both zero for other hosts. */
  size_t k;
  size_t t;
  /* KARY: the arity of the host used; zero for other hosts. */
  size_t arity;
  /* ADAPTIVE with adaptive_complete: the least t tried whose dominion was
   * already this player's whole winning region. */
  size_t first_full_t;
  size_t host_nodes;
  size_t host_leaves;
  size_t host_positions;
  ADLiftStats lift;
  /* Metrics of the materialized decomposition; zero if it is empty. */
  ADTreeMetrics tree;
} ADLiftPlayerStats;

typedef struct {
  ADLiftPlayerStats player[2];
  size_t vertices;
  /* floor(log2 n) for n domain vertices. */
  size_t t_full;
  /* ADAPTIVE: whether the dominions covered the domain, and the least t at
   * which they did. */
  bool adaptive_complete;
  size_t first_complete_t;
  /* A host was not built because it exceeded max_host_leaves. With ADAPTIVE,
   * the result then holds the dominions found before that. */
  bool budget_exhausted;
} ADLiftSolveStats;

/* Priorities above this bound are rejected, as in zielonka_decompose, to keep
 * the recursive tree code within a safe depth. */
enum { AD_LIFT_MAX_PRIORITY = 1024 };

/* The result is complete exactly when the two dominions cover domain, which a
 * universal host guarantees; otherwise it is partial. domain must induce a
 * total subgame. stats may be nullptr. If an adaptive run stops at the leaf
 * limit, it still succeeds, stats->budget_exhausted is set, and error holds
 * the limit message. */
[[nodiscard]]
bool ad_lift_solve(PGGame const *game, PGSet const *domain,
                   ADLiftOptions const *options, ADResult *result,
                   ADLiftSolveStats *stats, ADLiftError *error);

#endif
