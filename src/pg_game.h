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

[[nodiscard]]
bool pg_game_read(FILE *input, PGGame *game, PGParseError *error);

void pg_game_destroy(PGGame *game);

[[nodiscard]]
bool pg_game_write_pgsolver(FILE *out, PGGame const *game, bool include_names);

#endif
