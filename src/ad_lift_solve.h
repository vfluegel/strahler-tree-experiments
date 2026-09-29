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
 * With n domain vertices and t = floor(log2 n), the Strahler tree
 * U^K_{t,H_P} contains every ordered tree with at most n leaves, height H_P,
 * and Strahler number at most K. Every decomposition of a winning region fits
 * with K = k_full = min(H_P, t + 1), so that host is universal and its lifting
 * yields P's whole winning region. A smaller host yields a dominion of P that
 * may be smaller than P's winning region. */

typedef enum {
  /* The universal host U^{k_full}_{t,H}. */
  AD_LIFT_HOST_FULL,
  /* U^K_{t,H} for a fixed K, clamped to k_full. */
  AD_LIFT_HOST_STRAHLER,
  /* U^K_{t,H} for K = k, k + 1, ... until the dominions cover the domain. */
  AD_LIFT_HOST_ADAPTIVE,
  /* A caller-supplied base tree. */
  AD_LIFT_HOST_TREE,
} ADLiftHostMode;

typedef struct {
  ADLiftHostMode mode;
  /* STRAHLER: K. ADAPTIVE: the first K. At least one. */
  size_t k;
  /* TREE: the base tree, which must fit below both players' root levels. */
  OrderedTreeNode const *tree;
  /* Strahler hosts with more leaves are not built. */
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
  /* The Strahler bound of the host used; zero for a supplied tree. */
  size_t k;
  size_t k_full;
  /* ADAPTIVE with a complete result: the least K tried whose dominion was
   * already this player's whole winning region. Zero otherwise. */
  size_t first_full_k;
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
  /* t = floor(log2 n) for n domain vertices. */
  size_t t;
  /* ADAPTIVE: the least K at which the dominions covered the domain, or zero.
   */
  size_t first_complete_k;
  /* A Strahler host was not built because it exceeded max_host_leaves. With
   * ADAPTIVE, the result then holds the dominions found before that. */
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
