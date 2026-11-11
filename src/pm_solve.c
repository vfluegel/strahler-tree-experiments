/*
 * Copyright 2017-2018 Tom van Dijk, Johannes Kepler University Linz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Derived from Oink (https://github.com/trolando/oink) and Vanessa Flügel's
 * fork (https://github.com/vfluegel/oink). This file is licensed under
 * Apache-2.0 rather than the repository's MIT license. Apache-2.0 → MIT
 * relicensing requires the copyright holders' consent; until that consent
 * is obtained this file remains Apache-2.0.
 * Changes: ported to C23 and modified for tree output.
 */

#include <limits.h>
#include <stdlib.h>

#include "pm_internal.h"

static size_t floor_log2(size_t value) {
  size_t logarithm = 0;
  while (value > 1) {
    value >>= 1;
    logarithm++;
  }
  return logarithm;
}

static size_t ratio(PMParameters const parameters) {
  return parameters.k > parameters.t ? parameters.k / parameters.t
                                     : parameters.t / parameters.k;
}

typedef struct {
  PMParameters *heap;
  bool *seen;
  size_t count;
  size_t width;
  size_t k_max;
  size_t t_max;
} ParameterQueue;

static void push(ParameterQueue *queue, PMParameters const parameters) {
  size_t const key = parameters.k * queue->width + parameters.t;
  if (queue->seen[key]) {
    return;
  }
  queue->seen[key] = true;
  size_t hole = queue->count++;
  while (hole != 0) {
    size_t const parent = (hole - 1) / 2;
    if (ratio(queue->heap[parent]) <= ratio(parameters)) {
      break;
    }
    queue->heap[hole] = queue->heap[parent];
    hole = parent;
  }
  queue->heap[hole] = parameters;
}

static PMParameters pop(ParameterQueue *queue) {
  PMParameters const result = queue->heap[0];
  PMParameters const last = queue->heap[--queue->count];
  size_t hole = 0;
  /* Match std::priority_queue's integer-ratio comparator, including ties. */
  while (2 * hole + 1 < queue->count) {
    size_t child = 2 * hole + 1;
    if (child + 1 < queue->count &&
        ratio(queue->heap[child + 1]) <= ratio(queue->heap[child])) {
      child++;
    }
    queue->heap[hole] = queue->heap[child];
    hole = child;
  }
  while (hole != 0) {
    size_t const parent = (hole - 1) / 2;
    if (ratio(queue->heap[parent]) <= ratio(last)) {
      break;
    }
    queue->heap[hole] = queue->heap[parent];
    hole = parent;
  }
  queue->heap[hole] = last;
  return result;
}

bool pm_solve(PGGame const *game, PGSet const *active, PMOptions const *options,
              PMVisit const visit, void *context, PMError *error) {
  if (game == nullptr || active == nullptr || options == nullptr ||
      visit == nullptr || active->bit_count != game->vertex_count ||
      (options->lexicographic &&
       (options->fixed || options->algorithm != PM_STRAHLER)) ||
      game->max_priority >= INT_MAX || !pg_subgame_is_total(game, active)) {
    pm_error(error, "invalid solver arguments");
    return false;
  }
  size_t const vertices = pg_set_count(active);
  size_t const logarithm = floor_log2(vertices);
  size_t const max_bits = logarithm + (vertices > ((size_t)1 << logarithm));
  /* Full-game measures need the extra root level; Oink's smaller bound
   * relies on attraction and removal of previously found dominions. */
  size_t const height =
      (size_t)(game->max_priority / 2 + (options->incremental ? 1 : 2));
  ParameterQueue queue = {.width = logarithm + 2,
                          .k_max =
                              logarithm + 2 < height ? logarithm + 2 : height,
                          .t_max = logarithm};
  if (options->lexicographic) {
    /* A measure tree has at most n occupied leaves and depth+1 levels.
     * Proposition 3 gives Str <= min(depth+1, floor(log2 n)+1), including
     * the root, with the same safe bound whether or not dominions are removed.
     */
    size_t const measure_height = (size_t)(game->max_priority / 2 + 2);
    queue.k_max =
        logarithm + 1 < measure_height ? logarithm + 1 : measure_height;
  }
  size_t const slots = (queue.k_max + 1) * queue.width;
  PMResult result[2] = {0};
  PGSet remaining = {0};
  PGSet unresolved = {0};
  PGSet attracted = {0};
  bool succeeded = false;
  queue.heap = calloc(slots, sizeof(queue.heap[0]));
  queue.seen = calloc(slots, sizeof(queue.seen[0]));
  if (queue.heap == nullptr || queue.seen == nullptr ||
      !pg_set_clone(&remaining, active)) {
    pm_error(error, "failed to allocate parameter search");
    goto cleanup;
  }
  PMParameters parameters =
      options->fixed
          ? options->parameters
          : (PMParameters){.k = 1, .t = 1, .bits = max_bits == 0 ? 0 : 1};
  if (options->lexicographic) {
    parameters.t = 0;
  }
  if (!options->fixed && !options->lexicographic &&
      options->algorithm == PM_STRAHLER) {
    push(&queue, parameters);
  }
  for (size_t round = 1;; round++) {
    if (!options->fixed && !options->lexicographic &&
        options->algorithm == PM_STRAHLER) {
      if (queue.count == 0) {
        if (options->incremental) {
          succeeded = true;
          break;
        }
        pm_error(error, "parameter search exhausted before covering the game");
        goto cleanup;
      }
      parameters = pop(&queue);
    }
    size_t const before = pg_set_count(&remaining);
    for (size_t player = 0; player < 2; player++) {
      PGSet const *domain = options->incremental ? &remaining : active;
      if (!pm_lift(game, domain, options->algorithm, parameters,
                   (PGPlayer)player, &result[player], error) ||
          (options->verify &&
           !pm_verify(game, domain, &result[player], error))) {
        goto cleanup;
      }
      if (options->incremental) {
        if (!pg_attractor(game, &remaining, &result[player].region,
                          (PGPlayer)player, &attracted)) {
          pm_error(error, "failed to remove a round's dominion");
          goto cleanup;
        }
        if (!pg_set_clone(&result[player].attracted, &attracted)) {
          pm_error(error, "failed to record attracted vertices");
          goto cleanup;
        }
        pg_set_subtract_into(&result[player].attracted, &result[player].region);
        pg_set_subtract_into(&remaining, &attracted);
      }
    }
    if (!pg_set_clone(&unresolved,
                      options->incremental ? &remaining : active)) {
      pm_error(error, "failed to allocate unresolved vertices");
      goto cleanup;
    }
    pg_set_subtract_into(&unresolved, &result[0].region);
    pg_set_subtract_into(&unresolved, &result[1].region);
    bool const complete = pg_set_empty(&unresolved);
    if ((options->incremental || options->fixed || complete) &&
        !visit(result, &unresolved, round, context)) {
      pm_error(error, "failed to write progress-measure output");
      goto cleanup;
    }
    pm_result_destroy(&result[0]);
    pm_result_destroy(&result[1]);
    pg_set_destroy(&unresolved);
    if (complete || options->fixed) {
      succeeded = true;
      break;
    }
    if (options->lexicographic) {
      /* A smaller remaining game may fit the same host. Advance the grid
       * only after this pair stops exposing new dominions. */
      if (options->incremental && pg_set_count(&remaining) < before) {
        continue;
      }
      if (parameters.t < queue.t_max) {
        parameters.t++;
      } else if (parameters.k < queue.k_max) {
        parameters.k++;
        parameters.t = 0;
      } else {
        pm_error(error,
                 "lexicographic search exhausted before covering the game");
        goto cleanup;
      }
    } else if (options->algorithm == PM_STRAHLER) {
      if (parameters.k < queue.k_max) {
        push(&queue, (PMParameters){.k = parameters.k + 1, .t = parameters.t});
      }
      if (parameters.t < queue.t_max) {
        push(&queue, (PMParameters){.k = parameters.k, .t = parameters.t + 1});
      }
    } else if (!options->incremental || before == pg_set_count(&remaining)) {
      if (parameters.bits >= max_bits) {
        pm_error(error, "bit budget exhausted before covering the game");
        goto cleanup;
      }
      parameters.bits++;
    }
  }
cleanup:
  pm_result_destroy(&result[0]);
  pm_result_destroy(&result[1]);
  pg_set_destroy(&remaining);
  pg_set_destroy(&unresolved);
  pg_set_destroy(&attracted);
  free(queue.heap);
  free(queue.seen);
  return succeeded;
}
