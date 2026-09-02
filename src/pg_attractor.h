#ifndef PG_ATTRACTOR_H
#define PG_ATTRACTOR_H 1

#include "pg_game.h"
#include "pg_set.h"

[[nodiscard]]
bool pg_subgame_is_total(PGGame const *game, PGSet const *active);

[[nodiscard]]
bool pg_attractor(PGGame const *game, PGSet const *active, PGSet const *target,
                  PGPlayer player, PGSet *result);

#endif
