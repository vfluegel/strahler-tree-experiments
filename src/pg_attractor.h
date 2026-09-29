#ifndef PG_ATTRACTOR_H
#define PG_ATTRACTOR_H 1

#include "pg_game.h"
#include "pg_set.h"

[[nodiscard]]
bool pg_subgame_is_total(PGGame const *game, PGSet const *active);

/* Ordinary attractor in the total subgame induced by active. */
[[nodiscard]]
bool pg_attractor(PGGame const *game, PGSet const *active, PGSet const *target,
                  PGPlayer player, PGSet *result);

/* Largest subset of safe from which player can force a visit to target
 * without leaving safe first, in the game induced by active. Target vertices
 * count as reached immediately, whatever their successors. Unlike
 * pg_attractor(game, safe, ...), an opponent edge from safe to active \ safe
 * is not deleted: it lets the opponent escape and so blocks attraction.
 *
 * Requires target ⊆ safe ⊆ active. The caller guarantees that active induces
 * a total subgame; safe need not. Totality is not rechecked here because the
 * lifting algorithm calls this once per occupied position and round. */
[[nodiscard]]
bool pg_attractor_through(PGGame const *game, PGSet const *active,
                          PGSet const *safe, PGSet const *target,
                          PGPlayer player, PGSet *result);

#endif
