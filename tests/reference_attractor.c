#include "reference_attractor.h"

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
  PGSet fixed_point = {0};
  if (!pg_set_clone(&fixed_point, target)) {
    return false;
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(active, vertex + 1)) {
      if (pg_set_contains(&fixed_point, vertex)) {
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
        if (pg_set_contains(&fixed_point, successor)) {
          any = true;
        } else {
          all = false;
        }
      }
      bool const add = game->vertices[vertex].owner == player ? any : all;
      if (add) {
        pg_set_add(&fixed_point, vertex);
        changed = true;
      }
    }
  }
  pg_set_move(result, &fixed_point);
  return true;
}
