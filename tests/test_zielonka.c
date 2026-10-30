#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "reference_solver.h"
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

[[nodiscard]] static ADResult solve(PGGame const *game, PGSet *domain) {
  assert(pg_set_init(domain, game->vertex_count));
  pg_set_fill(domain);
  ADResult result = {0};
  ZielonkaError error = {0};
  if (!zielonka_decompose(game, domain, game->max_priority, &result, &error)) {
    fprintf(stderr, "solver failed: %s\n", error.message);
    assert(false);
  }
  ADVerifyError verify_error = {0};
  if (!ad_result_verify_complete(game, domain, &result, &verify_error)) {
    fprintf(stderr, "verification failed: %s\n", verify_error.message);
    assert(false);
  }
  for (size_t player = 0; player < 2; player++) {
    if (result.decomposition[player] != nullptr &&
        !ad_tree_relative_verify(game, &result.region[player],
                                 result.decomposition[player], &verify_error)) {
      fprintf(stderr, "tree-relative verification failed: %s\n",
              verify_error.message);
      assert(false);
    }
  }
  return result;
}

static void test_basic_games(void) {
  char const *games[] = {
      "0 0 0 0 \"even\";\n",
      "0 1 1 0 \"odd\";\n",
      "0 0 0 0 \"even\";\n1 1 1 1 \"odd\";\n",
  };
  size_t const expected_even[] = {1, 0, 1};
  size_t const expected_odd[] = {0, 1, 1};
  for (size_t index = 0; index < 3; index++) {
    PGGame game = parse(games[index]);
    PGSet domain = {0};
    ADResult result = solve(&game, &domain);
    assert(pg_set_count(&result.region[PG_EVEN]) == expected_even[index]);
    assert(pg_set_count(&result.region[PG_ODD]) == expected_odd[index]);
    for (size_t player = 0; player < 2; player++) {
      if (result.decomposition[player] != nullptr) {
        ADTreeMetrics const metrics =
            ad_tree_metrics(result.decomposition[player]);
        assert(metrics.nodes >= 1 && metrics.leaves >= 1 &&
               metrics.height >= 1 && metrics.strahler >= 1);
      }
    }
    ad_result_destroy(&result);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

static void test_ordered_two_children(void) {
  PGGame game = parse("parity 2;\n"
                      "0 1 0 1,2,0 \"bridge\";\n"
                      "1 1 1 2,0,1 \"first\";\n"
                      "2 2 1 2,0,1 \"high\";\n");
  PGSet domain = {0};
  ADResult result = solve(&game, &domain);
  assert(pg_set_empty(&result.region[PG_EVEN]));
  assert(pg_set_count(&result.region[PG_ODD]) == 3);
  ADNode const *odd = result.decomposition[PG_ODD];
  assert(odd != nullptr && odd->priority_bound == 3);
  assert(pg_set_empty(&odd->top_attractor));
  assert(odd->child_count == 2);
  assert(pg_set_count(&odd->children[0].trap) == 1);
  assert(pg_set_contains(&odd->children[0].trap, 1));
  assert(pg_set_count(&odd->children[0].attractor) == 2);
  assert(pg_set_contains(&odd->children[0].attractor, 1));
  assert(pg_set_contains(&odd->children[0].attractor, 2));
  assert(pg_set_count(&odd->children[1].trap) == 1);
  assert(pg_set_contains(&odd->children[1].trap, 0));
  assert(pg_set_equal(&odd->children[1].trap, &odd->children[1].attractor));

  ADTreeRelativeParts root_parts = {0};
  assert(ad_tree_relative_parts(&game, &result.region[PG_ODD],
                                &result.region[PG_ODD], odd, &root_parts));
  assert(pg_set_empty(&root_parts.highest));
  assert(pg_set_empty(&root_parts.top));
  assert(pg_set_empty(&root_parts.side));
  ad_tree_relative_parts_destroy(&root_parts);

  ADTreeRelativeParts child_parts = {0};
  assert(ad_tree_relative_parts(&game, &odd->children[0].attractor,
                                &odd->children[0].trap,
                                odd->children[0].subtree, &child_parts));
  assert(pg_set_count(&child_parts.highest) == 1);
  assert(pg_set_contains(&child_parts.highest, 1));
  assert(pg_set_empty(&child_parts.top));
  assert(pg_set_count(&child_parts.side) == 1);
  assert(pg_set_contains(&child_parts.side, 2));
  ad_tree_relative_parts_destroy(&child_parts);
  ADTreeMetrics const metrics = ad_tree_metrics(odd);
  assert(metrics.nodes == 3);
  assert(metrics.leaves == 2);
  assert(metrics.height == 2);
  assert(metrics.strahler == 2);

  pg_set_add(&odd->children[0].subtree->top_attractor, 0);
  ADVerifyError verify_error = {0};
  assert(!ad_result_verify_complete(&game, &domain, &result, &verify_error));
  assert(!ad_tree_relative_verify(&game, &result.region[PG_ODD], odd,
                                  &verify_error));
  pg_set_remove(&odd->children[0].subtree->top_attractor, 0);

  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_literal_priority_gap(void) {
  PGGame game = parse("0 0 0 0;\n1 5 1 1;\n");
  PGSet domain = {0};
  ADResult result = solve(&game, &domain);
  assert(pg_set_count(&result.region[PG_EVEN]) == 1);
  assert(pg_set_count(&result.region[PG_ODD]) == 1);
  assert(result.decomposition[PG_EVEN]->priority_bound == 6);
  ADTreeMetrics const metrics = ad_tree_metrics(result.decomposition[PG_EVEN]);
  assert(metrics.nodes == 4);
  assert(metrics.height == 4);

  ADResult rejected = {0};
  ZielonkaError error = {0};
  assert(!zielonka_decompose(&game, &domain, 1025, &rejected, &error));
  assert(error.message[0] != '\0');

  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_tree_relative_top_partition(void) {
  PGGame game = parse("parity 1;\n"
                      "0 2 0 0 \"high\";\n"
                      "1 0 0 0 \"top attractor\";\n");
  PGSet domain = {0};
  ADResult result = solve(&game, &domain);
  ADNode const *even = result.decomposition[PG_EVEN];
  assert(even != nullptr);

  ADTreeRelativeParts parts = {0};
  assert(ad_tree_relative_parts(&game, &result.region[PG_EVEN],
                                &result.region[PG_EVEN], even, &parts));
  assert(pg_set_count(&parts.highest) == 1);
  assert(pg_set_contains(&parts.highest, 0));
  assert(pg_set_count(&parts.top) == 1);
  assert(pg_set_contains(&parts.top, 1));
  assert(pg_set_empty(&parts.side));
  ad_tree_relative_parts_destroy(&parts);

  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static uint32_t random_state = UINT32_C(0xc0ffee42);

[[nodiscard]] static uint32_t next_random(void) {
  random_state = random_state * UINT32_C(1103515245) + UINT32_C(12345);
  return random_state;
}

static void test_random_reference_agreement(void) {
  for (size_t iteration = 0; iteration < 60; iteration++) {
    size_t const count = 2 + next_random() % 5;
    FILE *stream = tmpfile();
    assert(stream != nullptr);
    for (size_t vertex = 0; vertex < count; vertex++) {
      size_t const first = next_random() % count;
      size_t const second = next_random() % count;
      assert(fprintf(stream, "%zu %u %u %zu,%zu;\n", vertex, next_random() % 6,
                     next_random() % 2, first, second) > 0);
    }
    assert(fseek(stream, 0, SEEK_SET) == 0);
    PGGame game = {0};
    PGParseError parse_error = {0};
    assert(pg_game_read(stream, &game, &parse_error));
    assert(fclose(stream) == 0);

    PGSet domain = {0};
    ADResult result = solve(&game, &domain);
    PGSet expected_even = {0};
    PGSet expected_odd = {0};
    if (!reference_solve(&game, &expected_even, &expected_odd)) {
      FILE *diagnostic = stderr;
      (void)pg_game_write_pgsolver(diagnostic, &game, false);
      assert(false);
    }
    assert(pg_set_equal(&result.region[PG_EVEN], &expected_even));
    assert(pg_set_equal(&result.region[PG_ODD], &expected_odd));

    pg_set_destroy(&expected_even);
    pg_set_destroy(&expected_odd);
    ad_result_destroy(&result);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

static void test_solver_errors(void) {
  PGGame game = parse("0 2 0 1;1 0 1 1;");
  PGSet domain = {0};
  assert(pg_set_init(&domain, game.vertex_count));
  ADResult result = {0};
  ZielonkaError error = {0};
  ZielonkaStats stats = {.calls = 99, .attractors = 99};
  assert(zielonka_decompose_stats(&game, &domain, 2, &result, &stats, &error));
  assert(stats.calls == 0 && stats.attractors == 0);
  assert(pg_set_empty(&result.region[0]) && pg_set_empty(&result.region[1]));
  ad_result_destroy(&result);
  pg_set_fill(&domain);
  assert(!zielonka_decompose(nullptr, &domain, 2, &result, &error));
  assert(strstr(error.message, "invalid solver input") != nullptr);
  assert(!zielonka_decompose(&game, nullptr, 2, &result, &error));
  assert(!zielonka_decompose(&game, &domain, 2, nullptr, &error));
  PGSet wrong_size = {0};
  assert(!zielonka_decompose(&game, &wrong_size, 2, &result, &error));
  assert(!zielonka_decompose(&game, &domain, UINT64_MAX, &result, &error));
  assert(!zielonka_decompose(&game, &domain, 1025, &result, nullptr));
  assert(!zielonka_decompose(&game, &domain, 1, &result, &error));
  assert(strstr(error.message, "bound or totality") != nullptr);
  assert(result.region[0].words == nullptr &&
         result.region[1].words == nullptr);
  pg_set_remove(&domain, 1);
  assert(!zielonka_decompose(&game, &domain, 2, &result, &error));
  assert(strstr(error.message, "bound or totality") != nullptr);
  ad_result_destroy(&result);
  pg_set_fill(&domain);
  assert(zielonka_decompose(&game, &domain, 2, &result, &error));
  ADNode *root = result.decomposition[PG_EVEN];
  assert(root != nullptr && root->child_count != 0);
  ADVerifyError verification = {0};
  root->priority_bound = 0;
  assert(!ad_tree_verify(&game, &domain, root, &verification));
  assert(strstr(verification.message, "exceeds the node bound") != nullptr);
  assert(!ad_tree_relative_verify(&game, &domain, root, &verification));
  root->priority_bound = 2;
  root->player = PG_ODD;
  assert(!ad_tree_verify(&game, &domain, root, &verification));
  assert(strstr(verification.message, "metadata") != nullptr);
  root->player = PG_EVEN;
  size_t const children = root->child_count;
  root->child_count = 0;
  assert(!ad_tree_verify(&game, &domain, root, &verification));
  assert(strstr(verification.message,
                "child attractors do not cover the subgame Gᵢ") != nullptr);
  root->child_count = children;
  result.decomposition[PG_EVEN] = nullptr;
  assert(!ad_result_verify_complete(&game, &domain, &result, &verification));
  assert(strstr(verification.message, "region/decomposition mismatch") !=
         nullptr);
  result.decomposition[PG_EVEN] = root;
  assert(ad_result_verify_complete(&game, &domain, &result, &verification));
  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

int main(void) {
  test_basic_games();
  test_ordered_two_children();
  test_literal_priority_gap();
  test_tree_relative_top_partition();
  test_random_reference_agreement();
  test_solver_errors();
  return 0;
}
