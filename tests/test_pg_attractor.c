#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pg_attractor.h"
#include "reference_attractor.h"

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

static void test_fixed_cases(void) {
  PGGame game = parse("0 0 0 0;\n"
                      "1 0 0 0;\n"
                      "2 0 1 0,3;\n"
                      "3 0 1 0;\n");
  PGSet active = {0};
  PGSet target = {0};
  PGSet result = {0};
  assert(pg_set_init(&active, 4));
  assert(pg_set_init(&target, 4));
  pg_set_fill(&active);
  pg_set_add(&target, 0);
  assert(pg_attractor(&game, &active, &target, PG_EVEN, &result));
  assert(pg_set_count(&result) == 4);
  assert(pg_set_subset(&target, &result));
  pg_set_destroy(&result);

  pg_set_clear(&target);
  assert(pg_attractor(&game, &active, &target, PG_EVEN, &result));
  assert(pg_set_empty(&result));
  pg_set_destroy(&result);

  pg_set_clear(&active);
  pg_set_add(&active, 0);
  pg_set_add(&active, 2);
  pg_set_add(&target, 0);
  assert(pg_attractor(&game, &active, &target, PG_EVEN, &result));
  assert(pg_set_contains(&result, 2));
  pg_set_destroy(&result);

  pg_set_clear(&active);
  pg_set_add(&active, 1);
  assert(!pg_subgame_is_total(&game, &active));
  pg_set_clear(&target);
  assert(!pg_attractor(&game, &active, &target, PG_EVEN, &result));

  pg_set_destroy(&active);
  pg_set_destroy(&target);
  pg_game_destroy(&game);
}

static void test_duplicate_edges(void) {
  PGGame game = parse("0 0 0 0;\n1 0 1 0,0;\n");
  PGSet active = {0};
  PGSet target = {0};
  PGSet result = {0};
  assert(pg_set_init(&active, 2));
  assert(pg_set_init(&target, 2));
  pg_set_fill(&active);
  pg_set_add(&target, 0);
  assert(pg_attractor(&game, &active, &target, PG_EVEN, &result));
  assert(pg_set_count(&result) == 2);
  pg_set_destroy(&active);
  pg_set_destroy(&target);
  pg_set_destroy(&result);
  pg_game_destroy(&game);
}

[[nodiscard]] static PGSet set_of(size_t const count, size_t const size,
                                  size_t const members[static size]) {
  PGSet set = {0};
  assert(pg_set_init(&set, count));
  for (size_t index = 0; index < size; index++) {
    pg_set_add(&set, members[index]);
  }
  return set;
}

static void test_through_fixed_cases(void) {
  /* Vertex 2 is the only unsafe vertex. Vertex 7 is inactive, so the edge
   * from 6 to 7 is not an escape. */
  PGGame game = parse("0 0 0 0;\n"
                      "1 0 0 0,2;\n"
                      "2 0 1 2;\n"
                      "3 0 1 0,2;\n"
                      "4 0 1 2;\n"
                      "5 0 1 1,0;\n"
                      "6 0 1 0,7;\n"
                      "7 0 0 7;\n");
  PGSet active = set_of(8, 7, (size_t[]){0, 1, 2, 3, 4, 5, 6});
  PGSet safe = set_of(8, 6, (size_t[]){0, 1, 3, 4, 5, 6});
  PGSet target = set_of(8, 2, (size_t[]){0, 4});
  PGSet result = {0};
  assert(pg_subgame_is_total(&game, &active));
  assert(!pg_subgame_is_total(&game, &safe));

  /* 1: a player vertex with one target edge and one unsafe edge is attracted.
   * 3: an opponent vertex with the same edges is not.
   * 4: a target vertex counts even though its only edge is unsafe.
   * 5: an opponent vertex whose successors are both attracted is attracted.
   * 6: an edge leaving the active domain is ignored. */
  assert(
      pg_attractor_through(&game, &active, &safe, &target, PG_EVEN, &result));
  PGSet expected = set_of(8, 5, (size_t[]){0, 1, 4, 5, 6});
  assert(pg_set_equal(&result, &expected));
  pg_set_destroy(&result);
  pg_set_destroy(&expected);

  /* In the dual run, 3 is attracted and 1 is not; the Odd vertices 5 and 6
   * move to the target directly. */
  pg_set_remove(&target, 4);
  assert(pg_attractor_through(&game, &active, &safe, &target, PG_ODD, &result));
  expected = set_of(8, 4, (size_t[]){0, 3, 5, 6});
  assert(pg_set_equal(&result, &expected));
  pg_set_destroy(&result);
  pg_set_destroy(&expected);

  /* Ordinary attraction is the special case safe == active. */
  pg_set_clear(&target);
  pg_set_add(&target, 0);
  PGSet ordinary = {0};
  assert(pg_attractor(&game, &active, &target, PG_EVEN, &ordinary));
  assert(
      pg_attractor_through(&game, &active, &active, &target, PG_EVEN, &result));
  assert(pg_set_equal(&result, &ordinary));
  pg_set_destroy(&ordinary);
  pg_set_destroy(&result);

  /* The inclusions target ⊆ safe ⊆ active are required. */
  pg_set_add(&target, 2);
  assert(
      !pg_attractor_through(&game, &active, &safe, &target, PG_EVEN, &result));
  pg_set_remove(&target, 2);
  pg_set_add(&safe, 7);
  assert(
      !pg_attractor_through(&game, &active, &safe, &target, PG_EVEN, &result));
  assert(!pg_attractor_through(&game, &active, &active, &target, PG_EVEN,
                               nullptr));

  pg_set_destroy(&active);
  pg_set_destroy(&safe);
  pg_set_destroy(&target);
  pg_game_destroy(&game);
}

static uint32_t random_state = UINT32_C(0x5eed1234);

[[nodiscard]] static uint32_t next_random(void) {
  random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
  return random_state;
}

static void test_reference_agreement(void) {
  for (size_t iteration = 0; iteration < 100; iteration++) {
    size_t const count = 2 + next_random() % 6;
    FILE *stream = tmpfile();
    assert(stream != nullptr);
    for (size_t vertex = 0; vertex < count; vertex++) {
      size_t const second = next_random() % count;
      assert(fprintf(stream, "%zu %u %u %zu,%zu;\n", vertex, next_random() % 5,
                     next_random() % 2, vertex, second) > 0);
    }
    assert(fseek(stream, 0, SEEK_SET) == 0);
    PGGame game = {0};
    PGParseError error = {0};
    assert(pg_game_read(stream, &game, &error));
    assert(fclose(stream) == 0);

    PGSet active = {0};
    PGSet target = {0};
    PGSet optimized = {0};
    PGSet reference = {0};
    assert(pg_set_init(&active, count));
    assert(pg_set_init(&target, count));
    for (size_t vertex = 0; vertex < count; vertex++) {
      if ((next_random() & 1U) != 0) {
        pg_set_add(&active, vertex);
      }
    }
    if (pg_set_empty(&active)) {
      pg_set_add(&active, next_random() % count);
    }
    for (size_t vertex = pg_set_next(&active, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(&active, vertex + 1)) {
      if ((next_random() & 1U) != 0) {
        pg_set_add(&target, vertex);
      }
    }
    PGPlayer const player = (PGPlayer)(next_random() % 2);
    assert(pg_subgame_is_total(&game, &active));
    assert(pg_attractor(&game, &active, &target, player, &optimized));
    assert(reference_attractor(&game, &active, &target, player, &reference));
    assert(pg_set_equal(&optimized, &reference));

    pg_set_destroy(&active);
    pg_set_destroy(&target);
    pg_set_destroy(&optimized);
    pg_set_destroy(&reference);
    pg_game_destroy(&game);
  }
}

static void test_through_reference_agreement(void) {
  for (size_t iteration = 0; iteration < 200; iteration++) {
    size_t const count = 2 + next_random() % 7;
    FILE *stream = tmpfile();
    assert(stream != nullptr);
    for (size_t vertex = 0; vertex < count; vertex++) {
      size_t const second = next_random() % count;
      size_t const third = next_random() % count;
      assert(fprintf(stream, "%zu 0 %u %zu,%zu,%zu;\n", vertex,
                     next_random() % 2, vertex, second, third) > 0);
    }
    assert(fseek(stream, 0, SEEK_SET) == 0);
    PGGame game = {0};
    PGParseError error = {0};
    assert(pg_game_read(stream, &game, &error));
    assert(fclose(stream) == 0);

    /* Self-loops keep every active set total; safe and target are arbitrary
     * nested subsets, so safe is usually not total. */
    PGSet active = {0};
    PGSet safe = {0};
    PGSet target = {0};
    PGSet optimized = {0};
    PGSet reference = {0};
    assert(pg_set_init(&active, count));
    assert(pg_set_init(&safe, count));
    assert(pg_set_init(&target, count));
    for (size_t vertex = 0; vertex < count; vertex++) {
      if (next_random() % 4 != 0) {
        pg_set_add(&active, vertex);
      }
    }
    for (size_t vertex = pg_set_next(&active, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(&active, vertex + 1)) {
      if (next_random() % 3 != 0) {
        pg_set_add(&safe, vertex);
      }
    }
    for (size_t vertex = pg_set_next(&safe, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(&safe, vertex + 1)) {
      if (next_random() % 3 == 0) {
        pg_set_add(&target, vertex);
      }
    }
    PGPlayer const player = (PGPlayer)(next_random() % 2);
    assert(pg_attractor_through(&game, &active, &safe, &target, player,
                                &optimized));
    assert(reference_attractor_through(&game, &active, &safe, &target, player,
                                       &reference));
    assert(pg_set_equal(&optimized, &reference));
    assert(pg_set_subset(&target, &optimized));
    assert(pg_set_subset(&optimized, &safe));

    pg_set_destroy(&active);
    pg_set_destroy(&safe);
    pg_set_destroy(&target);
    pg_set_destroy(&optimized);
    pg_set_destroy(&reference);
    pg_game_destroy(&game);
  }
}

int main(void) {
  test_fixed_cases();
  test_duplicate_edges();
  test_reference_agreement();
  test_through_fixed_cases();
  test_through_reference_agreement();
  return 0;
}
