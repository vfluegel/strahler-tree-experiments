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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lift_simd.h"
#include "pm_internal.h"
#include "pm_packed.h"

void pm_error(PMError *error, char const *message) {
  if (error != nullptr) {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

int pm_compare(PMMeasure const *left, PMMeasure const *right,
               int const pindex) {
  if (left->top || right->top) {
    return (int)left->top - (int)right->top;
  }
  int i = 0;
  int j = 0;
  while (true) {
    bool const have_left = i < left->length && left->levels[i] <= pindex;
    bool const have_right = j < right->length && right->levels[j] <= pindex;
    if (!have_left && !have_right) {
      return 0;
    }
    if (!have_right || (have_left && left->levels[i] < right->levels[j])) {
      return left->bits[i] == 0 ? -1 : 1;
    }
    if (!have_left || left->levels[i] > right->levels[j]) {
      return right->bits[j] == 0 ? 1 : -1;
    }
    if (left->bits[i] != right->bits[j]) {
      return left->bits[i] < right->bits[j] ? -1 : 1;
    }
    i++;
    j++;
  }
}

static void copy_measure(PMMeasure *to, PMMeasure const *from) {
  to->top = from->top;
  to->length = from->length;
  if (from->length != 0) {
    memcpy(to->bits, from->bits, (size_t)from->length);
    memcpy(to->levels, from->levels,
           (size_t)from->length * sizeof(from->levels[0]));
  }
}

void pm_result_destroy(PMResult *result) {
  if (result == nullptr) {
    return;
  }
  if (result->measure != nullptr) {
    free(result->measure[0].bits);
    free(result->measure[0].levels);
  }
  free(result->measure);
  free(result->order);
  pg_set_destroy(&result->region);
  pg_set_destroy(&result->attracted);
  *result = (PMResult){0};
}

typedef struct {
  uint64_t priority;
  size_t vertex;
} VertexOrder;

static int compare_vertices(void const *left, void const *right) {
  VertexOrder const *a = left;
  VertexOrder const *b = right;
  if (a->priority != b->priority) {
    return a->priority < b->priority ? -1 : 1;
  }
  return (a->vertex > b->vertex) - (a->vertex < b->vertex);
}

typedef struct {
  PGGame const *game;
  PGSet const *active;
  PMResult *result;
  PMMeasure tmp;
  PMMeasure best;
  PMPacked *packed;
  PMPacked packed_tmp, packed_best;
  bool lookup, comparison;
} LiftWork;

static int priority_index(PMResult const *result, uint64_t const priority) {
  PGPlayer const counter = (PGPlayer)(1 - result->player);
  return result->depth -
         (int)(counter == PG_EVEN ? (priority + 1) / 2 : priority / 2) - 1;
}

static void progress(LiftWork *work, size_t const from, size_t const to,
                     int const pindex) {
  PMResult const *result = work->result;
  copy_measure(&work->tmp, &result->measure[to]);
  if (work->game->vertices[from].priority % 2 != result->player) {
    if (result->algorithm == PM_STRAHLER) {
      strpm_successor(&work->tmp, pindex, result->depth,
                      (int)result->parameters.k, (int)result->parameters.t);
    } else {
      sspm_successor(&work->tmp, pindex, result->depth);
    }
  } else if (result->algorithm == PM_SUCCINCT) {
    sspm_truncate(&work->tmp, pindex);
  }
}

static void packed_progress(LiftWork *work, size_t const from, size_t const to,
                            int const pindex) {
  work->packed_tmp = work->packed[to];
  if (work->game->vertices[from].priority % 2 != work->result->player) {
    pm_packed_successor(&work->packed_tmp, pindex, work->result->depth,
                        (int)work->result->parameters.k,
                        (int)work->result->parameters.t, work->lookup);
  }
}

static bool packed_vertex(LiftWork *work, size_t const vertex,
                          size_t const target) {
  PMResult *result = work->result;
  result->attempts++;
  PMPacked *current = work->packed + vertex;
  if (current->top) {
    return false;
  }
  int const pindex =
      priority_index(result, work->game->vertices[vertex].priority);
  bool const maximum = work->game->vertices[vertex].owner != result->player;
  if (maximum && target != SIZE_MAX) {
    packed_progress(work, vertex, target, pindex);
    if (pm_packed_compare(&work->packed_tmp, current, pindex,
                          work->comparison) <= 0) {
      return false;
    }
    *current = work->packed_tmp;
  } else {
    bool first = true;
    for (size_t edge = work->game->succ_offsets[vertex];
         edge < work->game->succ_offsets[vertex + 1]; edge++) {
      size_t const to = work->game->successors[edge];
      if (!pg_set_contains(work->active, to)) {
        continue;
      }
      packed_progress(work, vertex, to, pindex);
      int const cmp =
          first ? 0
                : pm_packed_compare(&work->packed_tmp, &work->packed_best,
                                    pindex, work->comparison);
      if (first || (maximum ? cmp > 0 : cmp < 0)) {
        work->packed_best = work->packed_tmp;
      }
      first = false;
    }
    if (first || pm_packed_compare(current, &work->packed_best, pindex,
                                   work->comparison) >= 0) {
      return false;
    }
    *current = work->packed_best;
  }
  result->lifts++;
  return true;
}

static bool lift_vertex(LiftWork *work, size_t const vertex,
                        size_t const target) {
  if (work->packed != nullptr) {
    return packed_vertex(work, vertex, target);
  }
  PMResult *result = work->result;
  result->attempts++;
  PMMeasure *current = &result->measure[vertex];
  if (current->top) {
    return false;
  }
  int const pindex =
      priority_index(result, work->game->vertices[vertex].priority);
  bool const maximum = work->game->vertices[vertex].owner != result->player;
  if (maximum && target != SIZE_MAX) {
    progress(work, vertex, target, pindex);
    if (pm_compare(&work->tmp, current, pindex) <= 0) {
      return false;
    }
    copy_measure(current, &work->tmp);
  } else {
    bool first = true;
    for (size_t edge = work->game->succ_offsets[vertex];
         edge < work->game->succ_offsets[vertex + 1]; edge++) {
      size_t const to = work->game->successors[edge];
      if (!pg_set_contains(work->active, to)) {
        continue;
      }
      progress(work, vertex, to, pindex);
      int const comparison =
          first ? 0 : pm_compare(&work->tmp, &work->best, pindex);
      if (first || (maximum ? comparison > 0 : comparison < 0)) {
        copy_measure(&work->best, &work->tmp);
      }
      first = false;
    }
    if (first || pm_compare(current, &work->best, pindex) >= 0) {
      return false;
    }
    copy_measure(current, &work->best);
  }
  result->lifts++;
  return true;
}

bool pm_lift(PGGame const *game, PGSet const *active,
             PMAlgorithm const algorithm, PMParameters parameters,
             PGPlayer const player, PMResult *result, PMError *error) {
  if (game == nullptr || active == nullptr || result == nullptr ||
      (algorithm != PM_STRAHLER && algorithm != PM_SUCCINCT) ||
      (player != PG_EVEN && player != PG_ODD) ||
      active->bit_count != game->vertex_count ||
      game->max_priority >= INT_MAX || !pg_subgame_is_total(game, active) ||
      (algorithm == PM_STRAHLER &&
       (parameters.k == 0 || parameters.k > INT_MAX ||
        parameters.t > (size_t)INT_MAX - parameters.k)) ||
      (algorithm == PM_SUCCINCT && parameters.bits > INT_MAX)) {
    pm_error(error, "invalid lifting parameters or non-total subgame");
    return false;
  }
  int const depth = (int)(player == PG_EVEN ? (game->max_priority + 1) / 2
                                            : game->max_priority / 2 + 1);
  if (algorithm == PM_STRAHLER && parameters.k > (size_t)depth + 1) {
    parameters.k = (size_t)depth + 1;
  }
  size_t const capacity = algorithm == PM_STRAHLER
                              ? parameters.k - 1 + parameters.t
                              : parameters.bits;
  size_t const stride = capacity == 0 ? 1 : capacity;
  size_t const count = game->vertex_count;
  if (count > SIZE_MAX / sizeof(PMMeasure) ||
      count > SIZE_MAX / sizeof(VertexOrder) ||
      count > SIZE_MAX / sizeof(size_t) ||
      game->edge_count > SIZE_MAX / sizeof(size_t) ||
      count > SIZE_MAX / stride - 2 ||
      (count + 2) * stride > SIZE_MAX / sizeof(int)) {
    pm_error(error, "progress-measure storage is too large");
    return false;
  }
  *result = (PMResult){.algorithm = algorithm,
                       .parameters = parameters,
                       .player = player,
                       .depth = depth,
                       .vertex_count = count,
                       .active_count = pg_set_count(active)};
  /* Two extra slots share the allocation with the working copies. */
  result->measure = calloc(count + 1, sizeof(result->measure[0]));
  result->order = malloc((count == 0 ? 1 : count) * sizeof(result->order[0]));
  unsigned char *bits = calloc((count + 2) * stride, sizeof(bits[0]));
  int *levels = calloc((count + 2) * stride, sizeof(levels[0]));
  VertexOrder *order = malloc((count == 0 ? 1 : count) * sizeof(order[0]));
  size_t *queue = malloc((count == 0 ? 1 : count) * sizeof(queue[0]));
  size_t *predecessors = malloc((game->edge_count == 0 ? 1 : game->edge_count) *
                                sizeof(predecessors[0]));
  PGSet dirty = {0};
  bool succeeded = false;
  PMPacked *packed = nullptr;
  if (result->measure != nullptr) {
    result->measure[0].bits = bits;
    result->measure[0].levels = levels;
  } else {
    free(bits);
    free(levels);
  }
  if (result->measure == nullptr || result->order == nullptr ||
      bits == nullptr || levels == nullptr || order == nullptr ||
      queue == nullptr || predecessors == nullptr ||
      !pg_set_init(&result->region, count) ||
      !pg_set_init(&result->attracted, count) || !pg_set_init(&dirty, count)) {
    pm_error(error, "failed to allocate progress measures");
    goto cleanup;
  }
  for (size_t vertex = 0; vertex < count; vertex++) {
    result->measure[vertex] =
        (PMMeasure){.top = !pg_set_contains(active, vertex),
                    .length = (int)capacity,
                    .bits = bits + vertex * stride,
                    .levels = levels + vertex * stride};
    if (algorithm == PM_STRAHLER) {
      for (size_t i = parameters.t + 1; i < capacity; i++) {
        result->measure[vertex].levels[i] = (int)(i - parameters.t);
      }
    }
    order[vertex] = (VertexOrder){game->vertices[vertex].priority, vertex};
  }
  qsort(order, count, sizeof(order[0]), compare_vertices);
  for (size_t i = 0; i < count; i++) {
    result->order[i] = order[i].vertex;
    queue[i] = game->pred_offsets[i + 1];
  }
  /* Oink builds incoming edges backwards after sorting vertices by priority. */
  for (size_t i = 0; i < count; i++) {
    size_t const vertex = result->order[i];
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      size_t const to = game->successors[edge];
      predecessors[--queue[to]] = vertex;
    }
  }
  LiftWork work = {
      .game = game,
      .active = active,
      .result = result,
      .tmp = {.bits = bits + count * stride, .levels = levels + count * stride},
      .best = {.bits = bits + (count + 1) * stride,
               .levels = levels + (count + 1) * stride}};
  char const *backend = getenv("STRAHLER_PM_BACKEND");
  if (algorithm == PM_STRAHLER && backend != nullptr &&
      strcmp(backend, "scalar") != 0 && strcmp(backend, "auto") != 0 &&
      pm_packed_eligible(depth, parameters.k, parameters.t) &&
      count <= SIZE_MAX / sizeof(PMPacked)) {
    packed = malloc((count == 0 ? 1 : count) * sizeof(*packed));
    bool encodable = packed != nullptr;
    for (size_t v = 0; encodable && v < count; v++) {
      encodable = pm_pack(result->measure + v, packed + v);
    }
    if (encodable) {
      work.packed = packed;
      work.lookup = backend == nullptr || strcmp(backend, "lookup") == 0 ||
                    strcmp(backend, "both") == 0;
      work.comparison = backend == nullptr || strcmp(backend, "compare") == 0 ||
                        strcmp(backend, "both") == 0;
    }
  }
  if (getenv("STRAHLER_BACKEND_REPORT") != nullptr) {
    fprintf(stderr, "PM backend %s, lookup=%d comparison=%d ISA=%s\n",
            work.packed == nullptr ? "scalar" : "packed", work.lookup,
            work.comparison, lift_isa_name(lift_isa()));
  }
  size_t queued = 0;
  /* Oink's uintqueue is a stack; order affects untruncated Strahler tails. */
  for (size_t step = 0; step < count || queued != 0;) {
    size_t vertex = 0;
    if (step < count) {
      vertex = result->order[count - 1 - step++];
      if (!pg_set_contains(active, vertex) ||
          !lift_vertex(&work, vertex, SIZE_MAX)) {
        continue;
      }
    } else {
      vertex = queue[--queued];
      pg_set_remove(&dirty, vertex);
    }
    for (size_t edge = game->pred_offsets[vertex];
         edge < game->pred_offsets[vertex + 1]; edge++) {
      size_t const from = predecessors[edge];
      if (pg_set_contains(active, from) && lift_vertex(&work, from, vertex) &&
          !pg_set_contains(&dirty, from)) {
        /* dirty keeps at most one queue entry per vertex. */
        assert(queued < count);
        queue[queued++] = from;
        pg_set_add(&dirty, from);
      }
    }
  }
  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    if (work.packed != nullptr) {
      pm_unpack(work.packed + vertex, result->measure + vertex);
    }
    if (!result->measure[vertex].top) {
      pg_set_add(&result->region, vertex);
    }
  }
  succeeded = true;
cleanup:
  free(packed);
  free(order);
  free(queue);
  free(predecessors);
  pg_set_destroy(&dirty);
  if (!succeeded) {
    pm_result_destroy(result);
  }
  return succeeded;
}

bool pm_verify(PGGame const *game, PGSet const *active, PMResult const *result,
               PMError *error) {
  if (game == nullptr || active == nullptr || result == nullptr ||
      result->vertex_count != game->vertex_count ||
      result->region.bit_count != game->vertex_count ||
      (result->algorithm != PM_STRAHLER && result->algorithm != PM_SUCCINCT) ||
      (result->player != PG_EVEN && result->player != PG_ODD) ||
      result->depth < 0 || result->parameters.bits > INT_MAX ||
      result->parameters.k > INT_MAX || result->parameters.t > INT_MAX ||
      (result->algorithm == PM_STRAHLER && result->parameters.k == 0) ||
      !pg_set_subset(&result->region, active) ||
      !pg_subgame_is_total(game, active)) {
    pm_error(error, "invalid progress-measure domain");
    return false;
  }
  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    PMMeasure const *measure = &result->measure[vertex];
    if (pg_set_contains(&result->region, vertex) == measure->top) {
      pm_error(error, "non-top set differs from the reported dominion");
      return false;
    }
    if (measure->top) {
      continue;
    }
    size_t const capacity =
        result->algorithm == PM_STRAHLER
            ? result->parameters.k - 1 + result->parameters.t
            : result->parameters.bits;
    if (measure->length < 0 || (size_t)measure->length > capacity ||
        (result->algorithm == PM_SUCCINCT &&
         (size_t)measure->length != capacity) ||
        (measure->length != 0 &&
         (measure->bits == nullptr || measure->levels == nullptr))) {
      pm_error(error, "invalid measure length");
      return false;
    }
    size_t strings = 0;
    for (int i = 0; i < measure->length; i++) {
      int const level = measure->levels[i];
      if (measure->bits[i] > 1 || level < 0 || level > result->depth ||
          (level == result->depth &&
           (measure->bits[i] != 0 ||
            (result->algorithm == PM_STRAHLER && result->depth != 0))) ||
          (i != 0 && level < measure->levels[i - 1])) {
        pm_error(error, "invalid measure bits or levels");
        return false;
      }
      strings += i == 0 || level != measure->levels[i - 1];
    }
    if (result->algorithm == PM_STRAHLER &&
        ((size_t)measure->length - strings > result->parameters.t ||
         strings >
             (result->parameters.k == 1 ? 1 : result->parameters.k - 1))) {
      pm_error(error, "measure exceeds the Strahler budget");
      return false;
    }
  }
  for (size_t vertex = pg_set_next(&result->region, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(&result->region, vertex + 1)) {
    PMMeasure const *measure = &result->measure[vertex];
    int const pindex = priority_index(result, game->vertices[vertex].priority);
    bool const strict = game->vertices[vertex].priority % 2 != result->player;
    bool const existential = game->vertices[vertex].owner == result->player;
    bool allowed = false;
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      size_t const to = game->successors[edge];
      if (!pg_set_contains(active, to)) {
        continue;
      }
      int const comparison = pm_compare(measure, &result->measure[to], pindex);
      bool const progressive = comparison > 0 || (!strict && comparison == 0);
      if (!existential && !progressive) {
        pm_error(error, "an opponent edge violates the progress condition");
        return false;
      }
      allowed |= progressive;
    }
    if (!allowed) {
      pm_error(error, "a non-top vertex has no progressive successor");
      return false;
    }
  }
  PGSet complement = {0};
  PGSet escape = {0};
  bool valid = false;
  if (pg_set_clone(&complement, active)) {
    pg_set_subtract_into(&complement, &result->region);
    valid = pg_attractor(game, active, &complement,
                         (PGPlayer)(1 - result->player), &escape) &&
            pg_set_equal(&escape, &complement);
  }
  pg_set_destroy(&complement);
  pg_set_destroy(&escape);
  if (!valid) {
    pm_error(error, "the non-top set is not a dominion");
  }
  return valid;
}
