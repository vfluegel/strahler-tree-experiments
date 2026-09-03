#ifndef PG_GAME_H
#define PG_GAME_H 1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
  PG_EVEN = 0,
  PG_ODD = 1,
} PGPlayer;

typedef struct {
  uint64_t external_id;
  uint64_t priority;
  PGPlayer owner;
  char *name;
} PGVertex;

typedef struct {
  size_t vertex_count;
  size_t edge_count;
  uint64_t max_priority;

  PGVertex *vertices;

  size_t *succ_offsets;
  size_t *successors;

  size_t *pred_offsets;
  size_t *predecessors;
} PGGame;

typedef struct {
  size_t line;
  size_t column;
  char message[256];
} PGParseError;

typedef struct {
  uint64_t original;
  uint64_t compact;
} PGPriorityMapEntry;

typedef struct {
  PGPriorityMapEntry *entries;
  size_t count;
} PGPriorityMap;

[[nodiscard]]
bool pg_game_read(FILE *input, PGGame *game, PGParseError *error);

void pg_game_destroy(PGGame *game);

[[nodiscard]]
bool pg_game_write_pgsolver(FILE *out, PGGame const *game, bool include_names);

/* Build the smallest strictly increasing priority map that preserves the
 * parity and identity of every distinct priority. */
[[nodiscard]]
bool pg_priority_map_build(PGGame const *game, PGPriorityMap *map);

void pg_priority_map_destroy(PGPriorityMap *map);

[[nodiscard]]
bool pg_priority_map_apply(PGPriorityMap const *map, PGGame *game);

[[nodiscard]]
bool pg_priority_map_restore(PGPriorityMap const *map, PGGame *game);

/* Translate a compact decomposition bound to the corresponding bound on the
 * input priority scale. Bounds for empty intervening levels are supported. */
[[nodiscard]]
bool pg_priority_map_original_bound(PGPriorityMap const *map,
                                    uint64_t compact_bound,
                                    uint64_t *original_bound);

#endif
