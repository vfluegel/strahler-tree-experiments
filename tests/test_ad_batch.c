#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ad_batch.h"
#include "ad_lift_solve.h"
#include "ad_reach.h"

static PGGame parse(FILE *file) {
  PGGame game = {0};
  PGParseError error = {0};
  assert(pg_game_read(file, &game, &error));
  return game;
}

static void compare(PGGame const *game, PGSet const *domain, size_t const arity,
                    size_t const lanes, bool const conditional,
                    unsigned const repeats) {
  ADLiftTree hosts[8] = {0};
  PGGame arenas[8] = {0};
  ADBatchRequest requests[8] = {0};
  uint64_t cutoffs[4] = {0};
  size_t used = 0;
  /* Two largest present priorities per parity; small fixtures may repeat. */
  for (unsigned player = 0; player < 2; player++) {
    uint64_t first = player, second = player;
    for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
         v = pg_set_next(domain, v + 1)) {
      uint64_t const p = game->vertices[v].priority;
      if (p % 2 == player && p > first) {
        second = first;
        first = p;
      } else if (p % 2 == player && p < first && p > second) {
        second = p;
      }
    }
    cutoffs[used++] = first;
    cutoffs[used++] = second;
  }
  for (size_t lane = 0; lane < lanes; lane++) {
    PGPlayer const player = (PGPlayer)(lane % 2);
    requests[lane] =
        (ADBatchRequest){hosts + lane, conditional, cutoffs[lane / 2]};
    assert(ad_batch_arena(game, domain, requests + lane, arenas + lane));
    uint64_t maximum = 0;
    for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
         v = pg_set_next(domain, v + 1)) {
      if (arenas[lane].vertices[v].priority > maximum) {
        maximum = arenas[lane].vertices[v].priority;
      }
    }
    uint64_t const level = maximum + (maximum % 2 != player);
    ADLiftError error = {0};
    assert(ad_lift_tree_build_full(arity, (size_t)(level / 2 + 1), player,
                                   level, hosts + lane, &error));
  }

  assert(repeats <= 3);
  double native_times[3] = {0}, batch_times[3] = {0};
  for (unsigned repeat = 0; repeat <= repeats; repeat++) {
    ADLiftLabelling reference[8] = {0}, batch[8] = {0};
    ADLiftError error = {0};
    for (unsigned arm = 0; arm < 2; arm++) {
      clock_t const start = clock();
      bool const native = (arm + repeat) % 2 == 0;
      if (native) {
        for (size_t lane = 0; lane < lanes; lane++) {
          assert(ad_lift_run(arenas + lane, domain, hosts + lane,
                             AD_LIFT_SCHEDULE_ROUNDS, reference + lane,
                             &error));
        }
      } else {
        assert(ad_lift_batch(game, domain, requests, lanes, batch, &error));
      }
      double const elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
      if (repeat != 0) {
        (native ? native_times : batch_times)[repeat - 1] = elapsed;
      }
    }
    for (size_t lane = 0; lane < lanes; lane++) {
      assert(memcmp(reference[lane].labels, batch[lane].labels,
                    game->vertex_count * sizeof(size_t)) == 0);
      assert(reference[lane].stats.lifts == batch[lane].stats.lifts);
      assert(reference[lane].stats.rounds == batch[lane].stats.rounds);
      assert(reference[lane].stats.attractors == batch[lane].stats.attractors);
      assert(reference[lane].stats.top_count == batch[lane].stats.top_count);
      assert(reference[lane].stats.max_position ==
             batch[lane].stats.max_position);
      assert(pg_set_equal(&reference[lane].dominion, &batch[lane].dominion));
      ADVerifyError verification = {0};
      assert(ad_lift_verify(arenas + lane, domain, hosts + lane, batch + lane,
                            &verification));
      ad_lift_labelling_destroy(reference + lane);
      ad_lift_labelling_destroy(batch + lane);
    }
  }
  if (repeats > 1) {
    for (unsigned i = 0; i < repeats; i++) {
      for (unsigned j = i + 1; j < repeats; j++) {
        if (native_times[j] < native_times[i]) {
          double const t = native_times[i];
          native_times[i] = native_times[j];
          native_times[j] = t;
        }
        if (batch_times[j] < batch_times[i]) {
          double const t = batch_times[i];
          batch_times[i] = batch_times[j];
          batch_times[j] = t;
        }
      }
    }
    size_t positions = 0;
    for (size_t lane = 0; lane < lanes; lane++) {
      if (hosts[lane].position_count > positions) {
        positions = hosts[lane].position_count;
      }
    }
    printf("batch lanes=%zu arity=%zu positions=%zu native_cpu=%.6f "
           "batch_cpu=%.6f native_spread=%.6f batch_spread=%.6f repeats=%u\n",
           lanes, arity, positions, native_times[repeats / 2],
           batch_times[repeats / 2],
           native_times[repeats - 1] - native_times[0],
           batch_times[repeats - 1] - batch_times[0], repeats);
  }
  for (size_t lane = 0; lane < lanes; lane++) {
    ad_lift_tree_destroy(hosts + lane);
    pg_game_destroy(arenas + lane);
  }
}

static void false_original_claim(void) {
  FILE *file = tmpfile();
  assert(file);
  assert(fputs("parity 1;\n0 0 0 1;\n1 1 1 1;\n", file) >= 0);
  assert(fseek(file, 0, SEEK_SET) == 0);
  PGGame game = parse(file);
  assert(fclose(file) == 0);
  PGSet domain = {0};
  assert(pg_set_init(&domain, 2));
  pg_set_fill(&domain);
  ADLiftTree host = {0};
  ADLiftError error = {0};
  assert(ad_lift_tree_build_full(1, 2, PG_EVEN, 2, &host, &error));
  ADBatchRequest request = {&host, true, 0};
  ADLiftLabelling result = {0};
  assert(ad_lift_batch(&game, &domain, &request, 1, &result, &error));
  assert(pg_set_contains(&result.dominion, 0));
  ADVerifyError verification = {0};
  assert(!ad_lift_verify(&game, &domain, &host, &result, &verification));
  ad_lift_labelling_destroy(&result);
  ad_lift_tree_destroy(&host);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void wide_fallbacks(void) {
  size_t const degree = 65536;
  size_t *edges = calloc(degree, sizeof(*edges));
  assert(edges);
  size_t offsets[2] = {0, degree};
  PGVertex vertex = {.priority = 1, .owner = PG_ODD};
  PGGame game = {.vertex_count = 1,
                 .edge_count = degree,
                 .max_priority = 1,
                 .vertices = &vertex,
                 .succ_offsets = offsets,
                 .pred_offsets = offsets,
                 .successors = edges,
                 .predecessors = edges};
  PGSet domain = {0};
  assert(pg_set_init(&domain, 1));
  pg_set_fill(&domain);
  for (unsigned scenario = 0; scenario < 2; scenario++) {
    ADLiftTree host = {0};
    ADLiftError error = {0};
    if (scenario == 0) {
      /* A 16-bit opponent counter must not wrap to zero and justify this
       * closed lazy self-loop. Its actual winner is Odd. */
      assert(ad_lift_tree_build_full(1, 2, PG_EVEN, 2, &host, &error));
    } else {
      offsets[1] = 1;
      game.edge_count = 1;
      assert(ad_lift_tree_build_full(2, 15, PG_EVEN, 28, &host, &error));
      assert(host.top > UINT16_MAX);
    }
    ADBatchRequest const request = {&host, false, 0};
    ADLiftLabelling native = {0}, batch = {0};
    assert(ad_lift_run(&game, &domain, &host, AD_LIFT_SCHEDULE_ROUNDS, &native,
                       &error));
    assert(ad_lift_batch(&game, &domain, &request, 1, &batch, &error));
    assert(native.labels[0] == batch.labels[0]);
    assert(native.stats.rounds == batch.stats.rounds &&
           native.stats.lifts == batch.stats.lifts);
    assert(batch.labels[0] == host.top && pg_set_empty(&batch.dominion));
    ADVerifyError verification = {0};
    assert(ad_lift_verify(&game, &domain, &host, &batch, &verification));
    ad_lift_labelling_destroy(&native);
    ad_lift_labelling_destroy(&batch);
    ad_lift_tree_destroy(&host);
  }
  pg_set_destroy(&domain);
  free(edges);
}

int main(int const argc, char **argv) {
  assert(setenv("STRAHLER_AD_BACKEND", argc > 1 ? "fused" : "scalar", 1) == 0);
  FILE *input = argc > 1 ? fopen(argv[1], "r") : tmpfile();
  assert(input);
  if (argc == 1) {
    assert(fputs("parity 7;\n0 0 0 0,1;\n1 1 1 0,2;\n2 2 0 3,2;\n3 3 1 0,4;\n4 "
                 "4 1 5,4;\n5 5 0 6;\n6 6 1 1,7;\n7 7 0 7,0;\n",
                 input) >= 0);
    assert(fseek(input, 0, SEEK_SET) == 0);
  }
  PGGame game = parse(input);
  PGPriorityMap map = {0};
  assert(pg_priority_map_build(&game, &map));
  assert(pg_priority_map_apply(&map, &game));
  pg_priority_map_destroy(&map);
  assert(fclose(input) == 0);
  PGSet domain = {0};
  assert(pg_set_init(&domain, game.vertex_count));
  pg_set_fill(&domain);
  if (argc > 1) {
    ADLiftOptions const options = {.mode = AD_LIFT_HOST_STRAHLER,
                                   .k = 1,
                                   .iter_shave = true,
                                   .verify = true,
                                   .max_host_leaves = SIZE_MAX};
    ADResult shaved = {0};
    ADLiftError error = {0};
    assert(ad_lift_solve(&game, &domain, &options, &shaved, nullptr, &error));
    pg_set_clear(&domain);
    pg_set_union_into(&domain, &shaved.unresolved);
    ad_result_destroy(&shaved);
    printf("conditional residual: %zu vertices\n", pg_set_count(&domain));
  }
  for (size_t lanes = 1; lanes <= 8; lanes *= 2) {
    compare(&game, &domain, 1, lanes, true, argc > 1 ? 3 : 1);
    compare(&game, &domain, 2, lanes, true, argc > 1 ? 3 : 1);
    if (argc == 1) {
      compare(&game, &domain, 2, lanes, false, 1);
    }
  }
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
  false_original_claim();
  if (argc == 1) {
    wide_fallbacks();
  }
  return 0;
}
