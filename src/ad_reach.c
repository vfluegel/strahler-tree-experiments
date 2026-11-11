#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ad_reach.h"

typedef struct {
  size_t vertex, degree;
} Source;
typedef struct {
  size_t offset, degree;
} Block;
typedef enum { FUSED, DENSE, QUEUE, DIRTY, BYTE } Completion;

struct ADReach {
  PGGame const *game;
  PGSet const *domain;
  PGPlayer player;
  ADReachLabel *labels;
  unsigned char *good;
  size_t *queue, *pending;
  size_t count, slots, blocks;
  size_t *original;
  int *packed, *edges, *keys, *limits, *owned;
  unsigned char *queued;
  Block *block;
  LiftBlock update;
  Completion completion;
  unsigned sweep_limit;
  ADReachStats stats;
  unsigned char *byte_keys;
};

bool ad_reach_scalar_requested(void) {
  char const *value = getenv("STRAHLER_AD_BACKEND");
  return value != nullptr && strcmp(value, "scalar") == 0;
}

ADReachLabel *ad_reach_labels(ADReach *work) { return work->labels; }
unsigned char const *ad_reach_good(ADReach const *work) { return work->good; }
ADReachStats ad_reach_stats(ADReach const *work) { return work->stats; }

void ad_reach_destroy(ADReach *work) {
  if (work == nullptr) {
    return;
  }
  if (getenv("STRAHLER_BACKEND_REPORT") != nullptr) {
    fprintf(stderr,
            "AD reach %s: %zu snapshots, %zu sweeps, %zu queue switches, "
            "%zu dirty visits, %zu byte snapshots, %zu byte fallbacks, %zu "
            "symbols, "
            "%zu real / %zu padded edges, %.6f layout s, %.6f dictionary s\n",
            lift_isa_name(work->stats.isa), work->stats.snapshots,
            work->stats.sweeps, work->stats.queue_switches,
            work->stats.dirty_visits, work->stats.byte_snapshots,
            work->stats.byte_fallbacks, work->stats.symbols,
            work->stats.real_edges, work->stats.padded_edges,
            work->stats.layout_seconds, work->stats.dictionary_seconds);
  }
  free(work->labels);
  free(work->good);
  free(work->queue);
  free(work->pending);
  free(work->original);
  free(work->packed);
  free(work->edges);
  free(work->keys);
  free(work->limits);
  free(work->owned);
  free(work->block);
  free(work->queued);
  free(work->byte_keys);
  free(work);
}

static int degree_order(void const *a, void const *b) {
  Source const *left = a, *right = b;
  if (left->degree != right->degree) {
    return left->degree < right->degree ? -1 : 1;
  }
  return (left->vertex > right->vertex) - (left->vertex < right->vertex);
}

/* This layout depends only on the induced domain. Duplicate real edges stay
 * duplicate; padding repeats the last edge and never enters pending counts. */
static bool layout(ADReach *work) {
  size_t const n = pg_set_count(work->domain), total = work->game->vertex_count;
  if (n == 0 || n > INT_MAX - 8 || n > SIZE_MAX / sizeof(Source) ||
      total > SIZE_MAX / sizeof(int)) {
    return false;
  }
  size_t const slots = (n + 7) / 8 * 8;
  Source *source = malloc(n * sizeof(*source));
  work->original = malloc(n * sizeof(*work->original));
  work->packed = malloc(total * sizeof(*work->packed));
  work->keys = calloc(slots, sizeof(*work->keys));
  work->limits = malloc(slots * sizeof(*work->limits));
  work->owned = calloc(slots, sizeof(*work->owned));
  work->block = calloc(slots / 8, sizeof(*work->block));
  work->queued = calloc(slots / 8, 1);
  if (source == nullptr || work->original == nullptr ||
      work->packed == nullptr || work->keys == nullptr ||
      work->limits == nullptr || work->owned == nullptr ||
      work->block == nullptr || work->queued == nullptr) {
    free(source);
    return false;
  }
  for (size_t v = 0; v < total; v++) {
    work->packed[v] = -1;
  }
  size_t used = 0;
  PGGame const *game = work->game;
  for (size_t v = pg_set_next(work->domain, 0); v != SIZE_MAX;
       v = pg_set_next(work->domain, v + 1)) {
    size_t degree = 0;
    for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1]; e++) {
      degree += pg_set_contains(work->domain, game->successors[e]);
    }
    if (degree == 0) {
      free(source);
      return false;
    }
    source[used++] = (Source){v, degree};
    work->stats.real_edges += degree;
  }
  qsort(source, n, sizeof(*source), degree_order);
  for (size_t i = 0; i < n; i++) {
    work->original[i] = source[i].vertex;
    work->packed[source[i].vertex] = (int)i;
    work->owned[i] =
        game->vertices[source[i].vertex].owner == work->player ? -1 : 0;
  }
  size_t offset = 0;
  for (size_t first = 0; first < n; first += 8) {
    size_t const end = first + 8 < n ? first + 8 : n;
    size_t const degree = source[end - 1].degree;
    if (degree > (SIZE_MAX / sizeof(int) - offset) / 8) {
      free(source);
      return false;
    }
    work->block[first / 8] = (Block){offset, degree};
    offset += 8 * degree;
  }
  work->edges = malloc(offset * sizeof(*work->edges));
  if (work->edges == nullptr) {
    free(source);
    return false;
  }
  for (size_t i = 0; i < slots; i++) {
    size_t const v = source[i < n ? i : n - 1].vertex;
    Block const block = work->block[i / 8];
    size_t filled = 0;
    int last = 0;
    for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1]; e++) {
      size_t const u = game->successors[e];
      if (work->packed[u] >= 0) {
        last = work->packed[u] - (int)(i / 8 * 8);
        work->edges[block.offset + i % 8 + 8 * filled++] = last;
      }
    }
    while (filled < block.degree) {
      work->edges[block.offset + i % 8 + 8 * filled++] = last;
    }
  }
  work->count = n;
  work->slots = slots;
  work->blocks = slots / 8;
  work->stats.padded_edges = offset;
  free(source);
  return true;
}

ADReach *ad_reach_create(PGGame const *game, PGSet const *domain,
                         PGPlayer const player) {
  return ad_reach_create_backend(game, domain, player,
                                 getenv("STRAHLER_AD_BACKEND"));
}

ADReach *ad_reach_create_backend(PGGame const *game, PGSet const *domain,
                                 PGPlayer const player, char const *choice) {
  bool const profile = getenv("STRAHLER_BACKEND_REPORT") != nullptr;
  clock_t const start = profile ? clock() : 0;
  size_t const n = game->vertex_count == 0 ? 1 : game->vertex_count;
  if (n > SIZE_MAX / sizeof(ADReachLabel)) {
    return nullptr;
  }
  ADReach *work = calloc(1, sizeof(*work));
  if (work == nullptr) {
    return nullptr;
  }
  work->game = game;
  work->domain = domain;
  work->player = player;
  work->labels = calloc(n, sizeof(*work->labels));
  work->good = calloc(n, 1);
  work->queue = malloc(n * sizeof(*work->queue));
  work->pending = malloc(n * sizeof(*work->pending));
  if (work->labels == nullptr || work->good == nullptr ||
      work->queue == nullptr || work->pending == nullptr) {
    ad_reach_destroy(work);
    return nullptr;
  }
  work->completion = DIRTY;
  work->sweep_limit = 8;
  LiftISA isa = lift_isa();
  bool const automatic = choice == nullptr || strcmp(choice, "auto") == 0;
  if (automatic && isa == LIFT_PORTABLE) {
    work->completion = FUSED;
  }
  if (choice != nullptr) {
    if (strcmp(choice, "fused") == 0 || strcmp(choice, "scalar") == 0) {
      work->completion = FUSED;
    } else if (strcmp(choice, "packed") == 0) {
      isa = LIFT_PORTABLE;
    } else if (strcmp(choice, "dense") == 0) {
      work->completion = DENSE;
    } else if (strcmp(choice, "queue") == 0) {
      work->completion = QUEUE;
    } else if (strcmp(choice, "queue1") == 0) {
      work->completion = QUEUE;
      work->sweep_limit = 1;
    } else if (strcmp(choice, "dirty1") == 0) {
      work->sweep_limit = 1;
    } else if (strcmp(choice, "byte") == 0) {
      work->completion = BYTE;
    }
  }
  work->update = lift_block_portable;
#if defined(__x86_64__)
  if (isa == LIFT_AVX2) {
    work->update = lift_block_avx2;
  }
#elif defined(__aarch64__)
  if (isa == LIFT_NEON) {
    work->update = lift_block_neon;
  }
#endif
  if (work->completion != FUSED &&
      ((automatic && pg_set_count(domain) < 64) || !layout(work))) {
    work->completion = FUSED;
  }
  work->stats.isa = work->completion == FUSED ? LIFT_PORTABLE : isa;
  if (work->completion == BYTE) {
    work->byte_keys = malloc(n);
    if (work->byte_keys == nullptr) {
      work->completion = DIRTY;
    }
  }
  if (profile) {
    work->stats.layout_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
  }
  return work;
}

/* All lazy fibres share a queue. A higher successor remains a permanent
 * blocker for an opponent, even though it is outside that fibre's safe set. */
static void fused(ADReach *work) {
  PGGame const *game = work->game;
  size_t head = 0, tail = 0;
  for (size_t v = pg_set_next(work->domain, 0); v != SIZE_MAX;
       v = pg_set_next(work->domain, v + 1)) {
    ADReachLabel const label = work->labels[v];
    if (label.kind == AD_REACH_TOP) {
      work->good[v] = 1;
      continue;
    }
    bool const owned = game->vertices[v].owner == work->player;
    bool lower = false;
    size_t pending = 0;
    for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1]; e++) {
      size_t const u = game->successors[e];
      if (!pg_set_contains(work->domain, u)) {
        continue;
      }
      bool const pass =
          work->labels[u].rank <
          (label.kind == AD_REACH_REGULAR ? label.after : label.rank);
      if (pass) {
        lower = true;
        if (owned) {
          break;
        }
      } else {
        pending++;
        if (!owned && label.kind == AD_REACH_REGULAR) {
          break;
        }
      }
    }
    work->good[v] =
        label.kind == AD_REACH_TOP || (owned ? lower : pending == 0);
    work->pending[v] = pending;
    if (work->good[v] && label.kind == AD_REACH_LAZY) {
      work->queue[tail++] = v;
    }
  }
  while (head < tail) {
    size_t const u = work->queue[head++];
    for (size_t e = game->pred_offsets[u]; e < game->pred_offsets[u + 1]; e++) {
      size_t const v = game->predecessors[e];
      if (!pg_set_contains(work->domain, v) || work->good[v] ||
          work->labels[v].rank != work->labels[u].rank ||
          work->labels[v].kind != AD_REACH_LAZY) {
        continue;
      }
      if (game->vertices[v].owner != work->player && --work->pending[v] != 0) {
        continue;
      }
      work->good[v] = 1;
      work->queue[tail++] = v;
    }
  }
}

static void scalar_tail(ADReach *work) {
  PGGame const *game = work->game;
  size_t head = 0, tail = 0;
  for (size_t i = 0; i < work->count; i++) {
    size_t const v = work->original[i];
    if (work->labels[v].kind != AD_REACH_LAZY) {
      continue;
    }
    if ((work->keys[i] & 1) == 0) {
      work->queue[tail++] = v;
    } else if (!work->owned[i]) {
      size_t pending = 0;
      for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1];
           e++) {
        size_t const u = game->successors[e];
        pending += pg_set_contains(work->domain, u) &&
                   work->labels[u].rank >= work->labels[v].rank;
      }
      work->pending[v] = pending;
    }
  }
  /* Counts include equal successors already good: replay each exactly once. */
  while (head < tail) {
    size_t const u = work->queue[head++];
    for (size_t e = game->pred_offsets[u]; e < game->pred_offsets[u + 1]; e++) {
      size_t const v = game->predecessors[e];
      int const i = work->packed[v];
      if (i < 0 || (work->keys[i] & 1) == 0 ||
          work->labels[v].kind != AD_REACH_LAZY ||
          work->labels[v].rank != work->labels[u].rank) {
        continue;
      }
      if (!work->owned[i] && --work->pending[v] != 0) {
        continue;
      }
      work->keys[i]--;
      work->queue[tail++] = v;
    }
  }
}

static unsigned update(ADReach *work, size_t const b) {
  Block const block = work->block[b];
  return work->update(work->keys + 8 * b, work->edges + block.offset,
                      block.degree, work->owned + 8 * b, work->limits + 8 * b);
}

static void dirty_tail(ADReach *work) {
  if (work->blocks == 0) {
    return;
  }
  size_t head = 0, tail = 0, queued = 0;
  for (size_t b = 0; b < work->blocks; b++) {
    work->queue[tail++] = b;
    work->queued[b] = 1;
    queued++;
  }
  tail %= work->blocks;
  while (queued != 0) {
    size_t const b = work->queue[head];
    head = (head + 1) % work->blocks;
    queued--;
    work->queued[b] = 0;
    unsigned const changed = update(work, b);
    work->stats.dirty_visits++;
    for (unsigned lane = 0; lane < 8; lane++) {
      size_t const i = 8 * b + lane;
      if ((changed & (1U << lane)) == 0 || i >= work->count) {
        continue;
      }
      size_t const u = work->original[i];
      if (work->labels[u].kind != AD_REACH_LAZY) {
        continue;
      }
      for (size_t e = work->game->pred_offsets[u];
           e < work->game->pred_offsets[u + 1]; e++) {
        size_t const v = work->game->predecessors[e];
        int const at = work->packed[v];
        if (at < 0 || (work->keys[at] & 1) == 0 ||
            work->labels[v].kind != AD_REACH_LAZY ||
            work->labels[v].rank != work->labels[u].rank) {
          continue;
        }
        size_t const next = (size_t)at / 8;
        if (!work->queued[next]) {
          work->queued[next] = 1;
          work->queue[tail] = next;
          tail = (tail + 1) % work->blocks;
          queued++;
        }
      }
    }
  }
}

typedef struct {
  size_t rank, begin;
  ADReachKind kind;
} Symbol;

static size_t symbol_at(Symbol const *symbols, size_t const count,
                        size_t const rank) {
  size_t first = 0, last = count;
  while (first < last) {
    size_t const mid = first + (last - first) / 2;
    if (symbols[mid].rank < rank) {
      first = mid + 1;
    } else {
      last = mid;
    }
  }
  return first;
}

static bool byte_passes(ADReach const *work, size_t const v) {
  if (work->labels[v].kind == AD_REACH_TOP) {
    return true;
  }
  bool const owned = work->game->vertices[v].owner == work->player;
  for (size_t e = work->game->succ_offsets[v];
       e < work->game->succ_offsets[v + 1]; e++) {
    size_t const u = work->game->successors[e];
    if (!pg_set_contains(work->domain, u)) {
      continue;
    }
    bool const pass = work->byte_keys[u] <= work->limits[work->packed[v]];
    if (pass == owned) {
      return owned;
    }
  }
  return !owned;
}

/* The count may be 256; codes alone are bytes. No global position ordinal,
 * outdegree, inactive sentinel or dictionary size is narrowed to a byte. */
static bool byte_snapshot(ADReach *work) {
  bool const profile = getenv("STRAHLER_BACKEND_REPORT") != nullptr;
  clock_t const start = profile ? clock() : 0;
  Symbol symbols[257];
  size_t count = 0, slots = 0;
  for (size_t i = 0; i < work->count; i++) {
    ADReachLabel const label = work->labels[work->original[i]];
    size_t const at = symbol_at(symbols, count, label.rank);
    if (at < count && symbols[at].rank == label.rank) {
      continue;
    }
    if (count == 256) {
      return false;
    }
    memmove(symbols + at + 1, symbols + at, (count - at) * sizeof(*symbols));
    symbols[at] = (Symbol){.rank = label.rank, .kind = label.kind};
    count++;
    slots += label.kind == AD_REACH_LAZY ? 2 : 1;
    if (slots > 256) {
      return false;
    }
  }
  size_t begin = 0;
  for (size_t i = 0; i < count; i++) {
    symbols[i].begin = begin;
    begin += symbols[i].kind == AD_REACH_LAZY ? 2 : 1;
  }
  work->stats.symbols =
      slots > work->stats.symbols ? slots : work->stats.symbols;
  work->stats.byte_snapshots++;
  for (size_t i = 0; i < work->count; i++) {
    size_t const v = work->original[i];
    ADReachLabel const label = work->labels[v];
    Symbol const symbol = symbols[symbol_at(symbols, count, label.rank)];
    work->byte_keys[v] =
        (unsigned char)(symbol.begin + (label.kind == AD_REACH_LAZY));
    if (label.kind == AD_REACH_REGULAR) {
      size_t const cut = symbol_at(symbols, count, label.after);
      size_t const code = cut == count ? slots : symbols[cut].begin;
      work->limits[i] = (int)code - 1;
    } else {
      work->limits[i] = (int)symbol.begin;
    }
  }
  size_t head = 0, tail = 0;
  if (profile) {
    work->stats.dictionary_seconds +=
        (double)(clock() - start) / CLOCKS_PER_SEC;
  }
  for (size_t i = 0; i < work->count; i++) {
    size_t const v = work->original[i];
    if (byte_passes(work, v)) {
      work->good[v] = 1;
      if (work->labels[v].kind == AD_REACH_LAZY) {
        work->byte_keys[v]--;
        work->queue[tail++] = v;
      }
    }
  }
  while (head < tail) {
    size_t const u = work->queue[head++];
    for (size_t e = work->game->pred_offsets[u];
         e < work->game->pred_offsets[u + 1]; e++) {
      size_t const v = work->game->predecessors[e];
      if (!pg_set_contains(work->domain, v) || work->good[v] ||
          work->labels[v].kind != AD_REACH_LAZY ||
          work->labels[v].rank != work->labels[u].rank) {
        continue;
      }
      if (byte_passes(work, v)) {
        work->good[v] = 1;
        work->byte_keys[v]--;
        work->queue[tail++] = v;
      }
    }
  }
  return true;
}

void ad_reach_compute(ADReach *work) {
  memset(work->good, 0, work->game->vertex_count);
  work->stats.snapshots++;
  if (work->completion == FUSED) {
    fused(work);
    return;
  }
  if (work->completion == BYTE) {
    if (byte_snapshot(work)) {
      return;
    }
    work->stats.byte_fallbacks++;
  }
  for (size_t i = 0; i < work->count; i++) {
    ADReachLabel const label = work->labels[work->original[i]];
    if (label.rank > (INT_MAX - 1) / 2 ||
        (label.kind == AD_REACH_REGULAR && label.after > (INT_MAX - 1) / 2)) {
      fused(work);
      return;
    }
    work->keys[i] = (int)(2 * label.rank + 1);
    work->limits[i] = label.kind == AD_REACH_TOP ? INT_MAX
                      : label.kind == AD_REACH_REGULAR
                          ? (int)(2 * label.after) - 1
                          : (int)(2 * label.rank);
  }
  for (size_t i = work->count; i < work->slots; i++) {
    work->keys[i] = 0;
    work->limits[i] = -1;
  }
  bool changed = true;
  unsigned sweeps = 0;
  while (changed && (work->completion == DENSE || sweeps < work->sweep_limit)) {
    changed = false;
    for (size_t b = 0; b < work->blocks; b++) {
      changed |= update(work, b) != 0;
    }
    sweeps++;
    work->stats.sweeps++;
  }
  if (changed) {
    if (work->completion == QUEUE) {
      work->stats.queue_switches++;
      scalar_tail(work);
    } else {
      dirty_tail(work);
    }
  }
  for (size_t i = 0; i < work->count; i++) {
    work->good[work->original[i]] = (work->keys[i] & 1) == 0;
  }
}
