#include <stddef.h>
#include <stdlib.h>

#include "pg_attractor.h"

[[nodiscard]] static bool inputs_match(PGGame const *game, PGSet const *active,
                                       PGSet const *target) {
  return game != nullptr && active != nullptr && target != nullptr &&
         active->bit_count == game->vertex_count &&
         target->bit_count == game->vertex_count &&
         pg_set_subset(target, active);
}

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
  if (result == nullptr || player > PG_ODD ||
      !inputs_match(game, active, target) ||
      !pg_subgame_is_total(game, active)) {
    return false;
  }

  PGSet attracted = {0};
  if (!pg_set_clone(&attracted, target)) {
    return false;
  }
  size_t const count = game->vertex_count;
  if (count == 0) {
    pg_set_move(result, &attracted);
    return true;
  }
  if (count > SIZE_MAX / sizeof(size_t)) {
    pg_set_destroy(&attracted);
    return false;
  }
  size_t *queue = malloc(count * sizeof(queue[0]));
  size_t *remaining = calloc(count, sizeof(remaining[0]));
  if (queue == nullptr || remaining == nullptr) {
    free(queue);
    free(remaining);
    pg_set_destroy(&attracted);
    return false;
  }

  size_t head = 0;
  size_t tail = 0;
  for (size_t vertex = pg_set_next(target, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(target, vertex + 1)) {
    queue[tail++] = vertex;
  }
  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    if (game->vertices[vertex].owner == player) {
      continue;
    }
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      if (pg_set_contains(active, game->successors[edge])) {
        remaining[vertex]++;
      }
    }
  }

  while (head < tail) {
    size_t const reached = queue[head++];
    for (size_t edge = game->pred_offsets[reached];
         edge < game->pred_offsets[reached + 1]; edge++) {
      size_t const predecessor = game->predecessors[edge];
      if (!pg_set_contains(active, predecessor) ||
          pg_set_contains(&attracted, predecessor)) {
        continue;
      }
      bool add = game->vertices[predecessor].owner == player;
      if (!add) {
        if (remaining[predecessor] == 0) {
          free(queue);
          free(remaining);
          pg_set_destroy(&attracted);
          return false;
        }
        remaining[predecessor]--;
        add = remaining[predecessor] == 0;
      }
      if (add) {
        pg_set_add(&attracted, predecessor);
        queue[tail++] = predecessor;
      }
    }
  }

  free(queue);
  free(remaining);
  pg_set_move(result, &attracted);
  return true;
}
