#ifndef REFERENCE_SOLVER_H
#define REFERENCE_SOLVER_H 1

#include "pg_game.h"
#include "pg_set.h"

[[nodiscard]]
bool reference_solve(PGGame const *game, PGSet *even, PGSet *odd);

#endif
