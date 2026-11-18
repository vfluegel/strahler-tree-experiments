#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ad_reach.h"
#include "reference_attractor.h"

static unsigned rng = 7019;
static unsigned random_value(void) {
  rng = rng * 1664525U + 1013904223U;
  return rng;
}

/* Construct CSR directly: unlike a textual fixture this preserves duplicate
 * edges, so the predecessor counters must discharge once per actual edge. */
static PGGame graph(size_t const n, bool const chain) {
  PGGame game = {.vertex_count = n};
  game.vertices = calloc(n, sizeof(*game.vertices));
  game.succ_offsets = calloc(n + 1, sizeof(size_t));
  game.pred_offsets = calloc(n + 1, sizeof(size_t));
  game.successors = malloc(11 * n * sizeof(size_t));
  game.predecessors = malloc(11 * n * sizeof(size_t));
  assert(game.vertices && game.succ_offsets && game.pred_offsets &&
         game.successors && game.predecessors);
  for (size_t v = 0; v < n; v++) {
    game.vertices[v].owner = chain ? PG_EVEN : (PGPlayer)(random_value() % 2);
    game.succ_offsets[v] = game.edge_count;
    size_t const degree = chain ? 3 : 1 + random_value() % 11;
    for (size_t j = 0; j < degree; j++) {
      size_t const u = chain ? (v + 1 < n ? v + 1 : v) : random_value() % n;
      game.successors[game.edge_count++] = u;
      game.pred_offsets[u + 1]++;
    }
  }
  game.succ_offsets[n] = game.edge_count;
  for (size_t v = 0; v < n; v++) {
    game.pred_offsets[v + 1] += game.pred_offsets[v];
  }
  size_t *next = malloc(n * sizeof(size_t));
  assert(next);
  memcpy(next, game.pred_offsets, n * sizeof(size_t));
  for (size_t v = 0; v < n; v++) {
    for (size_t e = game.succ_offsets[v]; e < game.succ_offsets[v + 1]; e++) {
      game.predecessors[next[game.successors[e]]++] = v;
    }
  }
  free(next);
  return game;
}

static void oracle(PGGame const *game, PGSet const *domain,
                   PGPlayer const player, ADReachLabel const *labels,
                   unsigned char *expected) {
  PGSet safe = {0}, lower = {0};
  assert(pg_set_init(&safe, game->vertex_count));
  assert(pg_set_init(&lower, game->vertex_count));
  memset(expected, 0, game->vertex_count);
  for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
       v = pg_set_next(domain, v + 1)) {
    ADReachLabel const label = labels[v];
    if (label.kind == AD_REACH_TOP) {
      expected[v] = 1;
      continue;
    }
    if (label.kind == AD_REACH_REGULAR) {
      bool any = false, all = true;
      for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1];
           e++) {
        size_t const u = game->successors[e];
        if (!pg_set_contains(domain, u)) {
          continue;
        }
        bool const good = labels[u].rank < label.after;
        any |= good;
        all &= good;
      }
      expected[v] = game->vertices[v].owner == player ? any : all;
      continue;
    }
    pg_set_clear(&safe);
    pg_set_clear(&lower);
    for (size_t u = pg_set_next(domain, 0); u != SIZE_MAX;
         u = pg_set_next(domain, u + 1)) {
      if (labels[u].rank <= label.rank) {
        pg_set_add(&safe, u);
      }
      if (labels[u].rank < label.rank) {
        pg_set_add(&lower, u);
      }
    }
    PGSet attracted = {0};
    assert(reference_attractor_through(game, domain, &safe, &lower, player,
                                       &attracted));
    expected[v] = pg_set_contains(&attracted, v);
    pg_set_destroy(&attracted);
  }
  pg_set_destroy(&safe);
  pg_set_destroy(&lower);
}

static void compare(PGGame const *game, PGSet const *domain,
                    PGPlayer const player, ADReachLabel const *labels,
                    char const *backend, bool const dirty) {
  assert(setenv("STRAHLER_AD_BACKEND", backend, 1) == 0);
  ADReach *work = ad_reach_create(game, domain, player);
  assert(work);
  memcpy(ad_reach_labels(work), labels, game->vertex_count * sizeof(*labels));
  unsigned char *expected = malloc(game->vertex_count);
  assert(expected);
  for (unsigned snapshot = 0; snapshot < 2; snapshot++) {
    oracle(game, domain, player, ad_reach_labels(work), expected);
    ad_reach_compute(work);
    assert(memcmp(expected, ad_reach_good(work), game->vertex_count) == 0);
  }
  ADReachStats const stats = ad_reach_stats(work);
  assert(stats.snapshots == 2);
  if (dirty) {
    assert(stats.dirty_visits > 0);
  }
  if (strcmp(backend, "byte") == 0 && game->vertex_count < 256) {
    assert(stats.byte_snapshots == 2);
  }
  free(expected);
  ad_reach_destroy(work);
}

static void random_snapshots(void) {
  char const *backends[] = {"fused",  "packed", "dense",  "queue",
                            "queue1", "dirty",  "dirty1", "byte"};
  ADReachLabel const positions[] = {{0, 3, AD_REACH_REGULAR},
                                    {1, 0, AD_REACH_LAZY},
                                    {4, 7, AD_REACH_REGULAR},
                                    {5, 0, AD_REACH_LAZY},
                                    {10, 0, AD_REACH_TOP}};
  for (unsigned sample = 0; sample < 32; sample++) {
    size_t const n = 64 + random_value() % 97;
    PGGame game = graph(n, false);
    PGSet domain = {0};
    assert(pg_set_init(&domain, n));
    pg_set_fill(&domain);
    ADReachLabel *labels = malloc(n * sizeof(*labels));
    assert(labels);
    for (size_t v = 0; v < n; v++) {
      labels[v] = positions[random_value() % 5];
    }
    for (unsigned player = 0; player < 2; player++) {
      for (unsigned b = 0; b < sizeof(backends) / sizeof(*backends); b++) {
        compare(&game, &domain, (PGPlayer)player, labels, backends[b], false);
      }
    }
    free(labels);
    pg_set_destroy(&domain);
    pg_game_destroy(&game);
  }
}

static void chain_and_dictionary(void) {
  PGGame game = graph(265, true);
  PGSet domain = {0};
  assert(pg_set_init(&domain, 265));
  pg_set_fill(&domain);
  ADReachLabel labels[265];
  for (size_t v = 0; v < 265; v++) {
    labels[v] = (ADReachLabel){10, 0, AD_REACH_LAZY};
  }
  labels[264] = (ADReachLabel){0, 1, AD_REACH_REGULAR};
  compare(&game, &domain, PG_EVEN, labels, "dirty", true);
  compare(&game, &domain, PG_EVEN, labels, "queue", false);
  compare(&game, &domain, PG_EVEN, labels, "queue1", false);
  compare(&game, &domain, PG_EVEN, labels, "dirty1", true);
  /* 254 regular symbols + one lazy rank = exactly 256 symbols. Its good
   * symbol is 254 and its pending symbol occupies byte slot 255. */
  for (size_t v = 0; v < 265; v++) {
    size_t const rank = v < 254 ? v : 254;
    labels[v] = (ADReachLabel){rank * 1000000, (rank + 1) * 1000000,
                               rank < 254 ? AD_REACH_REGULAR : AD_REACH_LAZY};
  }
  assert(setenv("STRAHLER_AD_BACKEND", "byte", 1) == 0);
  ADReach *work = ad_reach_create(&game, &domain, PG_EVEN);
  assert(work);
  memcpy(ad_reach_labels(work), labels, sizeof(labels));
  ad_reach_compute(work);
  assert(ad_reach_stats(work).symbols == 256 &&
         ad_reach_stats(work).byte_snapshots == 1);
  ad_reach_labels(work)[253] =
      (ADReachLabel){255000000, 300000000, AD_REACH_REGULAR};
  ad_reach_compute(work); /* a regular cut after all 256 allocated symbols */
  assert(ad_reach_good(work)[253]);
  ad_reach_labels(work)[0] = (ADReachLabel){SIZE_MAX - 2, 0, AD_REACH_TOP};
  ad_reach_compute(work); /* dictionary changed; 256 still fits */
  assert(ad_reach_stats(work).byte_snapshots == 3);
  ad_reach_labels(work)[1] = (ADReachLabel){1000000, 0, AD_REACH_LAZY};
  ad_reach_compute(work);
  assert(ad_reach_stats(work).byte_fallbacks == 1);
  ad_reach_destroy(work);
  pg_set_clear(&domain);
  compare(&game, &domain, PG_EVEN, labels, "dirty", false);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static void boundaries(void) {
  FILE *file = tmpfile();
  assert(file);
  /* t, h, u, v, w, x, a closed cycle, and regular boundary tests. */
  assert(fputs("parity 11;\n0 0 0 0;\n1 0 0 1;\n2 0 0 2,0;\n3 0 1 0,2;\n4 0 1 "
               "0,1;\n5 0 1 0,5;\n6 0 0 7;\n7 0 0 6;\n8 0 0 0;\n9 0 0 2;\n10 0 "
               "0 1,0;\n11 0 0 11;\n",
               file) >= 0);
  assert(fseek(file, 0, SEEK_SET) == 0);
  PGGame game = {0};
  PGParseError error = {0};
  assert(pg_game_read(file, &game, &error));
  assert(fclose(file) == 0);
  PGSet domain = {0};
  assert(pg_set_init(&domain, 12));
  pg_set_fill(&domain);
  ADReachLabel labels[12] = {
      {0, 1, AD_REACH_REGULAR},  {20, 0, AD_REACH_TOP},
      {10, 0, AD_REACH_LAZY},    {10, 0, AD_REACH_LAZY},
      {10, 0, AD_REACH_LAZY},    {10, 0, AD_REACH_LAZY},
      {10, 0, AD_REACH_LAZY},    {10, 0, AD_REACH_LAZY},
      {3, 10, AD_REACH_REGULAR}, {3, 10, AD_REACH_REGULAR},
      {3, 20, AD_REACH_REGULAR}, {20, 0, AD_REACH_TOP}};
  for (unsigned player = 0; player < 2; player++) {
    for (size_t v = 0; v < 12; v++) {
      game.vertices[v].owner =
          (PGPlayer)((v == 3 || v == 4 || v == 5 || v == 10) ? 1 - player
                                                             : player);
    }
    assert(setenv("STRAHLER_AD_BACKEND", "dirty", 1) == 0);
    ADReach *work = ad_reach_create(&game, &domain, (PGPlayer)player);
    assert(work);
    memcpy(ad_reach_labels(work), labels, sizeof(labels));
    ad_reach_compute(work);
    unsigned char const *good = ad_reach_good(work);
    assert(good[2] && good[3] && !good[4] && !good[5] && !good[6] && !good[7]);
    assert(good[8] && !good[9] && !good[10] && good[11]);
    ad_reach_destroy(work);
    pg_set_remove(&domain, 1);
    compare(&game, &domain, (PGPlayer)player, labels, "dirty", false);
    compare(&game, &domain, (PGPlayer)player, labels, "byte", false);
    work = ad_reach_create(&game, &domain, (PGPlayer)player);
    assert(work);
    memcpy(ad_reach_labels(work), labels, sizeof(labels));
    ad_reach_compute(work);
    assert(ad_reach_good(work)[4] && !ad_reach_good(work)[1]);
    ad_reach_labels(work)[2] = (ADReachLabel){20, 0, AD_REACH_TOP};
    ad_reach_compute(work);
    assert(!ad_reach_good(work)[3]);
    ad_reach_destroy(work);
    pg_set_add(&domain, 1);
  }
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

static double median3(double values[3]) {
  for (unsigned i = 0; i < 3; i++) {
    for (unsigned j = i + 1; j < 3; j++) {
      if (values[j] < values[i]) {
        double const t = values[i];
        values[i] = values[j];
        values[j] = t;
      }
    }
  }
  return values[1];
}

static void benchmark(bool const chain) {
  size_t const n = 8192;
  PGGame game = graph(n, chain);
  PGSet domain = {0};
  assert(pg_set_init(&domain, n));
  pg_set_fill(&domain);
  ADReachLabel *labels = malloc(n * sizeof(*labels));
  unsigned char *expected = malloc(n);
  assert(labels && expected);
  ADReachLabel const positions[] = {{0, 3, AD_REACH_REGULAR},
                                    {1, 0, AD_REACH_LAZY},
                                    {4, 7, AD_REACH_REGULAR},
                                    {5, 0, AD_REACH_LAZY},
                                    {10, 0, AD_REACH_TOP}};
  for (size_t v = 0; v < n; v++) {
    labels[v] = chain ? (ADReachLabel){10, 0, AD_REACH_LAZY}
                      : positions[random_value() % 5];
  }
  if (chain) {
    labels[n - 1] = (ADReachLabel){0, 1, AD_REACH_REGULAR};
  }
  assert(setenv("STRAHLER_AD_BACKEND", "fused", 1) == 0);
  ADReach *reference = ad_reach_create(&game, &domain, PG_EVEN);
  assert(reference);
  memcpy(ad_reach_labels(reference), labels, n * sizeof(*labels));
  ad_reach_compute(reference);
  memcpy(expected, ad_reach_good(reference), n);
  ad_reach_destroy(reference);
  char const *backends[] = {"fused", "packed", "dense",
                            "queue", "dirty",  "byte"};
  double build[6][3] = {0}, solve[6][3] = {0};
  ADReachStats counters[6] = {0};
  for (unsigned repeat = 0; repeat < 4; repeat++) {
    for (unsigned arm = 0; arm < 6; arm++) {
      unsigned const b = repeat % 2 ? 5 - arm : arm;
      assert(setenv("STRAHLER_AD_BACKEND", backends[b], 1) == 0);
      clock_t const start = clock();
      ADReach *work = ad_reach_create(&game, &domain, PG_EVEN);
      assert(work);
      memcpy(ad_reach_labels(work), labels, n * sizeof(*labels));
      clock_t const built = clock();
      ad_reach_compute(work);
      clock_t const done = clock();
      assert(memcmp(expected, ad_reach_good(work), n) == 0);
      counters[b] = ad_reach_stats(work);
      if (repeat != 0) {
        build[b][repeat - 1] = (double)(built - start) / CLOCKS_PER_SEC;
        solve[b][repeat - 1] = (double)(done - built) / CLOCKS_PER_SEC;
      }
      ad_reach_destroy(work);
    }
  }
  for (unsigned b = 0; b < 6; b++) {
    double const layout = median3(build[b]), closure = median3(solve[b]);
    printf(
        "snapshot fixture=%s backend=%s vertices=%zu edges=%zu build_cpu=%.6f "
        "closure_cpu=%.6f build_spread=%.6f closure_spread=%.6f sweeps=%zu "
        "dirty_visits=%zu padded=%zu samples=3\n",
        chain ? "chain" : "mixed-degree", backends[b], n, game.edge_count,
        layout, closure, build[b][2] - build[b][0], solve[b][2] - solve[b][0],
        counters[b].sweeps, counters[b].dirty_visits, counters[b].padded_edges);
  }
  free(labels);
  free(expected);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
}

int main(int const argc, char **argv) {
  (void)argv;
  if (argc > 1) {
    benchmark(true);
    benchmark(false);
    return 0;
  }
  boundaries();
  random_snapshots();
  chain_and_dictionary();
  assert(unsetenv("STRAHLER_AD_BACKEND") == 0);
  printf("AD reachability: %s; scalar and SIMD frozen snapshots agree\n",
         lift_isa_name(lift_isa()));
  if (lift_isa() != LIFT_AVX2) {
    puts("SKIP AVX2: unsupported CPU; portable/NEON paths checked");
  }
  return 0;
}
