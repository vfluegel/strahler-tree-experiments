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
 * Changes: native packed labels, preserved C23 carry/refill, and SIMD kernels.
 */

#include "pm_packed.h"
#include "lift_simd.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned length_mask(unsigned n) { return (1U << n) - 1; }
static unsigned bit(PMPacked const *m, int i) { return (m->symbols >> i) & 1U; }
static void set_bit(PMPacked *m, int i, unsigned value) {
  assert(i >= 0 && i < 16);
  m->symbols = (uint16_t)((m->symbols & ~(1U << i)) | (value << i));
}
static void refresh(PMPacked *m) {
  memset(m->bits, 0, sizeof(m->bits));
  memset(m->masks, 0, sizeof(m->masks));
  memset(m->levels, 0, sizeof(m->levels));
  m->count = 0;
  for (int i = 0; i < m->length;) {
    int const begin = i;
    uint16_t const level = m->symbol_levels[i];
    while (i < m->length && m->symbol_levels[i] == level) {
      i++;
    }
    unsigned const n = (unsigned)(i - begin);
    assert(m->count < 8 && n <= 8);
    m->levels[m->count] = level;
    m->masks[m->count] = (uint16_t)length_mask(n);
    m->bits[m->count] = (uint16_t)((m->symbols >> begin) & length_mask(n));
    m->count++;
  }
}

bool pm_packed_eligible(int const depth, size_t const k, size_t const t) {
  return depth > 0 && depth < INT16_MAX && k >= 2 && k <= 9 && t <= 7 &&
         k <= (size_t)depth + 1;
}

bool pm_pack(PMMeasure const *from, PMPacked *to) {
  if (from->length < 0 || from->length > 15) {
    return false;
  }
  *to = (PMPacked){.top = from->top, .length = from->length};
  int runs = 0, run_length = 0;
  for (int i = 0; i < from->length; i++) {
    if (from->levels[i] < 0 || from->levels[i] >= INT16_MAX ||
        from->bits[i] > 1 ||
        (i != 0 && from->levels[i] < from->levels[i - 1])) {
      return false;
    }
    if (i == 0 || from->levels[i] != from->levels[i - 1]) {
      runs++;
      run_length = 0;
    }
    if (++run_length > 8 || runs > 8) {
      return false;
    }
    set_bit(to, i, from->bits[i]);
    to->symbol_levels[i] = (int16_t)from->levels[i];
  }
  refresh(to);
  return true;
}

void pm_unpack(PMPacked const *from, PMMeasure *to) {
  to->top = from->top;
  to->length = from->length;
  for (int i = 0; i < from->length; i++) {
    to->bits[i] = (unsigned char)bit(from, i);
    to->levels[i] = from->symbol_levels[i];
  }
}

int pm_string_compare(uint16_t const a, uint16_t const A, uint16_t const b,
                      uint16_t const B) {
  unsigned const delta = ((a ^ b) & A & B) | (A ^ B);
  unsigned const first = delta & (0U - delta);
  return delta == 0 ? 0 : (((a & A) | (~A & ~b)) & first) != 0 ? 1 : -1;
}

static int reference_string(unsigned const a, unsigned const A,
                            unsigned const b, unsigned const B) {
  for (unsigned i = 0; i < 8; i++) {
    unsigned const mask = 1U << i;
    int const left = !(A & mask) ? 1 : (a & mask) ? 2 : 0;
    int const right = !(B & mask) ? 1 : (b & mask) ? 2 : 0;
    if (left != right) {
      return left < right ? -1 : 1;
    }
    if (!(A & mask) && !(B & mask)) {
      return 0;
    }
  }
  return 0;
}

void pm_packed_counts(uint16_t const masks[8], uint16_t counts[8],
                      bool const optimized) {
#if defined(__x86_64__)
  if (optimized && lift_ssse3()) {
    lift_counts_ssse3(masks, counts);
    return;
  }
#elif defined(__aarch64__)
  if (optimized) {
    lift_counts_neon(masks, counts);
    return;
  }
#else
  (void)optimized;
#endif
  for (unsigned i = 0; i < 8; i++) {
    unsigned length = 0, mask = masks[i];
    while (mask != 0) {
      length++;
      mask >>= 1;
    }
    counts[i] = (uint16_t)(length == 0 ? 0 : length - 1);
  }
}

int pm_packed_compare(PMPacked const *a, PMPacked const *b, int const cutoff,
                      bool const optimized) {
  if (a->top || b->top) {
    return (int)a->top - (int)b->top;
  }
  int decisions[8];
  bool vector = false;
  /* A component comparison is useful only at aligned levels. Omitted levels
   * are empty strings, and the cutoff is applied before any count shortcut. */
  if (optimized) {
#if defined(__x86_64__)
    if (lift_ssse3()) {
      lift_strings_ssse3(a->bits, a->masks, b->bits, b->masks, decisions);
      vector = true;
    }
#elif defined(__aarch64__)
    lift_strings_neon(a->bits, a->masks, b->bits, b->masks, decisions);
    vector = true;
#endif
  }
  unsigned i = 0, j = 0;
  while (true) {
    bool const left = i < a->count && a->levels[i] <= cutoff;
    bool const right = j < b->count && b->levels[j] <= cutoff;
    if (!left && !right) {
      return 0;
    }
    if (!right || (left && a->levels[i] < b->levels[j])) {
      return (a->bits[i] & 1) ? 1 : -1;
    }
    if (!left || b->levels[j] < a->levels[i]) {
      return (b->bits[j] & 1) ? -1 : 1;
    }
    int const comparison = vector && i == j ? decisions[i]
                           : optimized
                               ? pm_string_compare(a->bits[i], a->masks[i],
                                                   b->bits[j], b->masks[j])
                               : reference_string(a->bits[i], a->masks[i],
                                                  b->bits[j], b->masks[j]);
    if (comparison != 0) {
      return comparison;
    }
    i++;
    j++;
  }
}
static int minimum(int const left, int const right) {
  return left < right ? left : right;
}

static int distinct(PMPacked const *measure, int const begin, int const end) {
  int count = begin < end ? 1 : 0;
  for (int i = begin + 1; i < end; i++) {
    count += measure->symbol_levels[i] != measure->symbol_levels[i - 1];
  }
  return count;
}

static void successor(PMPacked *measure, int const pindex, int const depth,
                      int const k, int const t, bool const optimized) {
  if (measure->top) {
    return;
  }
  if (measure->length == 0) {
    /* The zero-budget tree has a single all-empty leaf. */
    measure->top = true;
    return;
  }
  /* Components stay packed across edges. A compact symbol cache lets the
   * native carry/refill rules run verbatim, including t=0 and TOP payloads. */
  uint16_t nonleading[8];
  pm_packed_counts(measure->masks, nonleading, optimized);
  for (unsigned c = 1; c < 8; c++) {
    nonleading[c] += nonleading[c - 1];
  }
  bool skip_level = false;
  int i = measure->length - 1;
  while (i >= 0 && measure->symbol_levels[i] > pindex) {
    set_bit(measure, i--, 0);
  }
  unsigned components = 0;
  while (components < measure->count && measure->levels[components] <= pindex) {
    components++;
  }
  int nes = i + 1 - (components == 0 ? 0 : nonleading[components - 1]);
  if (nes == k) {
    while (i == measure->length - 1 ||
           (i >= 0 &&
            measure->symbol_levels[i] == measure->symbol_levels[i + 1])) {
      set_bit(measure, i--, 0);
    }
  }
  while (i >= 0) {
    if (i == measure->length - 1 ||
        measure->symbol_levels[i] != measure->symbol_levels[i + 1]) {
      int nlb = i + 1 - nes;
      nes--;
      if (nlb < t) {
        int const level = minimum(i + 1 == measure->length
                                      ? measure->symbol_levels[i]
                                      : measure->symbol_levels[i + 1] - 1,
                                  pindex);
        i++;
        if (i != 0 &&
            (i == measure->length || measure->symbol_levels[i - 1] == level)) {
          nlb++;
        } else {
          nes++;
        }
        int const total_nes = minimum(nes + depth - level, k - 1);
        if (total_nes + t > measure->length) {
          int const old_length = measure->length;
          measure->length = total_nes + t;
          for (int j = old_length; j < measure->length; j++) {
            set_bit(measure, j, 0);
          }
          set_bit(measure, i, 1);
          for (int j = i; j < measure->length; j++) {
            measure->symbol_levels[j] = level;
          }
          i += t - nlb + 1;
        } else {
          set_bit(measure, i, 1);
          measure->symbol_levels[i] = level;
          int j = 1;
          while (nlb + j <= t) {
            set_bit(measure, i + j, 0);
            measure->symbol_levels[i + j] = level;
            j++;
          }
          i += j;
        }
        break;
      }
      if (i == 0 ||
          measure->symbol_levels[i - 1] != measure->symbol_levels[i]) {
        set_bit(measure, i--, 0);
        continue;
      }
    }
    if (bit(measure, i) == 0) {
      if (i == 0 ||
          measure->symbol_levels[i - 1] != measure->symbol_levels[i]) {
        if (distinct(measure, i, measure->length) ==
            depth - measure->symbol_levels[i]) {
          i--;
          continue;
        }
        nes--;
        skip_level = true;
      }
      break;
    }
    set_bit(measure, i--, 0);
  }
  if (i == -1) {
    if (measure->symbol_levels[0] == 0) {
      measure->top = true;
      return;
    }
    i = 0;
    set_bit(measure, 0, 1);
    measure->symbol_levels[0] =
        minimum(measure->symbol_levels[0], pindex + 1) - 2;
    if (t + k - 1 > measure->length) {
      int const old_length = measure->length;
      measure->length = t + k - 1;
      for (int j = old_length; j < measure->length; j++) {
        set_bit(measure, j, 0);
      }
      for (int j = 0; j < measure->length; j++) {
        measure->symbol_levels[j] = measure->symbol_levels[0];
      }
    }
    skip_level = true;
    nes--;
  }
  int const level = measure->symbol_levels[skip_level ? i : i - 1] + 1;
  int const needed = minimum(k - 2 - nes, depth - level);
  if (needed == 0) {
    measure->length = i;
  } else {
    int next_level = level;
    while (measure->length >= i + needed) {
      assert(i >= 0 && i < measure->length && next_level < depth);
      measure->symbol_levels[i++] = next_level;
    }
    while (i < measure->length) {
      next_level++;
      assert(next_level < depth);
      measure->symbol_levels[i++] = next_level;
    }
  }
  assert(measure->length - distinct(measure, 0, measure->length) <= t);
}

void pm_packed_successor(PMPacked *measure, int const cutoff, int const depth,
                         int const k, int const t, bool const optimized) {
  successor(measure, cutoff, depth, k, t, optimized);
  /* Ignore bits outside the native length, but keep the native TOP payload. */
  measure->symbols &= (uint16_t)length_mask((unsigned)measure->length);
  refresh(measure);
}
