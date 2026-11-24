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

#include "host_order_data.h"
#include "parameter_pair.h"

static inline size_t parameter_ratio(ParameterPair const parameters) {
  return parameters.k > parameters.t ? parameters.k / parameters.t
                                     : parameters.t / parameters.k;
}

typedef struct {
  ParameterPair *heap;
  uint8_t *seen; /* Bit 0: queued; bit 1: selected for a solver attempt. */
  size_t count;
  size_t width;
  size_t k_max;
  size_t t_max;
  /* Table mode's persistent cursor. The ratio heap still expands normally
   * through prefix entries; its seen bitmap is never used to skip expansion. */
  HostOrderBand const *band;
  size_t table_at;
  size_t zero_k;
  ParameterPair ordered;
  bool ordered_valid;
  bool table_budget_skipped;
  bool monotone_table;
  bool ordered_prefix;
  uint64_t monotone_prefix;
} ParameterQueue;

static inline void parameter_queue_push(ParameterQueue *queue,
                                        ParameterPair const parameters) {
  /* The spare t columns also hold the original PM default's initial (1,1)
   * when t_max is zero. Keep every key inside that allocated grid. */
  if (parameters.k == 0 || parameters.k > queue->k_max || parameters.t == 0 ||
      parameters.t >= queue->width) {
    return;
  }
  size_t const key = parameters.k * queue->width + parameters.t;
  if ((queue->seen[key] & 1) != 0) {
    return;
  }
  queue->seen[key] |= 1;
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

/* Select the next untried pair: size-ordered rectangle, remaining zero column,
 * then the original positive-t ratio traversal, excluding the tried rectangle.
 * Keep ordered unchanged while shaving makes progress at this same pair. */
static inline void parameter_table_next(ParameterQueue *queue) {
  queue->ordered_valid = true;
  queue->ordered_prefix = false;
  if (queue->monotone_table && queue->monotone_prefix != 0) {
    unsigned const code = (unsigned)(queue->monotone_prefix & 63);
    queue->monotone_prefix >>= 6;
    queue->ordered = (ParameterPair){.k = code >> 3, .t = code & 7};
    queue->ordered_prefix = true;
    return;
  }
  while (!queue->monotone_table && queue->band != nullptr &&
         queue->table_at < queue->band->count) {
    unsigned const code = queue->band->pairs[queue->table_at++];
    ParameterPair const pair = {.k = code >> 3, .t = code & 7};
    if (pair.k <= queue->k_max && pair.t <= queue->t_max) {
      queue->ordered = pair;
      return;
    }
  }
  while (queue->zero_k <= queue->k_max) {
    size_t const k = queue->zero_k++;
    if (queue->band == nullptr || k > HOST_ORDER_MAX_K) {
      queue->ordered = (ParameterPair){.k = k, .t = 0};
      return;
    }
  }
  while (queue->count != 0) {
    ParameterPair const pair = queue->heap[0];
    parameter_queue_advance(queue);
    bool const visited = (queue->seen[pair.k * queue->width + pair.t] & 2) != 0;
    if (queue->monotone_table
            ? !visited
            : queue->band == nullptr || pair.k > HOST_ORDER_MAX_K ||
                  pair.t > HOST_ORDER_MAX_T) {
      queue->ordered = pair;
      return;
    }
  }
  queue->ordered_valid = false;
}

static inline void parameter_table_advance(ParameterQueue *queue) {
  parameter_table_next(queue);
  if (queue->ordered_valid) {
    size_t const key = queue->ordered.k * queue->width + queue->ordered.t;
    queue->seen[key] |= 2;
  }
}

[[nodiscard]] static inline bool parameter_table_init(ParameterQueue *queue,
                                                      size_t const k_max,
                                                      size_t const t_max,
                                                      size_t const height) {
  if (!parameter_queue_init(queue, k_max, t_max)) {
    return false;
  }
  if (height >= 1 && height <= HOST_ORDER_MAX_HEIGHT) {
    for (size_t i = 0;
         i < sizeof(host_order_bands) / sizeof(host_order_bands[0]); i++) {
      if (host_order_bands[i].height > height) {
        break;
      }
      queue->band = host_order_bands + i;
    }
  }
  queue->zero_k = 1;
  parameter_table_advance(queue);
  return true;
}

[[nodiscard]] static inline bool parameter_monotone_init(ParameterQueue *queue,
                                                         size_t const k_max,
                                                         size_t const t_max,
                                                         size_t const height) {
  if (!parameter_table_init(queue, k_max, t_max, height)) {
    return false;
  }
  queue->monotone_table = true;
  queue->zero_k = k_max + 1;
  queue->monotone_prefix =
      8; /* Chain, also when height is outside the table. */
  if (queue->band != nullptr) {
    size_t const k = k_max < HOST_ORDER_MAX_K ? k_max : HOST_ORDER_MAX_K;
    size_t const t = t_max < HOST_ORDER_MAX_T ? t_max : HOST_ORDER_MAX_T;
    queue->monotone_prefix =
        host_monotone_prefixes[queue->band->monotone[k - 1][t]];
  }
  parameter_table_advance(queue);
  return true;
}

static inline void parameter_queue_destroy(ParameterQueue *queue) {
  free(queue->heap);
  free(queue->seen);
}

#endif
