#ifndef PG_PREPROCESS_H
#define PG_PREPROCESS_H 1

#include "pg_attractor.h"

typedef enum {
  PG_SHAVE_SINGLE,
  PG_SHAVE_LOOPS,
  PG_SHAVE_WCWC,
  PG_SHAVE_RULE_COUNT,
} PGShaveRule;

enum { PG_SHAVE_ALL = (1U << PG_SHAVE_RULE_COUNT) - 1 };

typedef struct {
  unsigned shave;
  bool compress;
} PGPreprocessOptions;

typedef struct {
  PGSet active;
  PGSet won[2];
  /* Counts include attraction; each vertex is charged to its first rule. */
  size_t shaved[PG_SHAVE_RULE_COUNT][2];
  PGPriorityMap priority_map;
} PGPreprocessResult;

/* Shared parser for the long options "initial-shave" and "compress". Invalid
 * input leaves options unchanged. all/none must appear alone. */
[[nodiscard]] bool pg_preprocess_parse_option(PGPreprocessOptions *options,
                                              char const *name,
                                              char const *value);

/* Renumber (or compress) priorities, then shave single/loops/wcwc to a
 * fixpoint. Vertex indices and edges stay unchanged; active is the remaining
 * game (after preprocessing and earlier shaving rounds), a total subgame;
 * won[p] records every shaved winner. On success game holds
 * the new priorities. A compressed map is not invertible; original_bound
 * displays the largest source priority merged into a level. */
[[nodiscard]] bool pg_preprocess(PGGame *game,
                                 PGPreprocessOptions const *options,
                                 PGPreprocessResult *result);
void pg_preprocess_destroy(PGPreprocessResult *result);

/* Check the partition, totality of the remaining game, winner-attractor
 * closure, and that neither loser can force an exit from its opponent's shaved
 * region in the full game. Seed parity correctness is established by the
 * shaving rules. */
[[nodiscard]] bool pg_preprocess_verify(PGGame const *game,
                                        PGPreprocessResult const *result);

bool pg_preprocess_priority_map(PGGame const *game, bool compress,
                                PGPriorityMap *map);

#endif
