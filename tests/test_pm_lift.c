#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pg_preprocess.h"
#include "pm_internal.h"
#include "zielonka.h"

static PGGame read_game(FILE *stream) {
  PGGame game = {0};
  PGParseError error = {0};
  assert(pg_game_read(stream, &game, &error));
  return game;
}

static PGGame parse(char const *text) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  assert(fputs(text, stream) >= 0);
  assert(fseek(stream, 0, SEEK_SET) == 0);
  PGGame game = read_game(stream);
  assert(fclose(stream) == 0);
  return game;
}

typedef struct {
  PGSet won[2];
  PGSet unresolved;
  size_t calls;
  PMParameters parameters;
} Collected;

static bool collect(PMResult const result[static 2], PGSet const *unresolved,
                    size_t const round, void *context) {
  Collected *collected = context;
  assert(round >= 1);
  for (size_t player = 0; player < 2; player++) {
    for (size_t vertex = pg_set_next(&result[player].region, 0);
         vertex != SIZE_MAX;
         vertex = pg_set_next(&result[player].region, vertex + 1)) {
      assert(!pg_set_contains(&collected->won[0], vertex));
      assert(!pg_set_contains(&collected->won[1], vertex));
    }
    pg_set_union_into(&collected->won[player], &result[player].region);
    pg_set_union_into(&collected->won[player], &result[player].attracted);
  }
  assert(pg_set_clone(&collected->unresolved, unresolved));
  collected->calls++;
  collected->parameters = result[0].parameters;
  return true;
}

static Collected solve(PGGame const *game, PGSet const *active,
                       PMOptions const *options) {
  Collected result = {0};
  assert(pg_set_init(&result.won[0], game->vertex_count));
  assert(pg_set_init(&result.won[1], game->vertex_count));
  PMError error = {0};
  if (!pm_solve(game, active, options, collect, &result, &error)) {
    fprintf(stderr, "pm_solve: %s (algorithm=%d incremental=%d)\n",
            error.message, options->algorithm, options->incremental);
    assert(pg_game_write_pgsolver(stderr, game, false));
    assert(false);
  }
  return result;
}

static void destroy(Collected *result) {
  pg_set_destroy(&result->won[0]);
  pg_set_destroy(&result->won[1]);
  pg_set_destroy(&result->unresolved);
}

static void check_game(PGGame *game, PGPreprocessOptions const *pre_options) {
  PGPreprocessResult preprocessed = {0};
  assert(pg_preprocess(game, pre_options, &preprocessed));
  assert(pg_preprocess_verify(game, &preprocessed));
  ADResult expected = {0};
  ZielonkaError error = {0};
  assert(zielonka_decompose(game, &preprocessed.active, game->max_priority,
                            &expected, &error));
  for (int algorithm = PM_STRAHLER; algorithm <= PM_SUCCINCT; algorithm++) {
    for (int incremental = 0; incremental <= 1; incremental++) {
      PMOptions options = {.algorithm = (PMAlgorithm)algorithm,
                           .incremental = incremental != 0,
                           .verify = true};
      Collected result = solve(game, &preprocessed.active, &options);
      assert(result.calls >= 1);
      assert(pg_set_subset(&result.won[0], &expected.region[0]));
      assert(pg_set_subset(&result.won[1], &expected.region[1]));
      assert(pg_set_count(&result.won[0]) + pg_set_count(&result.won[1]) +
                 pg_set_count(&result.unresolved) ==
             pg_set_count(&preprocessed.active));
      if (!incremental || pg_set_empty(&result.unresolved)) {
        assert(pg_set_empty(&result.unresolved));
        assert(pg_set_equal(&result.won[0], &expected.region[0]));
        assert(pg_set_equal(&result.won[1], &expected.region[1]));
      }
      destroy(&result);
    }
    PMOptions const fixed = {.algorithm = (PMAlgorithm)algorithm,
                             .fixed = true,
                             .parameters = {.k = 1, .t = 0, .bits = 0},
                             .verify = true};
    Collected result = solve(game, &preprocessed.active, &fixed);
    assert(result.calls == 1);
    assert(pg_set_subset(&result.won[0], &expected.region[0]));
    assert(pg_set_subset(&result.won[1], &expected.region[1]));
    assert(pg_set_count(&result.won[0]) + pg_set_count(&result.won[1]) +
               pg_set_count(&result.unresolved) ==
           pg_set_count(&preprocessed.active));
    destroy(&result);
  }
  ad_result_destroy(&expected);
  pg_preprocess_destroy(&preprocessed);
}

static void test_fixtures(void) {
  char const *const files[] = {"even_loop",
                               "odd_loop",
                               "split_winners",
                               "ordered_two_children",
                               "strahler_two",
                               "priority_gap_start",
                               "lifted_shape",
                               "shave_single",
                               "shave_loops",
                               "shave_wcwc",
                               "shave_partial",
                               "compress",
                               "sparse",
                               "disagree_lifting_leaves",
                               "disagree_lifting_strahler",
                               "disagree_odd_strahler",
                               "disagree_shape",
                               "disagree_zielonka_height"};
  for (size_t index = 0; index < sizeof(files) / sizeof(files[0]); index++) {
    for (unsigned mode = 0; mode < 4; mode++) {
      char path[128];
      assert(snprintf(path, sizeof(path), "tests/games/%s.pg", files[index]) >
             0);
      FILE *stream = fopen(path, "rb");
      assert(stream != nullptr);
      PGGame game = read_game(stream);
      assert(fclose(stream) == 0);
      PGPreprocessOptions const options = {.shave = mode % 2 ? PG_SHAVE_ALL : 0,
                                           .compress = mode >= 2};
      check_game(&game, &options);
      pg_game_destroy(&game);
    }
  }
}

static uint32_t random_state = UINT32_C(0x1f7a3c55);

static uint32_t next_random(void) {
  random_state = random_state * UINT32_C(1103515245) + UINT32_C(12345);
  return random_state >> 8;
}

static void test_generated(void) {
  for (size_t sample = 0; sample < 200; sample++) {
    size_t const count = 1 + next_random() % 24;
    FILE *stream = tmpfile();
    assert(stream != nullptr);
    for (size_t vertex = 0; vertex < count; vertex++) {
      unsigned const priority = next_random() % 9;
      unsigned const owner = next_random() % 2;
      size_t const degree = 1 + next_random() % 3;
      assert(fprintf(stream, "%zu %u %u ", vertex, priority, owner) > 0);
      for (size_t edge = 0; edge < degree; edge++) {
        size_t const to = next_random() % count;
        assert(fprintf(stream, "%s%zu", edge == 0 ? "" : ",", to) > 0);
      }
      assert(fputs(";\n", stream) >= 0);
    }
    assert(fseek(stream, 0, SEEK_SET) == 0);
    PGGame game = read_game(stream);
    assert(fclose(stream) == 0);
    PGPreprocessOptions const options = {.shave = sample % 2 ? PG_SHAVE_ALL : 0,
                                         .compress = sample % 3 == 0};
    check_game(&game, &options);
    pg_game_destroy(&game);
  }
}

static void test_successors(void) {
  for (int algorithm = PM_STRAHLER; algorithm <= PM_SUCCINCT; algorithm++) {
    for (int depth = 1; depth <= 4; depth++) {
      for (int budget = 0; budget <= 3; budget++) {
        unsigned char bits[8] = {0};
        unsigned char previous_bits[8] = {0};
        int levels[8] = {0};
        int previous_levels[8] = {0};
        int const k = depth < 3 ? depth + 1 : 3;
        PMMeasure measure = {.length = algorithm == PM_STRAHLER ? k - 1 + budget
                                                                : budget,
                             .bits = bits,
                             .levels = levels};
        if (algorithm == PM_STRAHLER) {
          for (int i = budget + 1; i < measure.length; i++) {
            levels[i] = i - budget;
          }
        }
        for (size_t step = 0; !measure.top; step++) {
          assert(step < 10000);
          PMMeasure const previous = {.length = measure.length,
                                      .bits = previous_bits,
                                      .levels = previous_levels};
          memcpy(previous_bits, bits, sizeof(bits));
          memcpy(previous_levels, levels, sizeof(levels));
          if (algorithm == PM_STRAHLER) {
            strpm_successor(&measure, depth - 1, depth, k, budget);
          } else {
            sspm_successor(&measure, depth - 1, depth);
          }
          assert(pm_compare(&measure, &previous, depth - 1) > 0);
          assert(measure.length >= 0 && measure.length <= (int)sizeof(bits));
        }
      }
    }
  }
  unsigned char bits[] = {1, 0, 1};
  int levels[] = {0, 1, 2};
  PMMeasure measure = {.length = 3, .bits = bits, .levels = levels};
  sspm_truncate(&measure, 0);
  assert(bits[0] == 1 && bits[1] == 0 && bits[2] == 0);
  assert(levels[0] == 0 && levels[1] == 1 && levels[2] == 1);
}

static void test_partial_and_rejection(void) {
  PGGame game = parse("0 2 0 3;\n1 2 1 2;\n2 0 1 1,4;\n3 0 0 3;\n4 3 1 3;\n");
  PGPreprocessOptions const pre_options = {0};
  PGPreprocessResult preprocessed = {0};
  assert(pg_preprocess(&game, &pre_options, &preprocessed));
  PMOptions options = {.algorithm = PM_STRAHLER,
                       .fixed = true,
                       .parameters = {.k = 1, .t = 1},
                       .verify = true};
  Collected result = solve(&game, &preprocessed.active, &options);
  assert(pg_set_count(&result.won[0]) == 2);
  assert(pg_set_count(&result.unresolved) == 3);
  destroy(&result);
  options.fixed = false;
  result = solve(&game, &preprocessed.active, &options);
  assert(result.parameters.k == 2 && result.parameters.t == 1);
  assert(result.calls == 1 && pg_set_empty(&result.unresolved));
  destroy(&result);
  PMResult lifted = {0};
  PMError error = {0};
  assert(pm_lift(&game, &preprocessed.active, PM_STRAHLER,
                 (PMParameters){.k = 2, .t = 1}, PG_EVEN, &lifted, &error));
  assert(pm_verify(&game, &preprocessed.active, &lifted, &error));
  unsigned char const bit = lifted.measure[0].bits[0];
  lifted.measure[0].bits[0] = 2;
  assert(!pm_verify(&game, &preprocessed.active, &lifted, &error));
  lifted.measure[0].bits[0] = bit;
  int const level = lifted.measure[0].levels[0];
  lifted.measure[0].levels[0] = -1;
  assert(!pm_verify(&game, &preprocessed.active, &lifted, &error));
  lifted.measure[0].levels[0] = level;
  lifted.measure[0].top = true;
  assert(!pm_verify(&game, &preprocessed.active, &lifted, &error));
  lifted.measure[0].top = false;
  pg_set_remove(&lifted.region, 0);
  assert(!pm_verify(&game, &preprocessed.active, &lifted, &error));
  pm_result_destroy(&lifted);
  assert(!pm_lift(nullptr, &preprocessed.active, PM_STRAHLER, (PMParameters){0},
                  PG_EVEN, &lifted, &error));
  assert(!pm_lift(&game, &preprocessed.active, PM_STRAHLER,
                  (PMParameters){.k = 0}, PG_EVEN, &lifted, &error));
  pg_preprocess_destroy(&preprocessed);
  pg_game_destroy(&game);
  game = parse("0 1 0 0;\n");
  PGSet active = {0};
  assert(pg_set_init(&active, 1));
  pg_set_fill(&active);
  assert(pm_lift(&game, &active, PM_SUCCINCT, (PMParameters){.bits = 1},
                 PG_EVEN, &lifted, &error));
  assert(lifted.measure[0].top);
  lifted.measure[0].top = false;
  pg_set_add(&lifted.region, 0);
  assert(!pm_verify(&game, &active, &lifted, &error));
  pm_result_destroy(&lifted);
  pg_set_destroy(&active);
  pg_game_destroy(&game);
}

static void test_lifo_order(void) {
  PGGame game = parse("0 1 0 1;\n1 2 0 0;\n2 3 0 0;\n"
                      "3 4 1 2;\n4 5 1 3;\n");
  PGSet active = {0};
  assert(pg_set_init(&active, game.vertex_count));
  pg_set_fill(&active);
  PMResult result = {0};
  PMError error = {0};
  assert(pm_lift(&game, &active, PM_STRAHLER, (PMParameters){.k = 2, .t = 1},
                 PG_EVEN, &result, &error));
  assert(pm_verify(&game, &active, &result, &error));
  /* FIFO leaves vertex 3 at 00 on level 1 instead of Oink's 10. */
  PMMeasure const *measure = &result.measure[3];
  assert(!measure->top && measure->length == 2);
  assert(measure->levels[0] == 1 && measure->levels[1] == 1);
  assert(measure->bits[0] == 1 && measure->bits[1] == 0);
  pm_result_destroy(&result);
  pg_set_destroy(&active);
  pg_game_destroy(&game);
}

static void test_ratio_queue(void) {
  /* Generated by std::priority_queue with Oink's comparator and push order. */
  size_t const expected[][2] = {{1, 1}, {2, 1}, {2, 2}, {3, 2}, {2, 3}, {3, 3},
                                {4, 3}, {1, 2}, {4, 2}, {3, 1}, {1, 3}, {4, 1}};
  ParameterQueue queue = {0};
  assert(parameter_queue_init(&queue, 4, 3));
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
    assert(queue.count != 0);
    assert(queue.heap[0].k == expected[i][0] &&
           queue.heap[0].t == expected[i][1]);
    /* Retrying a productive pair leaves the queue unchanged. */
    parameter_queue_push(&queue, queue.heap[0]);
    assert(queue.heap[0].k == expected[i][0] &&
           queue.heap[0].t == expected[i][1]);
    parameter_queue_advance(&queue);
  }
  assert(queue.count == 0);
  parameter_queue_destroy(&queue);
  assert(parameter_queue_init(&queue, 1, 0));
  assert(queue.count == 0);
  parameter_queue_destroy(&queue);
}

int main(void) {
  test_ratio_queue();
  test_successors();
  test_partial_and_rejection();
  test_lifo_order();
  test_fixtures();
  test_generated();
  return EXIT_SUCCESS;
}
