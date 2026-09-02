#ifndef REFERENCE_ATTRACTOR_H
#define REFERENCE_ATTRACTOR_H 1

#include "pg_attractor.h"

[[nodiscard]]
bool reference_attractor(PGGame const *game, PGSet const *active,
                         PGSet const *target, PGPlayer player, PGSet *result);

#endif
