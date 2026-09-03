#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pg_game.h"

[[nodiscard]] static FILE *input_stream(char const *text) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  size_t const length = strlen(text);
  assert(fwrite(text, 1, length, stream) == length);
  assert(fseek(stream, 0, SEEK_SET) == 0);
  return stream;
}

[[nodiscard]] static PGGame parse(char const *text) {
  FILE *stream = input_stream(text);
  PGGame game = {0};
  PGParseError error = {0};
  bool const succeeded = pg_game_read(stream, &game, &error);
  if (!succeeded) {
    fprintf(stderr, "parse failed at %zu:%zu: %s\n", error.line, error.column,
            error.message);
  }
  assert(succeeded);
  assert(fclose(stream) == 0);
  return game;
}

static void reject(char const *text) {
  FILE *stream = input_stream(text);
  PGGame game = {0};
  PGParseError error = {0};
  assert(!pg_game_read(stream, &game, &error));
  assert(game.vertex_count == 0);
  assert(game.vertices == nullptr);
  assert(error.line > 0);
  assert(error.column > 0);
  assert(error.message[0] != '\0');
  pg_game_destroy(&game);
  assert(fclose(stream) == 0);
}

[[nodiscard]] static char *write_game(PGGame const *game,
                                      bool const include_names) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  assert(pg_game_write_pgsolver(stream, game, include_names));
  assert(fflush(stream) == 0);
  long const length = ftell(stream);
  assert(length >= 0);
  assert(fseek(stream, 0, SEEK_SET) == 0);
  char *result = malloc((size_t)length + 1);
  assert(result != nullptr);
  assert(fread(result, 1, (size_t)length, stream) == (size_t)length);
  result[length] = '\0';
  assert(fclose(stream) == 0);
  return result;
}

static void test_sparse_dense_graph(void) {
  PGGame game = parse("parity 1000;\n"
                      "1000 7 1 10 \"last\";\n"
                      "10 2 0 1000, 10 \"first\";\n");
  assert(game.vertex_count == 2);
  assert(game.edge_count == 3);
  assert(game.max_priority == 7);
  assert(game.vertices[0].external_id == 10);
  assert(game.vertices[0].priority == 2);
  assert(game.vertices[0].owner == PG_EVEN);
  assert(strcmp(game.vertices[0].name, "first") == 0);
  assert(game.vertices[1].external_id == 1000);
  assert(game.vertices[1].owner == PG_ODD);
  assert(strcmp(game.vertices[1].name, "last") == 0);

  size_t const expected_succ_offsets[] = {0, 2, 3};
  size_t const expected_successors[] = {1, 0, 0};
  size_t const expected_pred_offsets[] = {0, 2, 3};
  size_t const expected_predecessors[] = {0, 1, 0};
  assert(memcmp(game.succ_offsets, expected_succ_offsets,
                sizeof(expected_succ_offsets)) == 0);
  assert(memcmp(game.successors, expected_successors,
                sizeof(expected_successors)) == 0);
  assert(memcmp(game.pred_offsets, expected_pred_offsets,
                sizeof(expected_pred_offsets)) == 0);
  assert(memcmp(game.predecessors, expected_predecessors,
                sizeof(expected_predecessors)) == 0);
  pg_game_destroy(&game);
}

static void test_last_declaration_wins(void) {
  PGGame game = parse("9 1 0 9 \"old\";\n"
                      "1 2 1 9;\n"
                      "9 4 1 1 \"new\";\n");
  assert(game.vertex_count == 2);
  assert(game.edge_count == 2);
  assert(game.vertices[0].external_id == 1);
  assert(game.vertices[0].name == nullptr);
  assert(game.vertices[1].external_id == 9);
  assert(game.vertices[1].priority == 4);
  assert(game.vertices[1].owner == PG_ODD);
  assert(strcmp(game.vertices[1].name, "new") == 0);
  assert(game.successors[0] == 1);
  assert(game.successors[1] == 0);
  pg_game_destroy(&game);
}

static void test_canonical_round_trip(void) {
  PGGame game = parse("  20 0 0 20,3 \"back\\slash\" ;\n"
                      "3 11 1 20;\n");
  char *with_names = write_game(&game, true);
  assert(strcmp(with_names, "parity 20;\n"
                            "3 11 1 20;\n"
                            "20 0 0 20,3 \"back\\slash\";\n") == 0);

  PGGame round_trip = parse(with_names);
  char *second = write_game(&round_trip, true);
  assert(strcmp(with_names, second) == 0);
  free(second);
  pg_game_destroy(&round_trip);

  char *without_names = write_game(&game, false);
  assert(strstr(without_names, "back") == nullptr);
  free(without_names);
  free(with_names);
  pg_game_destroy(&game);
}

static void test_start_directive(void) {
  PGGame game = parse("parity 0;\n"
                      "start 18446744073709551615;\n"
                      "0 0 0 0;\n");
  assert(game.vertex_count == 1);
  assert(game.vertices[0].external_id == 0);
  char *written = write_game(&game, true);
  assert(strcmp(written, "parity 0;\n0 0 0 0;\n") == 0);
  free(written);
  pg_game_destroy(&game);

  PGGame headerless = parse("start 999;\n7 1 1 7;\n");
  assert(headerless.vertex_count == 1);
  assert(headerless.vertices[0].external_id == 7);
  pg_game_destroy(&headerless);
}

static void test_priority_map(void) {
  PGGame game = parse("parity 3;\n"
                      "0 0 0 0;\n"
                      "1 3 1 1;\n"
                      "2 4 0 2;\n"
                      "3 100 1 3;\n");
  PGPriorityMap map = {0};
  assert(pg_priority_map_build(&game, &map));
  assert(map.count == 4);
  uint64_t const original[] = {0, 3, 4, 100};
  uint64_t const compact[] = {0, 1, 2, 4};
  for (size_t index = 0; index < map.count; index++) {
    assert(map.entries[index].original == original[index]);
    assert(map.entries[index].compact == compact[index]);
  }

  uint64_t bound = UINT64_MAX;
  assert(pg_priority_map_original_bound(&map, 0, &bound) && bound == 0);
  assert(pg_priority_map_original_bound(&map, 1, &bound) && bound == 3);
  assert(pg_priority_map_original_bound(&map, 2, &bound) && bound == 4);
  assert(pg_priority_map_original_bound(&map, 3, &bound) && bound == 5);
  assert(pg_priority_map_original_bound(&map, 4, &bound) && bound == 100);
  assert(pg_priority_map_original_bound(&map, 5, &bound) && bound == 101);
  assert(!pg_priority_map_original_bound(&map, UINT64_MAX, &bound));

  assert(pg_priority_map_apply(&map, &game));
  assert(game.max_priority == 4);
  for (size_t vertex = 0; vertex < game.vertex_count; vertex++) {
    assert(game.vertices[vertex].priority == compact[vertex]);
  }
  assert(pg_priority_map_restore(&map, &game));
  assert(game.max_priority == 100);
  for (size_t vertex = 0; vertex < game.vertex_count; vertex++) {
    assert(game.vertices[vertex].priority == original[vertex]);
  }

  pg_priority_map_destroy(&map);
  assert(map.entries == nullptr && map.count == 0);
  pg_game_destroy(&game);
}

static void test_invalid_games(void) {
  reject("");
  reject("   \n\t");
  reject("parity 0;");
  reject("parity; 0 0 0 0;");
  reject("parity 0 0 0 0 0;");
  reject("game 0; 0 0 0 0;");
  reject("-1 0 0 0;");
  reject("18446744073709551616 0 0 0;");
  reject("parity 0; 1 0 0 0;");
  reject("parity 0; 0 0 0 1;");
  reject("0 0 2 0;");
  reject("0 0 0;");
  reject("0 0 0 0,;");
  reject("0 0 0 0");
  reject("0 0 0 1;");
  reject("0 0 0 0 \"unterminated;");
  reject("0 0 0 0 \"caf\303\251\";");
  reject("0 0 0 0; trailing");
  reject("parity 0; 1 0 0 1; 1 0 0 1;");
  reject("start; 0 0 0 0;");
  reject("start 0 0 0 0;");
  reject("start 0; start 0; 0 0 0 0;");
  reject("0 0 0 0; start 0;");
}

static void test_api_guards(void) {
  PGParseError error = {0};
  PGGame game = {0};
  assert(!pg_game_read(nullptr, &game, &error));
  assert(error.message[0] != '\0');
  FILE *stream = input_stream("0 0 0 0;");
  assert(!pg_game_read(stream, nullptr, &error));
  assert(fclose(stream) == 0);
  assert(!pg_game_write_pgsolver(nullptr, &game, true));
  assert(!pg_game_write_pgsolver(stdout, &game, true));
  PGPriorityMap map = {0};
  uint64_t bound = 0;
  assert(!pg_priority_map_build(nullptr, &map));
  assert(!pg_priority_map_apply(&map, &game));
  assert(!pg_priority_map_restore(&map, &game));
  assert(!pg_priority_map_original_bound(&map, 0, &bound));
  pg_priority_map_destroy(nullptr);
  pg_priority_map_destroy(&map);
  pg_game_destroy(nullptr);
  pg_game_destroy(&game);
}

int main(void) {
  test_sparse_dense_graph();
  test_last_declaration_wins();
  test_canonical_round_trip();
  test_start_directive();
  test_priority_map();
  test_invalid_games();
  test_api_guards();
  return 0;
}
