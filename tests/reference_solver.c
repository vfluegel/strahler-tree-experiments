#include <stdlib.h>

#include "reference_solver.h"

[[nodiscard]] static bool strategy_count(PGGame const *game, PGPlayer player,
                                         size_t *count) {
  size_t result = 1;
  for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
    if (game->vertices[vertex].owner != player) {
      continue;
    }
    size_t const choices =
        game->succ_offsets[vertex + 1] - game->succ_offsets[vertex];
    if (choices == 0 || result > SIZE_MAX / choices) {
      return false;
    }
    result *= choices;
  }
  *count = result;
  return true;
}

static void select_strategy(PGGame const *game, PGPlayer const player,
                            size_t code, size_t *selected) {
  for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
    if (game->vertices[vertex].owner != player) {
      continue;
    }
    size_t const begin = game->succ_offsets[vertex];
    size_t const choices = game->succ_offsets[vertex + 1] - begin;
    selected[vertex] = game->successors[begin + code % choices];
    code /= choices;
  }
}

[[nodiscard]] static PGPlayer play_winner(PGGame const *game, size_t start,
                                          size_t *selected, size_t *seen,
                                          size_t *path) {
  for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
    seen[vertex] = SIZE_MAX;
  }
  size_t length = 0;
  size_t current = start;
  while (true) {
    if (current >= game->vertex_count) {
      abort();
    }
    if (seen[current] != SIZE_MAX) {
      break;
    }
    if (length >= game->vertex_count) {
      abort();
    }
    seen[current] = length;
    path[length++] = current;
    current = selected[current];
  }
  uint64_t maximum = 0;
  for (size_t index = seen[current]; index < length; index++) {
    uint64_t const priority = game->vertices[path[index]].priority;
    if (priority > maximum) {
      maximum = priority;
    }
  }
  return (PGPlayer)(maximum % 2);
}

[[nodiscard]] static bool
winning_for_player(PGGame const *game, PGPlayer const player, PGSet *winning) {
  size_t player_strategies = 0;
  size_t opponent_strategies = 0;
  if (!strategy_count(game, player, &player_strategies) ||
      !strategy_count(game, (PGPlayer)(1 - player), &opponent_strategies) ||
      game->vertex_count > SIZE_MAX / sizeof(size_t)) {
    return false;
  }
  size_t *selected = calloc(game->vertex_count, sizeof(selected[0]));
  size_t *seen = malloc(game->vertex_count * sizeof(seen[0]));
  size_t *path = malloc(game->vertex_count * sizeof(path[0]));
  bool *candidate = malloc(game->vertex_count * sizeof(candidate[0]));
  PGSet result = {0};
  if (selected == nullptr || seen == nullptr || path == nullptr ||
      candidate == nullptr || !pg_set_init(&result, game->vertex_count)) {
    free(selected);
    free(seen);
    free(path);
    free(candidate);
    return false;
  }

  for (size_t strategy = 0; strategy < player_strategies; strategy++) {
    select_strategy(game, player, strategy, selected);
    for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
      candidate[vertex] = true;
    }
    for (size_t opponent = 0; opponent < opponent_strategies; opponent++) {
      select_strategy(game, (PGPlayer)(1 - player), opponent, selected);
      for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
        if (candidate[vertex] &&
            play_winner(game, vertex, selected, seen, path) != player) {
          candidate[vertex] = false;
        }
      }
    }
    for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
      if (candidate[vertex]) {
        pg_set_add(&result, vertex);
      }
    }
  }

  free(selected);
  free(seen);
  free(path);
  free(candidate);
  pg_set_move(winning, &result);
  return true;
}

bool reference_solve(PGGame const *game, PGSet *even, PGSet *odd) {
  if (game == nullptr || even == nullptr || odd == nullptr ||
      !winning_for_player(game, PG_EVEN, even) ||
      !winning_for_player(game, PG_ODD, odd)) {
    return false;
  }
  PGSet intersection = {0};
  PGSet combined = {0};
  PGSet domain = {0};
  bool valid = pg_set_clone(&intersection, even) &&
               pg_set_clone(&combined, even) &&
               pg_set_init(&domain, game->vertex_count);
  if (valid) {
    pg_set_intersect_into(&intersection, odd);
    pg_set_union_into(&combined, odd);
    pg_set_fill(&domain);
    valid = pg_set_empty(&intersection) && pg_set_equal(&combined, &domain);
  }
  pg_set_destroy(&intersection);
  pg_set_destroy(&combined);
  pg_set_destroy(&domain);
  return valid;
}
