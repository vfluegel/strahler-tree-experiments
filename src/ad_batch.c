#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_batch.h"
#include "lift_simd.h"
#include "pg_attractor.h"

static bool forced(PGGame const *game, ADBatchRequest const *request,
                   size_t const v) {
  return request->conditional && game->vertices[v].priority >= request->cutoff;
}

static uint64_t priority(PGGame const *game, ADBatchRequest const *request,
                         size_t const v) {
  uint64_t const original = game->vertices[v].priority;
  return forced(game, request, v)
             ? (original == request->cutoff ? request->cutoff % 2
                                            : 1 - request->cutoff % 2)
             : original;
}

bool ad_batch_arena(PGGame const *game, PGSet const *domain,
                    ADBatchRequest const *request, PGGame *arena) {
  *arena = (PGGame){0};
  size_t const n = game->vertex_count;
  if (n > SIZE_MAX / sizeof(PGVertex) || game->edge_count > SIZE_MAX - n ||
      game->edge_count + n >= SIZE_MAX / sizeof(size_t)) {
    return false;
  }
  arena->vertices = calloc(n == 0 ? 1 : n, sizeof(PGVertex));
  arena->succ_offsets = calloc(n + 1, sizeof(size_t));
  arena->pred_offsets = calloc(n + 1, sizeof(size_t));
  arena->successors = malloc((game->edge_count + n + 1) * sizeof(size_t));
  arena->predecessors = malloc((game->edge_count + n + 1) * sizeof(size_t));
  size_t *next = malloc((n == 0 ? 1 : n) * sizeof(size_t));
  arena->vertex_count = n;
  arena->start_id = game->start_id;
  arena->start_vertex = game->start_vertex;
  if (!arena->vertices || !arena->succ_offsets || !arena->pred_offsets ||
      !arena->successors || !arena->predecessors || !next) {
    free(next);
    pg_game_destroy(arena);
    return false;
  }
  for (size_t v = 0; v < n; v++) {
    arena->vertices[v] = game->vertices[v];
    arena->vertices[v].name = nullptr;
    arena->vertices[v].priority = priority(game, request, v);
    if (forced(game, request, v)) {
      arena->vertices[v].owner =
          arena->vertices[v].priority % 2 == 0 ? PG_EVEN : PG_ODD;
    }
    if (arena->vertices[v].priority > arena->max_priority) {
      arena->max_priority = arena->vertices[v].priority;
    }
    arena->succ_offsets[v] = arena->edge_count;
    if (forced(game, request, v)) {
      arena->successors[arena->edge_count++] = v;
    } else {
      for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1];
           e++) {
        size_t const u = game->successors[e];
        if (pg_set_contains(domain, u)) {
          arena->successors[arena->edge_count++] = u;
        }
      }
    }
  }
  arena->succ_offsets[n] = arena->edge_count;
  for (size_t e = 0; e < arena->edge_count; e++) {
    arena->pred_offsets[arena->successors[e] + 1]++;
  }
  for (size_t v = 0; v < n; v++) {
    arena->pred_offsets[v + 1] += arena->pred_offsets[v];
    next[v] = arena->pred_offsets[v];
  }
  for (size_t v = 0; v < n; v++) {
    for (size_t e = arena->succ_offsets[v]; e < arena->succ_offsets[v + 1];
         e++) {
      arena->predecessors[next[arena->successors[e]]++] = v;
    }
  }
  free(next);
  return true;
}

typedef struct {
  uint16_t rank[8], threshold[8];
  uint16_t pending[8];
  unsigned lazy, top, owned, forced, valid, waiting;
} Row;

static void install(Row *row, unsigned const lane, ADLiftTree const *host,
                    size_t const rank) {
  ADPosition const position = host->positions[rank];
  unsigned const bit = 1U << lane;
  row->rank[lane] = (uint16_t)rank;
  row->threshold[lane] = (uint16_t)(position.kind == AD_POS_REGULAR
                                        ? host->nodes[position.node].after
                                        : rank);
  row->lazy = (row->lazy & ~bit) |
              (rank != host->top && position.kind != AD_POS_REGULAR ? bit : 0);
  row->top = (row->top & ~bit) | (rank == host->top ? bit : 0);
}

static bool independent(PGGame const *game, PGSet const *domain,
                        ADBatchRequest const *requests, size_t const count,
                        ADLiftLabelling *results, ADLiftError *error) {
  for (size_t lane = 0; lane < count; lane++) {
    PGGame arena = {0};
    if (!ad_batch_arena(game, domain, requests + lane, &arena)) {
      return false;
    }
    bool const ok = ad_lift_run(&arena, domain, requests[lane].host,
                                AD_LIFT_SCHEDULE_ROUNDS, results + lane, error);
    pg_game_destroy(&arena);
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool ad_lift_batch(PGGame const *game, PGSet const *domain,
                   ADBatchRequest const *requests, size_t const count,
                   ADLiftLabelling *results, ADLiftError *error) {
  if (error != nullptr) {
    *error = (ADLiftError){0};
  }
  if (!game || !domain || !requests || !results || count == 0 || count > 8 ||
      domain->bit_count != game->vertex_count ||
      !pg_subgame_is_total(game, domain)) {
    return false;
  }
  memset(results, 0, count * sizeof(*results));
  size_t const n = game->vertex_count;
  for (size_t lane = 0; lane < count; lane++) {
    ADLiftTree const *host = requests[lane].host;
    if (!host || !host->positions || !host->nodes || host->player > PG_ODD ||
        host->top + 1 != host->position_count) {
      return false;
    }
  }
  bool narrow = n <= SIZE_MAX / sizeof(Row);
  for (size_t v = pg_set_next(domain, 0); narrow && v != SIZE_MAX;
       v = pg_set_next(domain, v + 1)) {
    narrow = game->succ_offsets[v + 1] - game->succ_offsets[v] <= UINT16_MAX;
  }
  for (size_t lane = 0; lane < count; lane++) {
    narrow &= requests[lane].host->top <= UINT16_MAX;
  }
  Row *rows = narrow ? calloc(n == 0 ? 1 : n, sizeof(*rows)) : nullptr;
  size_t *queue = narrow ? malloc((n == 0 ? 1 : n) * sizeof(*queue)) : nullptr;
  bool success = false;
  size_t *occupied[8] = {0}, groups[8] = {0};
  bool workspace = rows != nullptr && queue != nullptr;
  for (size_t lane = 0; workspace && lane < count; lane++) {
    occupied[lane] =
        calloc(requests[lane].host->position_count, sizeof(size_t));
    workspace &= occupied[lane] != nullptr;
  }
  if (!workspace) {
    success = independent(game, domain, requests, count, results, error);
    goto cleanup;
  }
  for (size_t lane = 0; lane < count; lane++) {
    results[lane].labels = malloc((n == 0 ? 1 : n) * sizeof(size_t));
    results[lane].vertex_count = n;
    results[lane].stats.max_position = SIZE_MAX;
    if (!results[lane].labels || !pg_set_init(&results[lane].dominion, n)) {
      goto cleanup;
    }
    for (size_t v = 0; v < n; v++) {
      results[lane].labels[v] = SIZE_MAX;
    }
    for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
         v = pg_set_next(domain, v + 1)) {
      unsigned const bit = 1U << lane;
      ADLiftTree const *host = requests[lane].host;
      install(rows + v, (unsigned)lane, host,
              ad_lift_tree_first_compatible(
                  host, 0, priority(game, requests + lane, v)));
      if ((rows[v].lazy & bit) && occupied[lane][rows[v].rank[lane]]++ == 0) {
        groups[lane]++;
      }
      if (game->vertices[v].owner == host->player) {
        rows[v].owned |= bit;
      }
      if (forced(game, requests + lane, v)) {
        rows[v].forced |= bit;
      }
    }
  }
  if (n == 0) {
    success = true;
    goto cleanup;
  }
  unsigned live = (1U << count) - 1;
  while (live != 0) {
    size_t head = 0, tail = 0, queued = 0;
    for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
         v = pg_set_next(domain, v + 1)) {
      Row *row = rows + v;
      row->valid = ~live | row->top | (row->forced & ~row->lazy);
      row->waiting = 0;
      unsigned const internal = live & ~row->top & ~row->forced;
      if (internal == 0) {
        continue;
      }
      unsigned any = 0;
      memset(row->pending, 0, sizeof(row->pending));
      for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1];
           e++) {
        size_t const u = game->successors[e];
        if (!pg_set_contains(domain, u)) {
          continue;
        }
        unsigned const lower =
            lift_batch_edge(rows[u].rank, row->threshold, row->pending);
        any |= lower;
      }
      row->valid |= internal & ((row->owned & any) |
                                (~row->owned & lift_batch_zero(row->pending)));
      row->waiting = live & row->lazy & row->valid;
      if (row->waiting) {
        queue[tail] = v;
        tail = (tail + 1) % n;
        queued++;
      }
    }
    while (queued != 0) {
      size_t const u = queue[head];
      head = (head + 1) % n;
      queued--;
      unsigned const newly = rows[u].waiting;
      rows[u].waiting = 0;
      for (size_t e = game->pred_offsets[u]; e < game->pred_offsets[u + 1];
           e++) {
        size_t const v = game->predecessors[e];
        if (!pg_set_contains(domain, v)) {
          continue;
        }
        Row *row = rows + v;
        unsigned const eligible =
            newly & live & row->lazy & ~row->valid & ~row->forced;
        if (!eligible) {
          continue;
        }
        unsigned const good = lift_batch_notify(
            row->rank, rows[u].rank, row->pending, eligible, row->owned);
        if (!good) {
          continue;
        }
        row->valid |= good;
        if (!row->waiting) {
          queue[tail] = v;
          tail = (tail + 1) % n;
          queued++;
        }
        row->waiting |= good;
      }
    }
    unsigned changed = 0;
    for (size_t lane = 0; lane < count; lane++) {
      if (!(live & (1U << lane))) {
        continue;
      }
      results[lane].stats.attractors += groups[lane];
    }
    for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
         v = pg_set_next(domain, v + 1)) {
      unsigned invalid = live & ~rows[v].valid;
      changed |= invalid;
      while (invalid != 0) {
        unsigned const lane = (unsigned)__builtin_ctz(invalid);
        invalid &= invalid - 1;
        unsigned const bit = 1U << lane;
        results[lane].stats.lifts++;
        if ((rows[v].lazy & bit) && --occupied[lane][rows[v].rank[lane]] == 0) {
          groups[lane]--;
        }
        install(rows + v, (unsigned)lane, requests[lane].host,
                ad_lift_tree_first_compatible(
                    requests[lane].host, (size_t)rows[v].rank[lane] + 1,
                    priority(game, requests + lane, v)));
        if ((rows[v].lazy & bit) && occupied[lane][rows[v].rank[lane]]++ == 0) {
          groups[lane]++;
        }
      }
    }
    for (size_t lane = 0; lane < count; lane++) {
      results[lane].stats.rounds += (changed >> lane) & 1U;
    }
    live = changed;
  }
  for (size_t lane = 0; lane < count; lane++) {
    ADLiftLabelling *result = results + lane;
    for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
         v = pg_set_next(domain, v + 1)) {
      size_t const rank = rows[v].rank[lane];
      result->labels[v] = rank;
      if (rank == requests[lane].host->top) {
        result->stats.top_count++;
      } else {
        pg_set_add(&result->dominion, v);
        if (result->stats.max_position == SIZE_MAX ||
            rank > result->stats.max_position) {
          result->stats.max_position = rank;
        }
      }
    }
  }
  success = true;
cleanup:
  for (size_t lane = 0; lane < count; lane++) {
    free(occupied[lane]);
  }
  free(rows);
  free(queue);
  if (!success) {
    for (size_t lane = 0; lane < count; lane++) {
      ad_lift_labelling_destroy(results + lane);
    }
    if (error != nullptr && error->message[0] == '\0') {
      (void)snprintf(error->message, sizeof(error->message),
                     "failed to allocate batch lifting state");
    }
  }
  return success;
}
