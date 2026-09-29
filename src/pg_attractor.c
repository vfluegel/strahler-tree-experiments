#include <stddef.h>
#include <stdlib.h>

#include "pg_attractor.h"

bool pg_subgame_is_total(PGGame const *game, PGSet const *active) {
  if (game == nullptr || active == nullptr ||
      active->bit_count != game->vertex_count) {
    return false;
  }
  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    bool found = false;
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      if (pg_set_contains(active, game->successors[edge])) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

bool pg_attractor(PGGame const *game, PGSet const *active, PGSet const *target,
                  PGPlayer const player, PGSet *result) {
  if (game == nullptr || active == nullptr || target == nullptr ||
      !pg_subgame_is_total(game, active)) {
    return false;
  }
  return pg_attractor_through(game, active, active, target, player, result);
}

bool pg_attractor_through(PGGame const *game, PGSet const *active,
                          PGSet const *safe, PGSet const *target,
                          PGPlayer const player, PGSet *result) {
  if (result == nullptr || player > PG_ODD || game == nullptr ||
      active == nullptr || safe == nullptr || target == nullptr ||
      active->bit_count != game->vertex_count ||
      safe->bit_count != game->vertex_count ||
      target->bit_count != game->vertex_count || !pg_set_subset(target, safe) ||
      !pg_set_subset(safe, active)) {
    return false;
  }

  /* Only safe \ target can still be attracted, so the work below is linear in
   * those candidates and their edges rather than in the whole target. */
  PGSet attracted = {0};
  PGSet candidates = {0};
  if (!pg_set_clone(&attracted, target) || !pg_set_clone(&candidates, safe)) {
    pg_set_destroy(&attracted);
    return false;
  }
  pg_set_subtract_into(&candidates, target);
  size_t const candidate_count = pg_set_count(&candidates);
  if (candidate_count == 0) {
    pg_set_destroy(&candidates);
    pg_set_move(result, &attracted);
    return true;
  }
  if (game->vertex_count > SIZE_MAX / sizeof(size_t)) {
    pg_set_destroy(&candidates);
    pg_set_destroy(&attracted);
    return false;
  }
  size_t *queue = malloc(candidate_count * sizeof(queue[0]));
  /* Indexed by vertex, but only candidate entries are written or read. */
  size_t *remaining = malloc(game->vertex_count * sizeof(remaining[0]));
  if (queue == nullptr || remaining == nullptr) {
    free(queue);
    free(remaining);
    pg_set_destroy(&candidates);
    pg_set_destroy(&attracted);
    return false;
  }

  /* A player vertex needs one successor already attracted. An opponent vertex
   * needs every active successor attracted; one outside safe never is, so its
   * counter never reaches zero. Duplicate edges are counted per edge, matching
   * the per-edge predecessor lists used below. */
  size_t head = 0;
  size_t tail = 0;
  for (size_t vertex = pg_set_next(&candidates, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(&candidates, vertex + 1)) {
    bool const owned = game->vertices[vertex].owner == player;
    size_t pending = 0;
    bool reached = false;
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      size_t const successor = game->successors[edge];
      if (!pg_set_contains(active, successor)) {
        continue;
      }
      if (pg_set_contains(target, successor)) {
        reached = true;
      } else {
        pending++;
      }
    }
    remaining[vertex] = pending;
    if (owned ? reached : pending == 0) {
      pg_set_add(&attracted, vertex);
      queue[tail++] = vertex;
    }
  }

  while (head < tail) {
    size_t const reached = queue[head++];
    for (size_t edge = game->pred_offsets[reached];
         edge < game->pred_offsets[reached + 1]; edge++) {
      size_t const predecessor = game->predecessors[edge];
      if (!pg_set_contains(&candidates, predecessor) ||
          pg_set_contains(&attracted, predecessor)) {
        continue;
      }
      if (game->vertices[predecessor].owner != player &&
          --remaining[predecessor] != 0) {
        continue;
      }
      pg_set_add(&attracted, predecessor);
      queue[tail++] = predecessor;
    }
  }

  free(queue);
  free(remaining);
  pg_set_destroy(&candidates);
  pg_set_move(result, &attracted);
  return true;
}
