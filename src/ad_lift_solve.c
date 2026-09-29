#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "ad_lift_solve.h"
#include "pg_attractor.h"
#include "stree.h"

static void solve_error(ADLiftError *error, char const *format, ...) {
  if (error == nullptr || error->message[0] != '\0') {
    return;
  }
  va_list arguments;
  va_start(arguments, format);
  // The analyzer does not model C23 va_start, which initializes arguments.
  // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
  (void)vsnprintf(error->message, sizeof(error->message), format, arguments);
  va_end(arguments);
}

[[nodiscard]] static char const *player_name(PGPlayer const player) {
  return player == PG_EVEN ? "Even" : "Odd";
}

typedef struct {
  PGSet dominion;
  ADNode *decomposition;
} PlayerOutcome;

static void outcome_destroy(PlayerOutcome *outcome) {
  pg_set_destroy(&outcome->dominion);
  ad_node_destroy(outcome->decomposition);
  *outcome = (PlayerOutcome){0};
}

typedef struct {
  PGGame const *game;
  PGSet const *domain;
  ADLiftOptions const *options;
  ADLiftSolveStats *stats;
  ADLiftError *error;
  PlayerOutcome outcome[2];
} Solver;

/* Lift, check, and materialize one player's run, replacing its outcome. */
[[nodiscard]] static bool run_on_host(Solver *solver, ADLiftTree const *host,
                                      size_t const k) {
  PGPlayer const player = host->player;
  ADLiftPlayerStats *stats = solver->stats->player + player;
  ADLiftLabelling labelling = {0};
  ADLiftError lift_error = {0};
  ADVerifyError verify_error = {0};
  ADNode *root = nullptr;
  if (!ad_lift_run(solver->game, solver->domain, host,
                   solver->options->schedule, &labelling, &lift_error)) {
    solve_error(solver->error, "%s lifting failed: %s", player_name(player),
                lift_error.message);
    return false;
  }
  if (solver->options->verify &&
      !ad_lift_verify(solver->game, solver->domain, host, &labelling,
                      &verify_error)) {
    solve_error(solver->error, "%s labelling verification failed: %s",
                player_name(player), verify_error.message);
    goto failure;
  }
  if (!ad_lift_materialize(solver->game, host, &labelling, &root,
                           &lift_error)) {
    solve_error(solver->error, "%s materialization failed: %s",
                player_name(player), lift_error.message);
    goto failure;
  }
  if (solver->options->verify && root != nullptr &&
      (!ad_tree_verify(solver->game, &labelling.dominion, root,
                       &verify_error) ||
       !ad_tree_relative_verify(solver->game, &labelling.dominion, root,
                                &verify_error))) {
    solve_error(solver->error, "%s materialized tree verification failed: %s",
                player_name(player), verify_error.message);
    goto failure;
  }

  stats->k = k;
  stats->host_nodes = host->node_count;
  stats->host_leaves = host->leaf_count;
  stats->host_positions = host->position_count;
  stats->lift = labelling.stats;
  stats->tree = ad_tree_metrics(root);
  PlayerOutcome *outcome = solver->outcome + player;
  outcome_destroy(outcome);
  pg_set_move(&outcome->dominion, &labelling.dominion);
  outcome->decomposition = root;
  ad_lift_labelling_destroy(&labelling);
  return true;

failure:
  ad_node_destroy(root);
  ad_lift_labelling_destroy(&labelling);
  return false;
}

typedef enum {
  HOST_BUILT,
  HOST_OVER_BUDGET,
  HOST_FAILED,
} HostStatus;

[[nodiscard]] static HostStatus
run_strahler(Solver *solver, PGPlayer const player, size_t const k) {
  ADLiftPlayerStats const *shape = solver->stats->player + player;
  size_t const t = solver->stats->t;
  size_t const height = shape->height;
  /* The priority bound keeps height small, and t < 64. */
  unsigned const leaves = stree_count_leaves((int)k, (int)t, (int)height);
  if (leaves == 0) {
    solve_error(solver->error, "failed to count the leaves of U^%zu_{%zu,%zu}",
                k, t, height);
    return HOST_FAILED;
  }
  if (leaves == UINT_MAX || leaves > solver->options->max_host_leaves) {
    solver->stats->budget_exhausted = true;
    solve_error(solver->error,
                "the %s host U^%zu_{%zu,%zu} has %s%u leaves, more than the "
                "limit of %zu",
                player_name(player), k, t, height,
                leaves == UINT_MAX ? "at least " : "", leaves,
                solver->options->max_host_leaves);
    return HOST_OVER_BUDGET;
  }

  char *stream = stree_leaf_stream((int)k, (int)t, (int)height);
  OrderedTreeNode *base = nullptr;
  OrderedTreeError parse_error = {0};
  ADLiftTree host = {0};
  ADLiftError build_error = {0};
  bool const built =
      stream != nullptr &&
      ordered_tree_parse_leaf_stream(stream, &base, &parse_error) &&
      ad_lift_tree_build(base, player, shape->root_level, &host, &build_error);
  free(stream);
  ordered_tree_destroy(base);
  if (!built) {
    solve_error(
        solver->error, "failed to build the %s host U^%zu_{%zu,%zu}%s%s",
        player_name(player), k, t, height,
        build_error.message[0] == '\0' ? "" : ": ", build_error.message);
    return HOST_FAILED;
  }
  bool const ran = run_on_host(solver, &host, k);
  ad_lift_tree_destroy(&host);
  return ran ? HOST_BUILT : HOST_FAILED;
}

[[nodiscard]] static bool run_tree(Solver *solver, PGPlayer const player) {
  uint64_t const root_level = solver->stats->player[player].root_level;
  size_t const height = ordered_tree_height(solver->options->tree);
  if (height > root_level / 2) {
    solve_error(solver->error,
                "the host tree has height %zu, but the %s root level %" PRIu64
                " allows at most %" PRIu64,
                height, player_name(player), root_level, root_level / 2);
    return false;
  }
  ADLiftTree host = {0};
  ADLiftError build_error = {0};
  if (!ad_lift_tree_build(solver->options->tree, player, root_level, &host,
                          &build_error)) {
    solve_error(solver->error, "invalid %s host tree: %s", player_name(player),
                build_error.message);
    return false;
  }
  bool const ran = run_on_host(solver, &host, 0);
  ad_lift_tree_destroy(&host);
  return ran;
}

[[nodiscard]] static bool covered(Solver const *solver) {
  PGSet rest = {0};
  if (!pg_set_clone(&rest, solver->domain)) {
    return false;
  }
  for (size_t player = 0; player < 2; player++) {
    if (solver->outcome[player].dominion.bit_count != 0) {
      pg_set_subtract_into(&rest, &solver->outcome[player].dominion);
    }
  }
  bool const result = pg_set_empty(&rest);
  pg_set_destroy(&rest);
  return result;
}

/* Raise K until the dominions cover the domain. Once K reaches a player's
 * k_full its host stops changing, so its run is not repeated. If a host
 * exceeds the leaf limit after every player has run once, keep the dominions
 * found so far; they remain sound.
 *
 * Dominions need not grow monotonically with K, but each is contained in the
 * player's winning region, which the final dominion equals when the result is
 * complete. So the first K reaching the largest dominion seen is the least K
 * that already found the whole winning region. */
[[nodiscard]] static bool run_adaptive(Solver *solver) {
  ADLiftSolveStats *stats = solver->stats;
  size_t const last =
      stats->player[PG_EVEN].k_full > stats->player[PG_ODD].k_full
          ? stats->player[PG_EVEN].k_full
          : stats->player[PG_ODD].k_full;
  size_t used[2] = {0, 0};
  size_t best_count[2] = {0, 0};
  size_t best_k[2] = {0, 0};
  for (size_t k = solver->options->k;; k++) {
    for (size_t player = 0; player < 2; player++) {
      size_t const effective =
          k < stats->player[player].k_full ? k : stats->player[player].k_full;
      if (effective == used[player]) {
        continue;
      }
      HostStatus const status =
          run_strahler(solver, (PGPlayer)player, effective);
      if (status == HOST_FAILED ||
          (status == HOST_OVER_BUDGET && (used[0] == 0 || used[1] == 0))) {
        return false;
      }
      if (status == HOST_OVER_BUDGET) {
        /* The limit message stays in error for the caller to report. */
        return true;
      }
      used[player] = effective;
      size_t const count = pg_set_count(&solver->outcome[player].dominion);
      if (best_k[player] == 0 || count > best_count[player]) {
        best_count[player] = count;
        best_k[player] = effective;
      }
    }
    if (covered(solver)) {
      /* K beyond both players' k_full is clamped, so report the K in use. */
      stats->first_complete_k = used[0] > used[1] ? used[0] : used[1];
      stats->player[PG_EVEN].first_full_k = best_k[PG_EVEN];
      stats->player[PG_ODD].first_full_k = best_k[PG_ODD];
      return true;
    }
    if (k >= last) {
      return true;
    }
  }
}

bool ad_lift_solve(PGGame const *game, PGSet const *domain,
                   ADLiftOptions const *options, ADResult *result,
                   ADLiftSolveStats *stats, ADLiftError *error) {
  if (error != nullptr) {
    *error = (ADLiftError){0};
  }
  if (result != nullptr) {
    *result = (ADResult){0};
  }
  ADLiftSolveStats local_stats = {0};
  if (stats == nullptr) {
    stats = &local_stats;
  }
  *stats = (ADLiftSolveStats){0};
  if (game == nullptr || domain == nullptr || options == nullptr ||
      result == nullptr || domain->bit_count != game->vertex_count ||
      options->mode > AD_LIFT_HOST_TREE ||
      options->schedule > AD_LIFT_SCHEDULE_SINGLE ||
      ((options->mode == AD_LIFT_HOST_STRAHLER ||
        options->mode == AD_LIFT_HOST_ADAPTIVE) &&
       options->k == 0) ||
      (options->mode == AD_LIFT_HOST_TREE && options->tree == nullptr) ||
      !pg_subgame_is_total(game, domain)) {
    solve_error(error, "invalid lifting solver input");
    return false;
  }
  if (!pg_set_init(&result->region[PG_EVEN], game->vertex_count) ||
      !pg_set_init(&result->region[PG_ODD], game->vertex_count) ||
      !pg_set_init(&result->unresolved, game->vertex_count)) {
    solve_error(error, "failed to allocate the result sets");
    ad_result_destroy(result);
    return false;
  }

  size_t const vertices = pg_set_count(domain);
  stats->vertices = vertices;
  if (vertices == 0) {
    return true;
  }
  uint64_t maximum = 0;
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (game->vertices[vertex].priority > maximum) {
      maximum = game->vertices[vertex].priority;
    }
  }
  if (maximum > AD_LIFT_MAX_PRIORITY) {
    solve_error(error, "priority depth exceeds the safe recursion depth");
    ad_result_destroy(result);
    return false;
  }
  for (size_t rest = vertices; rest > 1; rest >>= 1) {
    stats->t++;
  }
  for (size_t player = 0; player < 2; player++) {
    ADLiftPlayerStats *shape = stats->player + player;
    shape->root_level = maximum % 2 == player ? maximum : maximum + 1;
    shape->height = (size_t)((shape->root_level - player) / 2) + 1;
    shape->k_full = shape->height < stats->t + 1 ? shape->height : stats->t + 1;
  }

  Solver solver = {
      .game = game,
      .domain = domain,
      .options = options,
      .stats = stats,
      .error = error,
  };
  bool succeeded = true;
  switch (options->mode) {
  case AD_LIFT_HOST_FULL:
  case AD_LIFT_HOST_STRAHLER:
    for (size_t player = 0; succeeded && player < 2; player++) {
      size_t const k_full = stats->player[player].k_full;
      size_t const k = options->mode == AD_LIFT_HOST_FULL || options->k > k_full
                           ? k_full
                           : options->k;
      succeeded = run_strahler(&solver, (PGPlayer)player, k) == HOST_BUILT;
    }
    break;
  case AD_LIFT_HOST_ADAPTIVE:
    succeeded = run_adaptive(&solver);
    break;
  case AD_LIFT_HOST_TREE:
    for (size_t player = 0; succeeded && player < 2; player++) {
      succeeded = run_tree(&solver, (PGPlayer)player);
    }
    break;
  }

  if (succeeded) {
    for (size_t player = 0; player < 2; player++) {
      PlayerOutcome *outcome = solver.outcome + player;
      pg_set_move(&result->region[player], &outcome->dominion);
      result->decomposition[player] = outcome->decomposition;
      outcome->decomposition = nullptr;
    }
    PGSet overlap = {0};
    succeeded = pg_set_clone(&overlap, &result->region[PG_EVEN]);
    if (succeeded) {
      pg_set_intersect_into(&overlap, &result->region[PG_ODD]);
      pg_set_union_into(&result->unresolved, domain);
      pg_set_subtract_into(&result->unresolved, &result->region[PG_EVEN]);
      pg_set_subtract_into(&result->unresolved, &result->region[PG_ODD]);
      result->kind = pg_set_empty(&result->unresolved) ? AD_RESULT_COMPLETE
                                                       : AD_RESULT_PARTIAL;
      if (!pg_set_empty(&overlap)) {
        solve_error(error, "the Even and Odd dominions overlap");
        succeeded = false;
      } else if (options->mode != AD_LIFT_HOST_TREE &&
                 stats->player[PG_EVEN].k == stats->player[PG_EVEN].k_full &&
                 stats->player[PG_ODD].k == stats->player[PG_ODD].k_full &&
                 result->kind != AD_RESULT_COMPLETE) {
        solve_error(error, "the universal hosts left vertices unresolved");
        succeeded = false;
      }
    } else {
      solve_error(error, "failed to allocate the result sets");
    }
    pg_set_destroy(&overlap);
  }
  outcome_destroy(solver.outcome + PG_EVEN);
  outcome_destroy(solver.outcome + PG_ODD);
  if (!succeeded) {
    ad_result_destroy(result);
  }
  return succeeded;
}
