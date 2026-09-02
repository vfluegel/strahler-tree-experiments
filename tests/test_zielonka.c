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

[[nodiscard]] static ZielonkaResult solve(PGGame const *game, PGSet *domain) {
  assert(pg_set_init(domain, game->vertex_count));
  pg_set_fill(domain);
  ZielonkaResult result = {0};
  ZielonkaError error = {0};
  if (!zielonka_decompose(game, domain, game->max_priority, &result, &error)) {
    fprintf(stderr, "solver failed: %s\n", error.message);
    assert(false);
  }
  ADVerifyError verify_error = {0};
  if (!zielonka_result_verify(game, domain, &result, &verify_error)) {
    fprintf(stderr, "verification failed: %s\n", verify_error.message);
    assert(false);
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
    ZielonkaResult result = solve(&game, &domain);
    assert(pg_set_count(&result.winning[PG_EVEN]) == expected_even[index]);
    assert(pg_set_count(&result.winning[PG_ODD]) == expected_odd[index]);
    for (size_t player = 0; player < 2; player++) {
      if (result.decomposition[player] != nullptr) {
        ADTreeMetrics const metrics =
            ad_tree_metrics(result.decomposition[player]);
        assert(metrics.nodes >= 1 && metrics.leaves >= 1 &&
               metrics.height >= 1 && metrics.strahler >= 1);
      }
    }
    zielonka_result_destroy(&result);
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
  ZielonkaResult result = solve(&game, &domain);
  assert(pg_set_empty(&result.winning[PG_EVEN]));
  assert(pg_set_count(&result.winning[PG_ODD]) == 3);
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
  ADTreeMetrics const metrics = ad_tree_metrics(odd);
  assert(metrics.nodes == 3);
  assert(metrics.leaves == 2);
  assert(metrics.height == 2);
  assert(metrics.strahler == 2);

  pg_set_add(&odd->children[0].subtree->top_attractor, 0);
  ADVerifyError verify_error = {0};
  assert(!zielonka_result_verify(&game, &domain, &result, &verify_error));
  pg_set_remove(&odd->children[0].subtree->top_attractor, 0);

  zielonka_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void test_literal_priority_gap(void) {
  PGGame game = parse("0 0 0 0;\n1 5 1 1;\n");
  PGSet domain = {0};
  ZielonkaResult result = solve(&game, &domain);
  assert(pg_set_count(&result.winning[PG_EVEN]) == 1);
  assert(pg_set_count(&result.winning[PG_ODD]) == 1);
  assert(result.decomposition[PG_EVEN]->priority_bound == 6);
  ADTreeMetrics const metrics = ad_tree_metrics(result.decomposition[PG_EVEN]);
  assert(metrics.nodes == 4);
  assert(metrics.height == 4);

  ZielonkaResult rejected = {0};
  ZielonkaError error = {0};
  assert(!zielonka_decompose(&game, &domain, 1025, &rejected, &error));
  assert(error.message[0] != '\0');

  zielonka_result_destroy(&result);
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
    ZielonkaResult result = solve(&game, &domain);
    PGSet expected_even = {0};
    PGSet expected_odd = {0};
    if (!reference_solve(&game, &expected_even, &expected_odd)) {
      FILE *diagnostic = stderr;
      (void)pg_game_write_pgsolver(diagnostic, &game, false);
      assert(false);
    }
    assert(pg_set_equal(&result.winning[PG_EVEN], &expected_even));
    assert(pg_set_equal(&result.winning[PG_ODD], &expected_odd));

    pg_set_destroy(&expected_even);
    pg_set_destroy(&expected_odd);
    zielonka_result_destroy(&result);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

int main(void) {
  test_basic_games();
  test_ordered_two_children();
  test_literal_priority_gap();
  test_random_reference_agreement();
  return 0;
}
