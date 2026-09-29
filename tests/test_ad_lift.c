#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_lift_solve.h"
#include "reference_solver.h"
#include "stree.h"
#include "zielonka.h"

[[nodiscard]] static PGGame parse(char const *text) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  assert(fwrite(text, 1, strlen(text), stream) == strlen(text));
  assert(fseek(stream, 0, SEEK_SET) == 0);
  PGGame game = {0};
  PGParseError error = {0};
  assert(pg_game_read(stream, &game, &error));
  assert(fclose(stream) == 0);
  return game;
}

[[nodiscard]] static PGSet full_domain(PGGame const *game) {
  PGSet domain = {0};
  assert(pg_set_init(&domain, game->vertex_count));
  pg_set_fill(&domain);
  return domain;
}

[[nodiscard]] static ADLiftOptions options_for(ADLiftHostMode const mode,
                                               size_t const k) {
  return (ADLiftOptions){
      .mode = mode,
      .k = k,
      .max_host_leaves = 1000000,
      .schedule = AD_LIFT_SCHEDULE_ROUNDS,
      .verify = true,
  };
}

/* Solve and check the result with the verifier matching its kind. */
[[nodiscard]] static ADResult solve(PGGame const *game, PGSet const *domain,
                                    ADLiftOptions const *options,
                                    ADLiftSolveStats *stats) {
  ADResult result = {0};
  ADLiftError error = {0};
  if (!ad_lift_solve(game, domain, options, &result, stats, &error)) {
    fprintf(stderr, "lifting failed: %s\n", error.message);
    assert(false);
  }
  ADVerifyError verify_error = {0};
  bool const verified =
      result.kind == AD_RESULT_COMPLETE
          ? ad_result_verify_complete(game, domain, &result, &verify_error)
          : ad_result_verify_partial(game, domain, &result, &verify_error);
  if (!verified) {
    fprintf(stderr, "result verification failed: %s\n", verify_error.message);
    assert(false);
  }
  for (size_t player = 0; player < 2; player++) {
    if (result.decomposition[player] != nullptr) {
      assert(ad_tree_relative_verify(game, &result.region[player],
                                     result.decomposition[player],
                                     &verify_error));
    }
  }
  return result;
}

static void expect_regions(ADResult const *result, size_t const even_count,
                           size_t const odd_count) {
  assert(result->kind == AD_RESULT_COMPLETE);
  assert(pg_set_count(&result->region[PG_EVEN]) == even_count);
  assert(pg_set_count(&result->region[PG_ODD]) == odd_count);
  assert(pg_set_empty(&result->unresolved));
}

static void test_tiny_games(void) {
  struct {
    char const *text;
    size_t even;
    size_t odd;
  } const cases[] = {
      {"parity 0;\n0 0 0 0 \"even\";\n", 1, 0},
      {"parity 0;\n0 1 1 0 \"odd\";\n", 0, 1},
      {"parity 1;\n0 0 0 0 \"even\";\n1 1 1 1 \"odd\";\n", 1, 1},
      {"parity 2;\n0 1 0 1,2,0;\n1 1 1 2,0,1;\n2 2 1 2,0,1;\n", 0, 3},
      {"parity 1;\n0 0 0 0 \"low\";\n1 5 1 1 \"high\";\n", 1, 1},
      {"parity 1000;\n1000 7 1 10;\n10 2 0 1000,10;\n", 2, 0},
  };
  for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
    PGGame game = parse(cases[index].text);
    PGSet domain = full_domain(&game);
    ADLiftOptions const options = options_for(AD_LIFT_HOST_FULL, 0);
    ADLiftSolveStats stats = {0};
    ADResult result = solve(&game, &domain, &options, &stats);
    expect_regions(&result, cases[index].even, cases[index].odd);
    for (size_t player = 0; player < 2; player++) {
      assert(stats.player[player].k == stats.player[player].k_full);
      assert(stats.player[player].root_level % 2 == player);
      assert(stats.player[player].root_level <= game.max_priority + 1);
    }
    ad_result_destroy(&result);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

static void test_ordered_two_children(void) {
  /* The least lifted decomposition matches the Zielonka one here: an Odd
   * root at 3 with an empty H, two children, and S_1 = {2}. */
  PGGame game = parse("parity 2;\n"
                      "0 1 0 1,2,0 \"bridge\";\n"
                      "1 1 1 2,0,1 \"first\";\n"
                      "2 2 1 2,0,1 \"high\";\n");
  PGSet domain = full_domain(&game);
  ADLiftOptions const options = options_for(AD_LIFT_HOST_FULL, 0);
  ADResult result = solve(&game, &domain, &options, nullptr);
  expect_regions(&result, 0, 3);
  ADNode const *odd = result.decomposition[PG_ODD];
  assert(odd != nullptr && odd->priority_bound == 3);
  assert(pg_set_empty(&odd->top_attractor));
  assert(odd->child_count == 2);
  assert(pg_set_count(&odd->children[0].trap) == 1);
  assert(pg_set_contains(&odd->children[0].trap, 1));
  assert(pg_set_count(&odd->children[0].attractor) == 2);
  assert(pg_set_contains(&odd->children[0].attractor, 2));
  assert(pg_set_contains(&odd->children[1].trap, 0));
  ADTreeMetrics const metrics = ad_tree_metrics(odd);
  assert(metrics.nodes == 3 && metrics.leaves == 2 && metrics.strahler == 2);
  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_top_attractor_and_pruning(void) {
  /* Even's root has H = {0} and T = {1}; the universal host has more nodes
   * than the single-node decomposition, so empty host branches are pruned. */
  PGGame game = parse("parity 1;\n0 2 0 0;\n1 0 0 0;\n");
  PGSet domain = full_domain(&game);
  ADLiftOptions const options = options_for(AD_LIFT_HOST_FULL, 0);
  ADLiftSolveStats stats = {0};
  ADResult result = solve(&game, &domain, &options, &stats);
  expect_regions(&result, 2, 0);
  ADNode const *even = result.decomposition[PG_EVEN];
  assert(even->priority_bound == 2 && even->child_count == 0);
  assert(pg_set_count(&even->top_attractor) == 2);
  ADTreeRelativeParts parts = {0};
  assert(ad_tree_relative_parts(&game, &result.region[PG_EVEN],
                                &result.region[PG_EVEN], even, &parts));
  assert(pg_set_count(&parts.highest) == 1 &&
         pg_set_contains(&parts.highest, 0));
  assert(pg_set_count(&parts.top) == 1 && pg_set_contains(&parts.top, 1));
  ad_tree_relative_parts_destroy(&parts);
  assert(stats.player[PG_EVEN].host_nodes > stats.player[PG_EVEN].tree.nodes);
  assert(stats.player[PG_EVEN].tree.nodes == 1);
  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_shape_differs_from_zielonka(void) {
  /* Both decompositions are valid, but the least labelling over the host
   * separates vertex 4 into its own child where Zielonka merges it. */
  PGGame game = parse("0 1 0 2;\n1 0 0 2;\n2 0 0 2;\n3 3 0 4;\n4 0 1 0,4;\n");
  PGSet domain = full_domain(&game);
  ADLiftOptions const options = options_for(AD_LIFT_HOST_FULL, 0);
  ADResult lifted = solve(&game, &domain, &options, nullptr);
  ADResult zielonka = {0};
  ZielonkaError error = {0};
  assert(
      zielonka_decompose(&game, &domain, game.max_priority, &zielonka, &error));
  assert(pg_set_equal(&lifted.region[PG_EVEN], &zielonka.region[PG_EVEN]));
  assert(ad_tree_metrics(lifted.decomposition[PG_EVEN]).nodes == 5);
  assert(ad_tree_metrics(zielonka.decomposition[PG_EVEN]).nodes == 4);
  assert(lifted.decomposition[PG_EVEN]->child_count == 2);
  ADChild const *second = lifted.decomposition[PG_EVEN]->children + 1;
  assert(pg_set_count(&second->trap) == 1 && pg_set_contains(&second->trap, 4));
  assert(pg_set_count(&second->attractor) == 2);
  ad_result_destroy(&lifted);
  ad_result_destroy(&zielonka);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_priority_bounds(void) {
  /* Raw priorities above the recursion bound are rejected; compacting them
   * first, as pg2adot does, makes the game solvable. */
  PGGame game = parse("0 9 0 1,2,0;\n1 9 1 2,0,1;\n2 1000000000000 1 2,0,1;\n");
  PGSet domain = full_domain(&game);
  ADLiftOptions const options = options_for(AD_LIFT_HOST_FULL, 0);
  ADResult result = {0};
  ADLiftError error = {0};
  assert(!ad_lift_solve(&game, &domain, &options, &result, nullptr, &error));
  assert(strstr(error.message, "recursion depth") != nullptr);

  PGPriorityMap map = {0};
  assert(pg_priority_map_build(&game, &map));
  assert(pg_priority_map_apply(&map, &game));
  result = solve(&game, &domain, &options, nullptr);
  expect_regions(&result, 0, 3);
  assert(result.decomposition[PG_ODD]->priority_bound == 3);
  assert(pg_priority_map_restore(&map, &game));
  pg_priority_map_destroy(&map);
  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static uint32_t random_state = UINT32_C(0x1f7a3c55);

[[nodiscard]] static uint32_t next_random(void) {
  random_state = random_state * UINT32_C(1103515245) + UINT32_C(12345);
  return random_state >> 8;
}

/* A random total game with self-loops, mixed owners, priority gaps, and up to
 * three successors per vertex. */
[[nodiscard]] static PGGame random_game(size_t const count,
                                        unsigned const priorities) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  for (size_t vertex = 0; vertex < count; vertex++) {
    /* Draw in a fixed order: argument evaluation order is unspecified, so
     * calling next_random() twice in one argument list makes the games
     * compiler dependent. */
    size_t const degree = 1 + next_random() % 3;
    unsigned const priority = next_random() % priorities;
    unsigned const owner = next_random() % 2;
    assert(fprintf(stream, "%zu %u %u ", vertex, priority, owner) > 0);
    for (size_t edge = 0; edge < degree; edge++) {
      assert(fprintf(stream, edge == 0 ? "%zu" : ",%zu",
                     (size_t)(next_random() % count)) > 0);
    }
    assert(fputs(";\n", stream) >= 0);
  }
  assert(fseek(stream, 0, SEEK_SET) == 0);
  PGGame game = {0};
  PGParseError error = {0};
  assert(pg_game_read(stream, &game, &error));
  assert(fclose(stream) == 0);
  return game;
}

static void test_random_reference_agreement(void) {
  for (size_t iteration = 0; iteration < 150; iteration++) {
    PGGame game = random_game(1 + next_random() % 6, 7);
    PGSet domain = full_domain(&game);
    ADLiftOptions const options = options_for(AD_LIFT_HOST_FULL, 0);
    ADResult result = solve(&game, &domain, &options, nullptr);
    PGSet expected_even = {0};
    PGSet expected_odd = {0};
    assert(reference_solve(&game, &expected_even, &expected_odd));
    assert(result.kind == AD_RESULT_COMPLETE);
    assert(pg_set_equal(&result.region[PG_EVEN], &expected_even));
    assert(pg_set_equal(&result.region[PG_ODD], &expected_odd));
    pg_set_destroy(&expected_even);
    pg_set_destroy(&expected_odd);
    ad_result_destroy(&result);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

/* Larger games are beyond the brute-force reference; compare them with the
 * independent Zielonka implementation instead. */
static void test_random_zielonka_agreement(void) {
  for (size_t iteration = 0; iteration < 60; iteration++) {
    PGGame game = random_game(7 + next_random() % 18, 9);
    PGSet domain = full_domain(&game);
    ADLiftOptions const options = options_for(AD_LIFT_HOST_FULL, 0);
    ADResult result = solve(&game, &domain, &options, nullptr);
    ADResult zielonka = {0};
    ZielonkaError error = {0};
    assert(zielonka_decompose(&game, &domain, game.max_priority, &zielonka,
                              &error));
    assert(pg_set_equal(&result.region[PG_EVEN], &zielonka.region[PG_EVEN]));
    assert(pg_set_equal(&result.region[PG_ODD], &zielonka.region[PG_ODD]));
    ad_result_destroy(&zielonka);
    ad_result_destroy(&result);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

static void test_restricted_soundness(void) {
  size_t partial = 0;
  for (size_t iteration = 0; iteration < 80; iteration++) {
    PGGame game = random_game(2 + next_random() % 5, 7);
    PGSet domain = full_domain(&game);
    PGSet winning[2] = {0};
    assert(reference_solve(&game, &winning[PG_EVEN], &winning[PG_ODD]));
    for (size_t k = 1;; k++) {
      ADLiftOptions const options = options_for(AD_LIFT_HOST_STRAHLER, k);
      ADLiftSolveStats stats = {0};
      ADResult result = solve(&game, &domain, &options, &stats);
      bool const universal =
          k >= stats.player[PG_EVEN].k_full && k >= stats.player[PG_ODD].k_full;
      for (size_t player = 0; player < 2; player++) {
        assert(pg_set_subset(&result.region[player], &winning[player]));
        if (universal) {
          assert(pg_set_equal(&result.region[player], &winning[player]));
        }
      }
      if (result.kind == AD_RESULT_PARTIAL) {
        partial++;
        assert(!pg_set_empty(&result.unresolved));
      }
      ad_result_destroy(&result);
      if (universal) {
        break;
      }
    }
    pg_set_destroy(&winning[PG_EVEN]);
    pg_set_destroy(&winning[PG_ODD]);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
  /* The seed exercises genuinely partial restricted results. */
  assert(partial > 0);
}

[[nodiscard]] static ADLiftTree strahler_host(int const k, int const t,
                                              PGPlayer const player,
                                              uint64_t const root_level) {
  char *stream = stree_leaf_stream(k, t, (int)(root_level / 2) + 1);
  assert(stream != nullptr);
  OrderedTreeNode *base = nullptr;
  OrderedTreeError parse_error = {0};
  assert(ordered_tree_parse_leaf_stream(stream, &base, &parse_error));
  free(stream);
  ADLiftTree host = {0};
  ADLiftError error = {0};
  assert(ad_lift_tree_build(base, player, root_level, &host, &error));
  ordered_tree_destroy(base);
  return host;
}

static void test_schedules_agree(void) {
  /* Round-based and single-vertex short lifts reach the same least labelling.
   * Both move by one compatible position per lift, so they also perform the
   * same number of lifts. */
  for (size_t iteration = 0; iteration < 40; iteration++) {
    PGGame game = random_game(2 + next_random() % 5, 6);
    PGSet domain = full_domain(&game);
    for (size_t player = 0; player < 2; player++) {
      ADLiftTree host = strahler_host(2, 2, (PGPlayer)player, 4 + player);
      ADLiftLabelling rounds = {0};
      ADLiftLabelling single = {0};
      ADLiftError error = {0};
      assert(ad_lift_run(&game, &domain, &host, AD_LIFT_SCHEDULE_ROUNDS,
                         &rounds, &error));
      assert(ad_lift_run(&game, &domain, &host, AD_LIFT_SCHEDULE_SINGLE,
                         &single, &error));
      assert(memcmp(rounds.labels, single.labels,
                    game.vertex_count * sizeof(rounds.labels[0])) == 0);
      assert(pg_set_equal(&rounds.dominion, &single.dominion));
      assert(rounds.stats.lifts == single.stats.lifts);
      assert(rounds.stats.rounds <= single.stats.rounds);
      assert(rounds.stats.top_count == single.stats.top_count);
      ADVerifyError verify_error = {0};
      assert(ad_lift_verify(&game, &domain, &host, &rounds, &verify_error));
      ad_lift_labelling_destroy(&rounds);
      ad_lift_labelling_destroy(&single);
      ad_lift_tree_destroy(&host);
    }
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

static void test_verifier_rejections(void) {
  /* In the Odd run on a one-node host, vertex 0 cannot stay in its regular
   * block because its only edge leads to Even's self-loop at vertex 1. */
  PGGame game = parse("0 1 1 1;\n1 0 0 1;\n");
  PGSet domain = full_domain(&game);
  ADLiftTree host = strahler_host(1, 0, PG_ODD, 1);
  ADLiftLabelling labelling = {0};
  ADLiftError error = {0};
  assert(ad_lift_run(&game, &domain, &host, AD_LIFT_SCHEDULE_ROUNDS, &labelling,
                     &error));
  assert(labelling.labels[0] == host.top && labelling.labels[1] == host.top);
  assert(pg_set_empty(&labelling.dominion));
  assert(labelling.stats.top_count == 2);
  assert(labelling.stats.max_position == SIZE_MAX);
  ADVerifyError verify_error = {0};
  assert(ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  ADNode *root = nullptr;
  assert(ad_lift_materialize(&game, &host, &labelling, &root, &error));
  assert(root == nullptr);

  /* Regular condition. */
  labelling.labels[0] = host.nodes[0].regular;
  pg_set_add(&labelling.dominion, 0);
  assert(!ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  assert(strstr(verify_error.message, "regular") != nullptr);
  /* Lazy condition. */
  labelling.labels[0] = host.top;
  pg_set_remove(&labelling.dominion, 0);
  labelling.labels[1] = host.nodes[0].minus_inf;
  pg_set_add(&labelling.dominion, 1);
  assert(!ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  assert(strstr(verify_error.message, "lazy") != nullptr);
  /* Priority compatibility. */
  labelling.labels[1] = host.nodes[0].regular;
  assert(!ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  assert(strstr(verify_error.message, "compatible") != nullptr);
  /* Dominion bookkeeping and ranges. */
  labelling.labels[1] = host.top;
  assert(!ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  assert(strstr(verify_error.message, "dominion") != nullptr);
  pg_set_remove(&labelling.dominion, 1);
  labelling.labels[1] = host.top + 1;
  assert(!ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  labelling.labels[1] = host.top;
  pg_set_remove(&domain, 1);
  assert(!ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  pg_set_add(&domain, 1);
  assert(ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  assert(!ad_lift_verify(nullptr, &domain, &host, &labelling, &verify_error));
  ad_lift_labelling_destroy(&labelling);
  ad_lift_tree_destroy(&host);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_materialize_rejects_empty_core(void) {
  /* A priority-0 vertex placed in the 1+ leaf of an empty child: valid
   * labellings never do this, and materialization refuses it. */
  PGGame game = parse("0 0 0 0;\n");
  PGSet domain = full_domain(&game);
  OrderedTreeNode *base = nullptr;
  OrderedTreeError parse_error = {0};
  assert(ordered_tree_parse_leaf_stream("0|", &base, &parse_error));
  ADLiftTree host = {0};
  ADLiftError error = {0};
  assert(ad_lift_tree_build(base, PG_EVEN, 2, &host, &error));
  ordered_tree_destroy(base);
  size_t labels[1] = {host.nodes[1].after};
  ADLiftLabelling labelling = {.labels = labels, .vertex_count = 1};
  assert(pg_set_init(&labelling.dominion, 1));
  pg_set_add(&labelling.dominion, 0);
  ADVerifyError verify_error = {0};
  assert(!ad_lift_verify(&game, &domain, &host, &labelling, &verify_error));
  ADNode *root = nullptr;
  assert(!ad_lift_materialize(&game, &host, &labelling, &root, &error));
  assert(root == nullptr && strstr(error.message, "empty child core"));
  assert(!ad_lift_materialize(&game, nullptr, &labelling, &root, &error));
  pg_set_destroy(&labelling.dominion);

  ADLiftLabelling unused = {0};
  assert(!ad_lift_run(&game, &domain, nullptr, AD_LIFT_SCHEDULE_ROUNDS, &unused,
                      &error));
  ad_lift_labelling_destroy(nullptr);
  ad_lift_tree_destroy(&host);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

/* Partial at K = 1: the Even dominion {0, 3, 4} is certified, but Even's win
 * from {1, 2} needs a Strahler-2 decomposition. */
static char const *const partial_game = "0 2 0 3;\n1 2 1 2;\n2 0 1 1,4;\n"
                                        "3 0 0 3;\n4 3 1 3;\n";

static void test_partial_and_adaptive(void) {
  PGGame game = parse(partial_game);
  PGSet domain = full_domain(&game);
  ADLiftOptions options = options_for(AD_LIFT_HOST_STRAHLER, 1);
  ADResult result = solve(&game, &domain, &options, nullptr);
  assert(result.kind == AD_RESULT_PARTIAL);
  assert(pg_set_count(&result.region[PG_EVEN]) == 3);
  assert(pg_set_empty(&result.region[PG_ODD]));
  assert(result.decomposition[PG_ODD] == nullptr);
  assert(pg_set_count(&result.unresolved) == 2);
  ADVerifyError verify_error = {0};
  assert(!ad_result_verify_complete(&game, &domain, &result, &verify_error));

  /* The partial verifier checks the dominion structure. */
  pg_set_add(&result.unresolved, 0);
  assert(!ad_result_verify_partial(&game, &domain, &result, &verify_error));
  pg_set_remove(&result.unresolved, 0);
  PGSet saved = {0};
  assert(pg_set_clone(&saved, &result.region[PG_EVEN]));
  pg_set_remove(&result.region[PG_EVEN], 4);
  pg_set_add(&result.unresolved, 4);
  assert(!ad_result_verify_partial(&game, &domain, &result, &verify_error));
  pg_set_move(&result.region[PG_EVEN], &saved);
  pg_set_remove(&result.unresolved, 4);
  assert(ad_result_verify_partial(&game, &domain, &result, &verify_error));
  ad_result_destroy(&result);

  options = options_for(AD_LIFT_HOST_ADAPTIVE, 1);
  ADLiftSolveStats stats = {0};
  result = solve(&game, &domain, &options, &stats);
  expect_regions(&result, 5, 0);
  assert(stats.first_complete_k == 2);
  assert(stats.player[PG_EVEN].first_full_k == 2);
  assert(stats.player[PG_ODD].first_full_k == 1);
  assert(!stats.budget_exhausted);
  ad_result_destroy(&result);

  /* A start above k_full is clamped to the universal hosts. */
  options.k = 9;
  result = solve(&game, &domain, &options, &stats);
  assert(stats.first_complete_k == 3);
  assert(stats.player[PG_EVEN].k == stats.player[PG_EVEN].k_full);
  ad_result_destroy(&result);

  /* The leaf limit stops the adaptive loop after K = 1 with a sound partial
   * result, but it is an error for the universal host. */
  options = options_for(AD_LIFT_HOST_ADAPTIVE, 1);
  options.max_host_leaves = 1;
  ADLiftError error = {0};
  assert(ad_lift_solve(&game, &domain, &options, &result, &stats, &error));
  assert(stats.budget_exhausted && error.message[0] != '\0');
  assert(result.kind == AD_RESULT_PARTIAL);
  assert(ad_result_verify_partial(&game, &domain, &result, &verify_error));
  ad_result_destroy(&result);
  options.max_host_leaves = 0;
  assert(!ad_lift_solve(&game, &domain, &options, &result, &stats, &error));
  options = options_for(AD_LIFT_HOST_FULL, 0);
  options.max_host_leaves = 5;
  assert(!ad_lift_solve(&game, &domain, &options, &result, &stats, &error));
  assert(stats.budget_exhausted && strstr(error.message, "limit") != nullptr);

  /* The single-vertex schedule gives the same restricted result. */
  options = options_for(AD_LIFT_HOST_STRAHLER, 1);
  options.schedule = AD_LIFT_SCHEDULE_SINGLE;
  result = solve(&game, &domain, &options, nullptr);
  assert(pg_set_count(&result.region[PG_EVEN]) == 3);
  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_tree_hosts(void) {
  PGGame game = parse(partial_game);
  PGSet domain = full_domain(&game);
  OrderedTreeNode *chain = nullptr;
  OrderedTreeError parse_error = {0};
  assert(ordered_tree_parse_leaf_stream("e|", &chain, &parse_error));
  ADLiftOptions options = options_for(AD_LIFT_HOST_TREE, 0);
  options.tree = chain;
  ADLiftSolveStats stats = {0};
  ADResult result = solve(&game, &domain, &options, &stats);
  assert(stats.player[PG_EVEN].k == 0 && stats.player[PG_EVEN].host_nodes == 2);
  assert(pg_set_subset(&result.region[PG_EVEN], &domain));
  ad_result_destroy(&result);

  /* Too deep for Odd's root level 3. */
  OrderedTreeNode *deep = nullptr;
  assert(ordered_tree_parse_leaf_stream("e,e|", &deep, &parse_error));
  options.tree = deep;
  ADLiftError error = {0};
  assert(!ad_lift_solve(&game, &domain, &options, &result, &stats, &error));
  assert(strstr(error.message, "allows at most 1") != nullptr);
  ordered_tree_destroy(deep);
  ordered_tree_destroy(chain);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_solver_inputs(void) {
  PGGame game = parse("0 0 0 0;\n1 1 1 0;\n");
  PGSet domain = full_domain(&game);
  ADLiftOptions options = options_for(AD_LIFT_HOST_STRAHLER, 0);
  ADResult result = {0};
  ADLiftError error = {0};
  assert(!ad_lift_solve(&game, &domain, &options, &result, nullptr, &error));
  options = options_for(AD_LIFT_HOST_TREE, 0);
  assert(!ad_lift_solve(&game, &domain, &options, &result, nullptr, &error));
  options = options_for(AD_LIFT_HOST_FULL, 0);
  assert(!ad_lift_solve(&game, &domain, nullptr, &result, nullptr, &error));
  assert(!ad_lift_solve(nullptr, &domain, &options, &result, nullptr, &error));
  /* Vertex 1 only moves to 0, so {1} is not total. */
  pg_set_remove(&domain, 0);
  assert(!ad_lift_solve(&game, &domain, &options, &result, nullptr, &error));

  /* Restricting the domain to the total subgame {0} works; the empty domain
   * is trivially complete. */
  pg_set_clear(&domain);
  pg_set_add(&domain, 0);
  result = solve(&game, &domain, &options, nullptr);
  expect_regions(&result, 1, 0);
  ad_result_destroy(&result);
  pg_set_clear(&domain);
  ADLiftSolveStats stats = {0};
  result = solve(&game, &domain, &options, &stats);
  expect_regions(&result, 0, 0);
  assert(result.decomposition[PG_EVEN] == nullptr && stats.vertices == 0);
  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

int main(void) {
  test_tiny_games();
  test_ordered_two_children();
  test_top_attractor_and_pruning();
  test_shape_differs_from_zielonka();
  test_priority_bounds();
  test_random_reference_agreement();
  test_random_zielonka_agreement();
  test_restricted_soundness();
  test_schedules_agree();
  test_verifier_rejections();
  test_materialize_rejects_empty_core();
  test_partial_and_adaptive();
  test_tree_hosts();
  test_solver_inputs();
  return 0;
}
