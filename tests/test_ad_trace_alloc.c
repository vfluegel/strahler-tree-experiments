#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ad_lift_solve.h"
#include "zielonka.h"

#undef malloc
#undef calloc
#undef realloc

static size_t allocation;
static size_t fail_at = SIZE_MAX;

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

static bool accept(void *context, ADTraceEvent const *event) {
  (void)context;
  assert(event->step > 0);
  return true;
}

int main(void) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  assert(fputs("0 2 0 0;1 0 0 1;2 1 0 0;", stream) >= 0);
  assert(fseek(stream, 0, SEEK_SET) == 0);
  PGGame game = {0};
  PGParseError parse = {0};
  assert(pg_game_read(stream, &game, &parse));
  assert(fclose(stream) == 0);
  PGSet active = {0};
  assert(pg_set_init(&active, 3));
  pg_set_fill(&active);
  ADResult certified = {0};
  ZielonkaError error = {0};
  assert(zielonka_decompose(&game, &active, 2, &certified, &error));
  ADLiftTree host = {0};
  ADLiftError lift_error = {0};
  assert(ad_lift_tree_build_full(2, 2, PG_EVEN, 2, &host, &lift_error));
  size_t positions[] = {host.nodes[0].regular, host.nodes[1].after,
                        host.nodes[2].after};
  ADLiftLabelling labels = {
      .labels = positions, .vertex_count = 3, .dominion = active};

  /* Root node/set, child trap/attractor, then the child's restricted node. */
  allocation = 0;
  fail_at = 5;
  ADVerifyError verification = {0};
  assert(certified.decomposition[0]->child_count == 1);
  assert(!ad_tree_check(&game, &active, PG_EVEN, certified.decomposition[0],
                        &verification));
  assert(allocation == fail_at);
  assert(verification.condition == AD_VERIFY_MEMORY);
  assert(verification.path_length == 1 && verification.path[0] == 0);
  assert(verification.vertex == SIZE_MAX);
  assert(strcmp(verification.message, "failed to allocate restricted node") ==
         0);
  fail_at = SIZE_MAX;

  for (size_t scenario = 0; scenario < 10; scenario++) {
    bool completed = false;
    for (size_t nth = 1; nth < 2000; nth++) {
      allocation = 0;
      fail_at = nth;
      ADVerifyError verification = {0};
      ADTrace trace = {.callback = accept};
      ADResult result = {0};
      bool success = false;
      if (scenario == 0) {
        success = ad_tree_check(&game, &active, PG_EVEN,
                                certified.decomposition[0], &verification);
      } else if (scenario == 1) {
        success = ad_tree_relative_verify(
            &game, &active, certified.decomposition[0], &verification);
      } else if (scenario == 2) {
        success = ad_result_verify_complete(&game, &active, &certified,
                                            &verification);
      } else if (scenario == 3) {
        ad_trace_tree(&trace, &game, &active, &active,
                      certified.decomposition[0], (ADTracePath){0});
        success = !trace.failed;
      } else if (scenario == 4) {
        ADNode *candidate = nullptr;
        ADVerifyError *problems = nullptr;
        size_t count = 0;
        success = ad_lift_materialize_partial(&game, &host, &labels, &candidate,
                                              &problems, &count, &lift_error);
        if (success) {
          assert(count == 2);
        } else {
          assert(candidate == nullptr && problems == nullptr && count == 0);
        }
        ad_node_destroy(candidate);
        free(problems);
      } else if (scenario == 5) {
        success = zielonka_decompose_trace(&game, &active, 2, &result, nullptr,
                                           &error, &trace);
      } else if (scenario == 6) {
        success = ad_sym_solve_trace(&game, &active, nullptr, true, &result,
                                     nullptr, &lift_error, &trace);
      } else {
        ADLiftOptions options = {.mode = AD_LIFT_HOST_FULL,
                                 .max_host_leaves = SIZE_MAX,
                                 .verify = true,
                                 .trace = &trace,
                                 .symmetric = scenario == 8,
                                 .iter_shave = scenario == 9};
        success = ad_lift_solve(&game, &active, &options, &result, nullptr,
                                &lift_error);
      }
      fail_at = SIZE_MAX;
      ad_result_destroy(&result);
      if (success) {
        /* Optional acceleration may fail allocation and finish via scalar.
         * Continue until the injected failure is beyond every allocation. */
        if (allocation < nth) {
          completed = true;
          break;
        }
      }
      assert(allocation >= nth);
      if (scenario < 3) {
        assert(verification.condition == AD_VERIFY_MEMORY);
      }
    }
    assert(completed);
  }
  ad_result_destroy(&certified);
  ad_lift_tree_destroy(&host);
  pg_set_destroy(&active);
  pg_game_destroy(&game);
  return 0;
}
