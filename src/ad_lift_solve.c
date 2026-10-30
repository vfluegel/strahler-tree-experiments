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
  ADLiftTree hosts[2];
} Solver;

/* Lift, check, and materialize one player's run, replacing its outcome. */
[[nodiscard]] static bool run_on_host(Solver *solver, ADLiftTree *host) {
  PGPlayer const player = host->player;
  AD_TRACE(solver->options->trace, .kind = AD_TRACE_PHASE, .player = player,
           .name = "host_built", .to = host->node_count);
  ADLiftPlayerStats *stats = solver->stats->player + player;
  if (solver->options->symmetric) {
    stats->host_nodes = host->node_count;
    stats->host_leaves = host->leaf_count;
    stats->host_positions = host->position_count;
    solver->hosts[player] = *host;
    *host = (ADLiftTree){0};
    return true;
  }
  ADLiftLabelling labelling = {0};
  ADLiftError lift_error = {0};
  ADVerifyError verify_error = {0};
  ADNode *root = nullptr;
  PGSet const *known_losing = &solver->outcome[1 - player].dominion;
  if (!solver->options->incremental || known_losing->bit_count == 0) {
    known_losing = nullptr;
  }
  if (!ad_lift_run_trace(solver->game, solver->domain, host,
                         solver->options->schedule, known_losing, &labelling,
                         &lift_error, solver->options->trace)) {
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

  AD_TRACE(solver->options->trace, .kind = AD_TRACE_PHASE, .player = player,
           .name = "dominion_certified", .vertices = &labelling.dominion);
  if (solver->options->trace != nullptr && solver->options->trace->failed) {
    solve_error(solver->error, "trace observer stopped the run");
    goto failure;
  }
  stats->host_nodes = host->node_count;
  stats->host_leaves = host->leaf_count;
  stats->host_positions = host->position_count;
  stats->lift = labelling.stats;
  stats->total_lifts += labelling.stats.lifts;
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

[[nodiscard]] static HostStatus over_budget(Solver *solver,
                                            char const *message) {
  solver->stats->budget_exhausted = true;
  solve_error(solver->error, "%s", message);
  return HOST_OVER_BUDGET;
}

[[nodiscard]] static HostStatus run_strahler(Solver *solver,
                                             PGPlayer const player,
                                             size_t const k, size_t const t) {
  ADLiftPlayerStats *shape = solver->stats->player + player;
  size_t const height = shape->height;
  /* The priority bound keeps height small, and t < 64. */
  unsigned const leaves = stree_count_leaves((int)k, (int)t, (int)height);
  if (leaves == 0) {
    solve_error(solver->error, "failed to count the leaves of U^%zu_{%zu,%zu}",
                k, t, height);
    return HOST_FAILED;
  }
  // SIZE_MAX allows resource budgets to govern hosts beyond counter saturation.
  if (solver->options->max_host_leaves != SIZE_MAX &&
      (leaves == UINT_MAX || leaves > solver->options->max_host_leaves)) {
    char message[sizeof(solver->error->message)];
    (void)snprintf(message, sizeof(message),
                   "the %s host U^%zu_{%zu,%zu} has %s%u leaves, more than the "
                   "limit of %zu",
                   player_name(player), k, t, height,
                   leaves == UINT_MAX ? "at least " : "", leaves,
                   solver->options->max_host_leaves);
    return over_budget(solver, message);
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
  bool const ran = run_on_host(solver, &host);
  ad_lift_tree_destroy(&host);
  if (!ran) {
    return HOST_FAILED;
  }
  shape->k = k;
  shape->t = t;
  return HOST_BUILT;
}

[[nodiscard]] static HostStatus run_kary(Solver *solver,
                                         PGPlayer const player) {
  ADLiftPlayerStats *shape = solver->stats->player + player;
  size_t const arity = solver->options->arity;
  size_t nodes = 0;
  size_t leaves = 0;
  bool const counted =
      ad_lift_full_tree_size(arity, shape->height, &nodes, &leaves);
  if (!counted || leaves > solver->options->max_host_leaves) {
    char message[sizeof(solver->error->message)];
    (void)snprintf(message, sizeof(message),
                   "the %s host, the full %zu-ary tree with %zu levels, has "
                   "more than %zu leaves",
                   player_name(player), arity, shape->height,
                   solver->options->max_host_leaves);
    return over_budget(solver, message);
  }
  ADLiftTree host = {0};
  ADLiftError build_error = {0};
  if (!ad_lift_tree_build_full(arity, shape->height, player, shape->root_level,
                               &host, &build_error)) {
    solve_error(solver->error, "failed to build the %s full %zu-ary host: %s",
                player_name(player), arity, build_error.message);
    return HOST_FAILED;
  }
  bool const ran = run_on_host(solver, &host);
  ad_lift_tree_destroy(&host);
  if (!ran) {
    return HOST_FAILED;
  }
  shape->arity = arity;
  return HOST_BUILT;
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
  bool const ran = run_on_host(solver, &host);
  ad_lift_tree_destroy(&host);
  return ran;
}

[[nodiscard]] static bool covered(Solver const *solver) {
  for (size_t vertex = pg_set_next(solver->domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(solver->domain, vertex + 1)) {
    if (!pg_set_contains(&solver->outcome[0].dominion, vertex) &&
        !pg_set_contains(&solver->outcome[1].dominion, vertex)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] static size_t smaller(size_t const left, size_t const right) {
  return left < right ? left : right;
}

/* Raise t until the dominions cover the domain, using the largest useful K
 * for each t, at most the optional cap. Beyond t_full the hosts only grow, so
 * t stops there; a player whose host does not change is not lifted again. If
 * a host exceeds the leaf limit after every player has run once, keep the
 * dominions found so far; they remain sound.
 *
 * When K changes the hosts need not embed in their successors (see
 * docs/incremental-t.md). Each dominion is nevertheless contained in its
 * player's winning region, which the final dominion equals when complete.
 * The first t reaching the largest dominion seen is therefore the least t
 * that already found the whole winning region. */
[[nodiscard]] static bool run_adaptive(Solver *solver) {
  ADLiftSolveStats *stats = solver->stats;
  size_t const cap = solver->options->k;
  bool ran[2] = {false, false};
  size_t best_count[2] = {0, 0};
  size_t best_t[2] = {0, 0};
  for (size_t t = smaller(solver->options->t, stats->t_full);; t++) {
    AD_TRACE(solver->options->trace, .kind = AD_TRACE_PHASE, .name = "t_raised",
             .to = t);
    for (size_t player = 0; player < 2; player++) {
      ADLiftPlayerStats const *shape = stats->player + player;
      size_t k = smaller(shape->height, t + 1);
      if (cap != 0) {
        k = smaller(k, cap);
      }
      if (ran[player] && shape->k == k && shape->t == t) {
        continue;
      }
      HostStatus const status = run_strahler(solver, (PGPlayer)player, k, t);
      if (status == HOST_FAILED ||
          (status == HOST_OVER_BUDGET && (!ran[0] || !ran[1]))) {
        return false;
      }
      if (status == HOST_OVER_BUDGET) {
        /* The limit message stays in error for the caller to report. */
        return true;
      }
      size_t const count = pg_set_count(&solver->outcome[player].dominion);
      if (!ran[player] || count > best_count[player]) {
        best_count[player] = count;
        best_t[player] = t;
      }
      ran[player] = true;
    }
    if (covered(solver)) {
      stats->adaptive_complete = true;
      stats->first_complete_t = t;
      stats->player[PG_EVEN].first_full_t = best_t[PG_EVEN];
      stats->player[PG_ODD].first_full_t = best_t[PG_ODD];
      return true;
    }
    if (solver->options->iter_shave || t >= stats->t_full) {
      return true;
    }
  }
}

/* Whether the hosts used are proven universal, so that the dominions must be
 * the winning regions. A decomposition of a region with at most n vertices has
 * at most n leaves, so no node has more than n children. */
[[nodiscard]] static bool universal_hosts(ADLiftSolveStats const *stats,
                                          ADLiftHostMode const mode) {
  for (size_t player = 0; player < 2; player++) {
    ADLiftPlayerStats const *shape = stats->player + player;
    bool const full = mode == AD_LIFT_HOST_KARY
                          ? shape->arity >= stats->vertices
                          : mode != AD_LIFT_HOST_TREE &&
                                shape->k == shape->k_full &&
                                shape->t == stats->t_full;
    if (!full) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] static bool solve_once(PGGame const *game, PGSet const *domain,
                                     PGSet const *host_domain,
                                     ADLiftOptions const *options,
                                     ADResult *result, ADLiftSolveStats *stats,
                                     ADLiftError *error) {
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
      (options->symmetric && options->mode == AD_LIFT_HOST_ADAPTIVE) ||
      options->schedule > AD_LIFT_SCHEDULE_SINGLE ||
      (options->incremental && options->mode != AD_LIFT_HOST_ADAPTIVE) ||
      (options->mode == AD_LIFT_HOST_STRAHLER && options->k == 0) ||
      (options->mode == AD_LIFT_HOST_KARY && options->arity == 0) ||
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

  size_t const vertices = pg_set_count(host_domain);
  stats->vertices = vertices;
  if (pg_set_empty(domain)) {
    return true;
  }
  uint64_t maximum = 0;
  for (size_t vertex = pg_set_next(host_domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(host_domain, vertex + 1)) {
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
    stats->t_full++;
  }
  for (size_t player = 0; player < 2; player++) {
    ADLiftPlayerStats *shape = stats->player + player;
    shape->root_level = maximum % 2 == player ? maximum : maximum + 1;
    shape->height = (size_t)((shape->root_level - player) / 2) + 1;
    shape->k_full = smaller(shape->height, stats->t_full + 1);
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
      size_t const k = options->mode == AD_LIFT_HOST_FULL
                           ? k_full
                           : smaller(options->k, k_full);
      succeeded = run_strahler(&solver, (PGPlayer)player, k, stats->t_full) ==
                  HOST_BUILT;
    }
    break;
  case AD_LIFT_HOST_ADAPTIVE:
    succeeded = run_adaptive(&solver);
    break;
  case AD_LIFT_HOST_KARY:
    for (size_t player = 0; succeeded && player < 2; player++) {
      succeeded = run_kary(&solver, (PGPlayer)player) == HOST_BUILT;
    }
    break;
  case AD_LIFT_HOST_TREE:
    for (size_t player = 0; succeeded && player < 2; player++) {
      succeeded = run_tree(&solver, (PGPlayer)player);
    }
    break;
  }

  if (succeeded && options->symmetric) {
    ad_result_destroy(result);
    succeeded =
        ad_sym_solve_trace(game, domain, solver.hosts, options->verify, result,
                           &stats->symmetric, error, options->trace);
    if (succeeded) {
      for (size_t player = 0; player < 2; player++) {
        stats->player[player].tree =
            ad_tree_metrics(result->decomposition[player]);
      }
      if (result->kind != AD_RESULT_COMPLETE &&
          universal_hosts(stats, options->mode)) {
        solve_error(error, "the universal hosts left vertices unresolved");
        succeeded = false;
      }
    }
  } else if (succeeded) {
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
      } else if (result->kind != AD_RESULT_COMPLETE &&
                 universal_hosts(stats, options->mode)) {
        solve_error(error, "the universal hosts left vertices unresolved");
        succeeded = false;
      }
    } else {
      solve_error(error, "failed to allocate the result sets");
    }
    pg_set_destroy(&overlap);
  }
  ad_lift_tree_destroy(solver.hosts + PG_EVEN);
  ad_lift_tree_destroy(solver.hosts + PG_ODD);
  outcome_destroy(solver.outcome + PG_EVEN);
  outcome_destroy(solver.outcome + PG_ODD);
  if (!succeeded) {
    ad_result_destroy(result);
  }
  return succeeded;
}

[[nodiscard]] static bool empty_residual(PGSet const *remaining,
                                         ADResult *result) {
  result->kind =
      pg_set_empty(remaining) ? AD_RESULT_COMPLETE : AD_RESULT_PARTIAL;
  return pg_set_init(&result->region[0], remaining->bit_count) &&
         pg_set_init(&result->region[1], remaining->bit_count) &&
         pg_set_clone(&result->unresolved, remaining);
}

[[nodiscard]] static bool
solve_with_options(PGGame const *game, PGSet const *domain,
                   ADLiftOptions const *options, ADResult *result,
                   ADLiftSolveStats *stats, ADLiftError *error) {
  if (error != nullptr) {
    *error = (ADLiftError){0};
  }
  if (options == nullptr || !options->iter_shave) {
    return solve_once(game, domain, domain, options, result, stats, error);
  }
  ADLiftSolveStats local_stats = {0};
  if (stats == nullptr) {
    stats = &local_stats;
  }
  *stats = (ADLiftSolveStats){0};
  if (result == nullptr || domain == nullptr) {
    solve_error(error, "invalid lifting solver input");
    return false;
  }
  *result = (ADResult){0};
  PGSet remaining = {0};
  ADResult history = {0};
  ADLiftOptions round_options = *options;
  size_t last_winning_t[2] = {0, 0};
  bool succeeded = false;
  if (!pg_set_clone(&remaining, domain)) {
    solve_error(error, "failed to allocate the remaining game");
    goto cleanup;
  }
  for (;;) {
    ADLiftSolveStats current = {0};
    if (!solve_once(game, &remaining, domain, &round_options, result, &current,
                    error)) {
      if (current.budget_exhausted && stats->iterations != 0 &&
          options->mode == AD_LIFT_HOST_ADAPTIVE) {
        stats->budget_exhausted = true;
        succeeded = empty_residual(&remaining, result);
      }
      goto cleanup;
    }
    AD_TRACE(options->trace, .kind = AD_TRACE_PHASE, .name = "iteration_done",
             .to = stats->iterations, .result = result);
    if (options->trace != nullptr && options->trace->failed) {
      goto cleanup;
    }
    current.iterations = stats->iterations + !pg_set_empty(&remaining);
    for (size_t player = 0; player < 2; player++) {
      current.player[player].total_lifts += stats->player[player].total_lifts;
      if (options->mode == AD_LIFT_HOST_ADAPTIVE) {
        if (stats->iterations == 0 || !pg_set_empty(&result->region[player])) {
          last_winning_t[player] = current.player[player].t;
        }
        current.player[player].first_full_t = last_winning_t[player];
      }
    }
    *stats = current;
    if (options->verify) {
      ADVerifyError verification = {0};
      if (!ad_result_verify_partial(game, &remaining, result, &verification)) {
        solve_error(error, "round verification failed: %s",
                    verification.message);
        goto cleanup;
      }
    }
    bool const progress =
        !pg_set_empty(&result->region[0]) || !pg_set_empty(&result->region[1]);
    if (result->kind == AD_RESULT_COMPLETE ||
        (!progress && options->mode != AD_LIFT_HOST_ADAPTIVE)) {
      succeeded = true;
      goto cleanup;
    }
    ADShaveRound *grown = realloc(
        history.shaves, (history.shave_count + 1) * sizeof(history.shaves[0]));
    if (grown == nullptr) {
      solve_error(error, "failed to record a shaving round");
      goto cleanup;
    }
    history.shaves = grown;
    ADShaveRound *shave = history.shaves + history.shave_count++;
    *shave = (ADShaveRound){
        .certified = *result,
        .t = options->mode == AD_LIFT_HOST_ADAPTIVE ? stats->player[0].t
                                                    : SIZE_MAX,
    };
    *result = (ADResult){0};
    for (size_t player = 0; player < 2; player++) {
      if (!pg_attractor(game, &remaining, &shave->certified.region[player],
                        (PGPlayer)player, &shave->attracted[player])) {
        solve_error(error, "failed to attract a certified dominion");
        goto cleanup;
      }
      AD_TRACE(options->trace, .kind = AD_TRACE_ATTRACTOR,
               .player = (PGPlayer)player, .name = "side",
               .target = &shave->certified.region[player],
               .vertices = &shave->attracted[player], .subgame = &remaining);
      if (options->trace != nullptr && options->trace->failed) {
        goto cleanup;
      }
    }
    /* Removing a dominion's attractor leaves a total subgame. Its winners
     * remain winners in the original game: exits into a removed opponent
     * dominion were already attracted, and exits into one's own are winning. */
    pg_set_subtract_into(&remaining, &shave->attracted[0]);
    pg_set_subtract_into(&remaining, &shave->attracted[1]);
    AD_TRACE(options->trace, .kind = AD_TRACE_PHASE, .name = "shave_applied",
             .to = history.shave_count - 1, .result = &shave->certified,
             .vertices = &remaining);
    if (options->trace != nullptr && options->trace->failed) {
      goto cleanup;
    }
    bool const adaptive = options->mode == AD_LIFT_HOST_ADAPTIVE;
    if (pg_set_empty(&remaining) ||
        (adaptive && stats->player[0].t >= stats->t_full)) {
      if (adaptive && pg_set_empty(&remaining)) {
        stats->adaptive_complete = true;
        stats->first_complete_t = stats->player[0].t;
      }
      succeeded = empty_residual(&remaining, result);
      goto cleanup;
    }
    if (adaptive) {
      round_options.t = stats->player[0].t + 1;
    }
  }
cleanup:
  pg_set_destroy(&remaining);
  if (succeeded) {
    for (size_t player = 0; player < 2; player++) {
      stats->player[player].tree =
          ad_tree_metrics(result->decomposition[player]);
    }
    result->shaves = history.shaves;
    result->shave_count = history.shave_count;
    history.shaves = nullptr;
    history.shave_count = 0;
  } else {
    solve_error(error, "failed to allocate iterative shaving results");
    ad_result_destroy(result);
  }
  ad_result_destroy(&history);
  return succeeded;
}

bool ad_lift_solve(PGGame const *game, PGSet const *domain,
                   ADLiftOptions const *options, ADResult *result,
                   ADLiftSolveStats *stats, ADLiftError *error) {
  ADTrace *trace = options == nullptr ? nullptr : options->trace;
  if (trace != nullptr) {
    trace->nesting++;
  }
  bool success =
      solve_with_options(game, domain, options, result, stats, error);
  if (trace != nullptr) {
    if (--trace->nesting == 0 && success) {
      AD_TRACE(trace, .kind = AD_TRACE_RESULT, .result = result);
    }
    if (success && trace->failed) {
      ad_result_destroy(result);
      solve_error(error, "trace observer stopped the run");
      success = false;
    }
  }
  return success;
}
