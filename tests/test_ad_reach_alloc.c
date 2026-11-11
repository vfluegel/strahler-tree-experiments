#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "ad_reach.h"

#undef malloc
#undef calloc
#undef realloc

static size_t allocation, fail_at = SIZE_MAX;
void *trace_test_malloc(size_t const size) {
  if (++allocation == fail_at) {
    errno = ENOMEM;
    return nullptr;
  }
  return malloc(size);
}
void *trace_test_calloc(size_t const count, size_t const size) {
  if (++allocation == fail_at) {
    errno = ENOMEM;
    return nullptr;
  }
  return calloc(count, size);
}
void *trace_test_realloc(void *data, size_t const size) {
  if (++allocation == fail_at) {
    errno = ENOMEM;
    return nullptr;
  }
  return realloc(data, size);
}

int main(void) {
  FILE *file = tmpfile();
  assert(file);
  assert(fputs("parity 63;\n", file) >= 0);
  for (unsigned v = 0; v < 64; v++) {
    assert(fprintf(file, "%u 0 0 %u;\n", v, v) > 0);
  }
  assert(fseek(file, 0, SEEK_SET) == 0);
  PGGame game = {0};
  PGParseError error = {0};
  assert(pg_game_read(file, &game, &error));
  assert(fclose(file) == 0);
  PGSet domain = {0};
  assert(pg_set_init(&domain, 64));
  pg_set_fill(&domain);
  char const *backends[] = {"dirty", "byte"};
  for (unsigned backend = 0; backend < 2; backend++) {
    assert(setenv("STRAHLER_AD_BACKEND", backends[backend], 1) == 0);
    for (size_t nth = 1;; nth++) {
      allocation = 0;
      fail_at = nth;
      ADReach *work = ad_reach_create(&game, &domain, PG_EVEN);
      if (work != nullptr) {
        for (size_t v = 0; v < 64; v++) {
          ad_reach_labels(work)[v] = (ADReachLabel){10, 0, AD_REACH_LAZY};
        }
        ad_reach_compute(work);
        for (size_t v = 0; v < 64; v++) {
          assert(!ad_reach_good(work)[v]);
        }
        ad_reach_destroy(work);
      }
      if (allocation < nth) {
        break;
      }
      assert(nth < 64);
    }
  }
  fail_at = SIZE_MAX;
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
  return 0;
}
