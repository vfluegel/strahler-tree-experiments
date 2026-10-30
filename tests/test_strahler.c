#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strahler.h"

static size_t events;
static int observe(void *context, char const *line, size_t length) {
  size_t *count = context;
  assert(line != nullptr && length > 0 && line[length - 1] == '\n');
  assert(strncmp(line, "{\"event\":", 9) == 0);
  ++*count;
  return 1;
}

static int cancel(void *context, char const *line, size_t length) {
  (void)context;
  (void)line;
  (void)length;
  return 0;
}

static void clear(char **json, char **error) {
  strahler_free(*json);
  strahler_free(*error);
  *json = nullptr;
  *error = nullptr;
}

static void check_violations(void) {
  char const *text = "10 2 0 10;20 0 1 30;30 0 0 30;40 0 0 30;";
  char const *tree = "{\"h\":2,\"H\":[10],\"T\":[],\"S\":[],\"children\":["
                     "{\"h\":0,\"H\":[20],\"T\":[],\"S\":[],\"children\":[]},"
                     "{\"h\":0,\"H\":[30],\"T\":[],\"S\":[],\"children\":[]}]}";
  strahler_game *game = nullptr;
  char *json = nullptr, *error = nullptr;
  assert(strahler_game_parse(text, strlen(text), &game, &error) == 0);
  assert(strahler_check(game, tree, 0, nullptr, 0, &json, &error) == 0);
  assert(
      strstr(json, "\"ok\":false,\"condition\":7,\"path\":[0],\"vertex\":20") !=
      nullptr);
  assert(strstr(json, "\"violations\":[{\"condition\":7,\"path\":[0],"
                      "\"vertex\":20,\"edge\":[20,30]}") != nullptr);
  assert(
      strstr(json,
             "{\"condition\":9,\"path\":[1],\"vertex\":40,\"edge\":[40,30]}") !=
      nullptr);
  clear(&json, &error);
  strahler_game_free(game);
}

int main(void) {
  check_violations();
  char const *text = "parity 2;0 2 1 1 \"root\";1 1 1 1;2 1 1 2;";
  strahler_game *game = nullptr;
  char *json = nullptr, *error = nullptr;
  assert(strahler_game_parse(text, strlen(text), &game, &error) == 0);
  assert(game != nullptr && error == nullptr);
  assert(strahler_version()[0] != '\0');
  assert(strahler_game_summary(game, &json, &error) == 0);
  assert(strstr(json, "strahler-game/1") != nullptr);
  assert(strstr(json, "\"name\":\"root\"") != nullptr);
  clear(&json, &error);
  char const *algorithms[] = {"--algorithm=zielonka",
                              "--algorithm=zielonka-sym", "--algorithm=lifting",
                              "--algorithm=lifting-sym"};
  for (size_t i = 0; i < 4; i++) {
    char const *args[] = {algorithms[i]};
    events = 0;
    assert(strahler_decompose(game, args, 1, observe, &events, &json, &error) ==
           0);
    assert(events > 1 && strstr(json, "strahler-decomp/1") != nullptr);
    clear(&json, &error);
  }
  assert(strahler_progress_measures(game, nullptr, 0, &json, &error) == 0);
  assert(strstr(json, "strahler-pm/1") != nullptr);
  clear(&json, &error);
  char const *tree = "{\"h\":1,\"H\":[1,2],\"T\":[0],\"S\":[],\"children\":[]}";
  assert(strahler_check(game, tree, 1, nullptr, 0, &json, &error) == 0);
  assert(strstr(json, "\"ok\":false") != nullptr);
  clear(&json, &error);
  assert(strahler_check(game, "null", 1, "[]", 0, &json, &error) == 0);
  assert(strstr(json, "\"ok\":true") != nullptr);
  clear(&json, &error);
  char const *labels = "{\"player\":0,\"h\":2,\"tree\":\"0|1|\",\"labels\":["
                       "{\"vertex\":0,\"path\":[],\"part\":\"S\"},"
                       "{\"vertex\":1,\"path\":[0],\"part\":\"S\"},"
                       "{\"vertex\":2,\"path\":[1],\"part\":\"S\"}]}";
  assert(strahler_materialize_partial(game, labels, 0, &json, &error) == 0);
  assert(strstr(json, "\"condition\":22,\"path\":[0],\"vertex\":1") != nullptr);
  assert(strstr(json, "\"condition\":22,\"path\":[1],\"vertex\":2") != nullptr);
  clear(&json, &error);
  assert(strahler_genstree(2, 3, 1, &json, &error) == 0);
  assert(strstr(json, "strahler-tree/1") != nullptr);
  clear(&json, &error);
  assert(strahler_pms_tree("0|1|", 4, nullptr, 0, &json, &error) == 0);
  clear(&json, &error);
  char deep[2054];
  for (size_t i = 0; i < 1026; i++) {
    memcpy(deep + 2 * i, "e,", 2);
  }
  memcpy(deep + 2052, "|", 2);
  assert(strahler_pms_tree(deep, strlen(deep), nullptr, 0, &json, &error) == 2);
  assert(json == nullptr && strstr(error, "depth") != nullptr);
  clear(&json, &error);
  // A trailing comma terminates the last level; it adds no component.
  memcpy(deep + 2050, "|", 2);
  assert(strahler_pms_tree(deep, strlen(deep), nullptr, 0, &json, &error) == 0);
  clear(&json, &error);
  assert(strahler_pms_tree("e,e|e,0|", 8, nullptr, 0, &json, &error) == 0);
  clear(&json, &error);
  char const *limit[] = {"--trace-limit=0"};
  assert(strahler_decompose(game, limit, 1, observe, &events, &json, &error) ==
         1);
  assert(json == nullptr && strstr(error, "limit") != nullptr);
  clear(&json, &error);
  assert(strahler_decompose(game, nullptr, 0, cancel, nullptr, &json, &error) ==
         1);
  clear(&json, &error);
  char const *invalid[] = {nullptr,
                           "--algorithm=bad",
                           "--format=dot",
                           "file.pg",
                           "--trace=/tmp/strahler-should-not-exist",
                           "--help",
                           "--version"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    assert(strahler_decompose(game, invalid + i, 1, nullptr, nullptr, &json,
                              &error) == 2);
    assert(json == nullptr && error != nullptr);
    clear(&json, &error);
  }
  assert(strahler_decompose(game, nullptr, -1, nullptr, nullptr, &json,
                            &error) == 2);
  clear(&json, &error);
  assert(strahler_decompose(nullptr, nullptr, 0, nullptr, nullptr, &json,
                            &error) == 2);
  clear(&json, &error);
  assert(strahler_game_summary(nullptr, &json, &error) == 2);
  clear(&json, &error);
  assert(strahler_game_summary(game, nullptr, &error) == 2);
  clear(&json, &error);
  assert(strahler_progress_measures(nullptr, nullptr, 0, &json, &error) == 2);
  clear(&json, &error);
  char const *bad_json[] = {
      "",
      "{",
      "null x",
      "{}",
      "{\"h\":0,\"h\":1}",
      "{\"h\":18446744073709551616}",
      "{\"h\":01}",
      "{\"h\":-1}",
      "[null,]",
      "{\"\\u0000\":0}",
      "{\"a\":\"\\x\"}",
      "{\"a\":\"\\u123\"}",
      "{\"a\":\"\\u00e9\"}",
      "{\"a\":\"\\\"}",
      "{\"a\":true}",
      "{\"h\":0,\"H\":[999],\"T\":[],\"S\":[],\"children\":[]}",
      "{\"h\":0,\"H\":[1,1],\"T\":[],\"S\":[],\"children\":[]}"};
  for (size_t i = 0; i < sizeof(bad_json) / sizeof(bad_json[0]); i++) {
    assert(strahler_check(game, bad_json[i], 0, nullptr, 0, &json, &error) ==
           2);
    assert(json == nullptr && error != nullptr);
    clear(&json, &error);
  }
  assert(strahler_check(game, tree, 2, nullptr, 0, &json, &error) == 2);
  clear(&json, &error);
  assert(strahler_check(game, tree, 0, "{}", 0, &json, &error) == 2);
  clear(&json, &error);
  assert(strahler_materialize_partial(game, "{}", 0, &json, &error) == 2);
  clear(&json, &error);
  assert(strahler_genstree(0, 1, 0, &json, &error) == 2);
  clear(&json, &error);
  assert(strahler_pms_tree("\0", 1, nullptr, 0, &json, &error) == 2);
  clear(&json, &error);
  assert(strahler_pms_tree("bad", 3, nullptr, 0, &json, &error) != 0);
  clear(&json, &error);
  strahler_game_free(game);
  strahler_game_free(nullptr);
  assert(strahler_game_parse(nullptr, 1, &game, &error) == 2);
  clear(&json, &error);
  assert(strahler_game_parse("bad", 3, &game, &error) == 3);
  clear(&json, &error);
  assert(strahler_game_parse(text, SIZE_MAX, &game, &error) == 2);
  clear(&json, &error);
  puts("C API: game, algorithms, traces, checks, labels, trees, and invalid "
       "input passed");
  return 0;
}
