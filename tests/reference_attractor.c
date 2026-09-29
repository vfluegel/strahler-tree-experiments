#include "reference_attractor.h"

[[nodiscard]] static PGSet fixed_point(PGGame const *game, PGSet const *active,
                                       PGSet const *safe, PGSet const *target,
                                       PGPlayer const player, bool *valid) {
  PGSet result = {0};
  *valid = pg_set_clone(&result, target);
  bool changed = *valid;
  while (changed) {
    changed = false;
    for (size_t vertex = pg_set_next(safe, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(safe, vertex + 1)) {
      if (pg_set_contains(&result, vertex)) {
        continue;
      }
      bool any = false;
      bool all = true;
      for (size_t edge = game->succ_offsets[vertex];
           edge < game->succ_offsets[vertex + 1]; edge++) {
        size_t const successor = game->successors[edge];
        if (!pg_set_contains(active, successor)) {
          continue;
        }
        if (pg_set_contains(&result, successor)) {
          any = true;
        } else {
          all = false;
        }
      }
      bool const add = game->vertices[vertex].owner == player ? any : all;
      if (add) {
        pg_set_add(&result, vertex);
        changed = true;
      }
    }
  }
  return result;
}

bool reference_attractor(PGGame const *game, PGSet const *active,
                         PGSet const *target, PGPlayer const player,
                         PGSet *result) {
  if (game == nullptr || active == nullptr || target == nullptr ||
      result == nullptr || player > PG_ODD ||
      active->bit_count != game->vertex_count ||
      target->bit_count != game->vertex_count ||
      !pg_set_subset(target, active) || !pg_subgame_is_total(game, active)) {
    return false;
  }
  bool valid = false;
  PGSet computed = fixed_point(game, active, active, target, player, &valid);
  pg_set_move(result, &computed);
  return valid;
}

bool reference_attractor_through(PGGame const *game, PGSet const *active,
                                 PGSet const *safe, PGSet const *target,
                                 PGPlayer const player, PGSet *result) {
  if (game == nullptr || active == nullptr || safe == nullptr ||
      target == nullptr || result == nullptr || player > PG_ODD ||
      active->bit_count != game->vertex_count ||
      safe->bit_count != game->vertex_count ||
      target->bit_count != game->vertex_count || !pg_set_subset(target, safe) ||
      !pg_set_subset(safe, active) || !pg_subgame_is_total(game, active)) {
    return false;
  }
  bool valid = false;
  PGSet computed = fixed_point(game, active, safe, target, player, &valid);
  pg_set_move(result, &computed);
  return valid;
}
