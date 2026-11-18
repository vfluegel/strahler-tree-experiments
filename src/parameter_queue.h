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

#ifndef PARAMETER_QUEUE_H
#define PARAMETER_QUEUE_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

/* bits is used only by the existing PM default search. */
typedef struct {
  size_t k;
  size_t t;
  size_t bits;
} ParameterPair;

static inline size_t parameter_ratio(ParameterPair const parameters) {
  return parameters.k > parameters.t ? parameters.k / parameters.t
                                     : parameters.t / parameters.k;
}

typedef struct {
  ParameterPair *heap;
  bool *seen;
  size_t count;
  size_t width;
  size_t k_max;
  size_t t_max;
} ParameterQueue;

static inline void parameter_queue_push(ParameterQueue *queue,
                                        ParameterPair const parameters) {
  size_t const key = parameters.k * queue->width + parameters.t;
  if (queue->seen[key]) {
    return;
  }
  queue->seen[key] = true;
  size_t hole = queue->count++;
  while (hole != 0) {
    size_t const parent = (hole - 1) / 2;
    if (parameter_ratio(queue->heap[parent]) <= parameter_ratio(parameters)) {
      break;
    }
    queue->heap[hole] = queue->heap[parent];
    hole = parent;
  }
  queue->heap[hole] = parameters;
}

static inline ParameterPair parameter_queue_pop(ParameterQueue *queue) {
  ParameterPair const result = queue->heap[0];
  ParameterPair const last = queue->heap[--queue->count];
  size_t hole = 0;
  /* Match std::priority_queue's integer-ratio comparator, including ties. */
  while (2 * hole + 1 < queue->count) {
    size_t child = 2 * hole + 1;
    if (child + 1 < queue->count && parameter_ratio(queue->heap[child + 1]) <=
                                        parameter_ratio(queue->heap[child])) {
      child++;
    }
    queue->heap[hole] = queue->heap[child];
    hole = child;
  }
  while (hole != 0) {
    size_t const parent = (hole - 1) / 2;
    if (parameter_ratio(queue->heap[parent]) <= parameter_ratio(last)) {
      break;
    }
    queue->heap[hole] = queue->heap[parent];
    hole = parent;
  }
  queue->heap[hole] = last;
  return result;
}

static inline bool parameter_queue_init(ParameterQueue *queue, size_t k_max,
                                        size_t t_max) {
  *queue = (ParameterQueue){.width = t_max + 2, .k_max = k_max, .t_max = t_max};
  size_t const slots = (k_max + 1) * queue->width;
  queue->heap = calloc(slots, sizeof(queue->heap[0]));
  queue->seen = calloc(slots, sizeof(queue->seen[0]));
  if (queue->heap == nullptr || queue->seen == nullptr) {
    return false;
  }
  /* Oink starts at (1,1); t=0 has no ratio, even when t_max is zero. */
  if (k_max >= 1 && t_max >= 1) {
    parameter_queue_push(queue, (ParameterPair){.k = 1, .t = 1});
  }
  return true;
}

static inline void parameter_queue_advance(ParameterQueue *queue) {
  ParameterPair const pair = parameter_queue_pop(queue);
  if (pair.k < queue->k_max) {
    parameter_queue_push(queue, (ParameterPair){.k = pair.k + 1, .t = pair.t});
  }
  if (pair.t < queue->t_max) {
    parameter_queue_push(queue, (ParameterPair){.k = pair.k, .t = pair.t + 1});
  }
}

static inline void parameter_queue_destroy(ParameterQueue *queue) {
  free(queue->heap);
  free(queue->seen);
}

#endif
