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

int main(void) {
  test_fixed_cases();
  test_duplicate_edges();
  test_reference_agreement();
  return 0;
}
