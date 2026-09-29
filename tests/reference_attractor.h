#ifndef REFERENCE_ATTRACTOR_H
#define REFERENCE_ATTRACTOR_H 1

#include "pg_attractor.h"

[[nodiscard]]
bool reference_attractor(PGGame const *game, PGSet const *active,
                         PGSet const *target, PGPlayer player, PGSet *result);

/* Naive fixed point for pg_attractor_through: a vertex of safe is added when
 * the player can move to, or the opponent must move to, the current set. */
[[nodiscard]]
bool reference_attractor_through(PGGame const *game, PGSet const *active,
                                 PGSet const *safe, PGSet const *target,
                                 PGPlayer player, PGSet *result);

#endif
