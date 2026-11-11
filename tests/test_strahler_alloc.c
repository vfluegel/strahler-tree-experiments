#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "strahler.h"

#undef malloc
#undef calloc
#undef realloc

static size_t allocation;
static size_t fail_at = SIZE_MAX;

void *trace_test_malloc(size_t size) {
  if (++allocation == fail_at) {
    errno = ENOMEM;
    return nullptr;
  }
  return malloc(size);
}
void *trace_test_calloc(size_t count, size_t size) {
  if (++allocation == fail_at) {
    errno = ENOMEM;
    return nullptr;
  }
  return calloc(count, size);
}
void *trace_test_realloc(void *data, size_t size) {
  if (++allocation == fail_at) {
    errno = ENOMEM;
    return nullptr;
  }
  return realloc(data, size);
}

static int observe(void *context, char const *line, size_t length) {
  (void)context;
  assert(line != nullptr && length != 0);
  return 1;
}

int main(void) {
  char const *text = "0 2 1 1;1 1 1 1;2 1 1 2;";
  char const *tree =
      "{\"h\":3,\"H\":[],\"T\":[],\"S\":[],\"children\":["
      "{\"h\":1,\"H\":[1,2],\"T\":[],\"S\":[0],\"children\":[]}]}";
  char const *labels = "{\"player\":0,\"h\":2,\"tree\":\"0|1|\",\"labels\":["
                       "{\"vertex\":1,\"path\":[0],\"part\":\"S\"},"
                       "{\"vertex\":2,\"path\":[1],\"part\":\"S\"}]}";
  strahler_game *game = nullptr;
  strahler_game *adaptive_game = nullptr;
  char *error = nullptr;
  assert(strahler_game_parse(text, strlen(text), &game, &error) == 0);
  char const *adaptive_text = "0 2 0 3;1 2 1 2;2 0 1 1,4;3 0 0 3;4 3 1 3;";
  assert(strahler_game_parse(adaptive_text, strlen(adaptive_text),
                             &adaptive_game, &error) == 0);
  for (size_t scenario = 0; scenario < 14; scenario++) {
    bool completed = false;
    for (size_t nth = 1; nth < 20000; nth++) {
      allocation = 0;
      fail_at = nth;
      char *json = nullptr;
      int status = 0;
      strahler_game *parsed = nullptr;
      char const *args[] = {scenario == 2   ? "--algorithm=zielonka"
                            : scenario == 3 ? "--algorithm=zielonka-sym"
                            : scenario == 4 ? "--algorithm=lifting-sym"
                                            : "--algorithm=lifting"};
      switch (scenario) {
      case 0:
        status = strahler_game_parse(text, strlen(text), &parsed, &error);
        break;
      case 1:
        status = strahler_game_summary(game, &json, &error);
        break;
      case 2:
      case 3:
      case 4:
      case 5:
        status =
            strahler_decompose(game, args, 1, observe, nullptr, &json, &error);
        break;
      case 6:
        status = strahler_progress_measures(game, nullptr, 0, &json, &error);
        break;
      case 7:
        status = strahler_check(game, tree, 1, nullptr, 0, &json, &error);
        break;
      case 8:
        status = strahler_materialize_partial(game, labels, 0, &json, &error);
        break;
      case 9:
        status = strahler_genstree(2, 3, 1, &json, &error);
        break;
      case 10:
        status = strahler_pms_tree("0|1|", 4, nullptr, 0, &json, &error);
        break;
      case 11:
        status = strahler_check(game, tree, 1, "[0,1,2]", 1, &json, &error);
        break;
      case 12:
      case 13: {
        char const *search_args[] = {"--algorithm=lifting-sym",
                                     "--search=lexicographic", "--iter-shave"};
        status = strahler_decompose(adaptive_game, search_args,
                                    scenario == 12 ? 2 : 3, observe, nullptr,
                                    &json, &error);
        break;
      }
      default:
        assert(false);
      }
      fail_at = SIZE_MAX;
      strahler_game_free(parsed);
      strahler_free(json);
      strahler_free(error);
      error = nullptr;
      if (status == 0) {
        /* Optional packed workspaces may fall back after allocation failure. */
        if (allocation < nth) {
          completed = true;
          break;
        }
        continue;
      }
      assert(status == 6 && allocation >= nth);
    }
    assert(completed);
  }
  allocation = 0;
  fail_at = 1;
  strahler_game *invalid = nullptr;
  assert(strahler_game_parse(nullptr, 0, &invalid, &error) == 6);
  assert(error == nullptr && invalid == nullptr);
  fail_at = SIZE_MAX;
  strahler_game_free(adaptive_game);
  strahler_game_free(game);
  return 0;
}
