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
/* Derived from Vanessa Flügel's Oink strpm_simd.cpp/.hpp at
 * 85230b6d06307adbf6db6e474ec5fcf546b09400.
 * Changes: C23 SSE2/NEON kernels, native worklist integration, cutoff handling,
 * and conversion only at the tree/certificate boundary. */

#include "strpm_simd.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

#if (defined(__x86_64__) || defined(__aarch64__)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
#define STRPM_VECTOR 1
#if defined(__x86_64__)
#include <emmintrin.h>
#else
#include <arm_neon.h>
#endif
#else
#define STRPM_VECTOR 0
#endif

bool strpm_simd_available(void) { return STRPM_VECTOR != 0; }

bool strpm_simd_eligible(int const depth, size_t const k, size_t const t) {
  return strpm_simd_available() && depth > 0 && depth < INT16_MAX && k >= 2 &&
         k <= 9 && t <= 7 && k <= (size_t)depth + 1;
}

bool strpm_simd_pack(PMMeasure const *from, STRPMVector *to) {
  *to = (STRPMVector){0};
  if (from->top) {
    to->count = 1;
    to->levels[0] = UINT16_MAX;
    return true;
  }
  if (from->length < 0 || from->length > 15) {
    return false;
  }
  for (int i = 0; i < from->length;) {
    if (to->count == 8 || from->levels[i] < 0 || from->levels[i] >= INT16_MAX ||
        (i != 0 && from->levels[i] < from->levels[i - 1])) {
      return false;
    }
    unsigned const lane = to->count++;
    to->levels[lane] = (uint16_t)from->levels[i];
    unsigned n = 0;
    do {
      if (n == 8 || from->bits[i] > 1) {
        return false;
      }
      to->bits[lane] |= (uint16_t)((unsigned)from->bits[i++] << n++);
    } while (i < from->length && from->levels[i] == to->levels[lane]);
    to->masks[lane] = (uint16_t)((1U << n) - 1);
  }
  return true;
}

void strpm_simd_unpack(STRPMVector const *from, PMMeasure *to) {
  to->top = from->count != 0 && from->levels[0] == UINT16_MAX;
  if (to->top) {
    return;
  }
  to->length = 0;
  for (unsigned lane = 0; lane < from->count; lane++) {
    for (unsigned mask = from->masks[lane], bit = 0; mask; mask >>= 1, bit++) {
      to->bits[to->length] = (unsigned char)((from->bits[lane] >> bit) & 1);
      to->levels[to->length++] = from->levels[lane];
    }
  }
}

#if STRPM_VECTOR
typedef uint16_t Vec __attribute__((vector_size(16)));
static Vec const lanes = {0, 1, 2, 3, 4, 5, 6, 7};

typedef struct {
  Vec bits, masks, levels;
  unsigned count;
} Label;

static inline Vec splat(unsigned const n) {
  uint16_t const x = (uint16_t)n;
  return (Vec){x, x, x, x, x, x, x, x};
}
static inline Vec choose(Vec const mask, Vec const yes, Vec const no) {
  return (mask & yes) | (~mask & no);
}
static inline Label load(STRPMVector const *m) {
  Label result = {.count = m->count};
  memcpy(&result.bits, m->bits, 16);
  memcpy(&result.masks, m->masks, 16);
  memcpy(&result.levels, m->levels, 16);
  return result;
}
static inline void save(STRPMVector *m, Label const label) {
  memcpy(m->bits, &label.bits, 16);
  memcpy(m->masks, &label.masks, 16);
  memcpy(m->levels, &label.levels, 16);
  m->count = (uint8_t)label.count;
}
static inline Vec popcount(Vec x) {
  x -= (x >> 1) & splat(0x5555);
  x = (x & splat(0x3333)) + ((x >> 2) & splat(0x3333));
  x = (x + (x >> 4)) & splat(0x0f0f);
  return (x + (x >> 8)) & splat(0xff);
}
static inline Vec prefix(Vec x) {
#if defined(__x86_64__)
  __m128i v = (__m128i)x;
  v = _mm_add_epi16(v, _mm_slli_si128(v, 2));
  v = _mm_add_epi16(v, _mm_slli_si128(v, 4));
  v = _mm_add_epi16(v, _mm_slli_si128(v, 8));
  return (Vec)v;
#else
  uint16x8_t v = (uint16x8_t)x, zero = vdupq_n_u16(0);
  v = vaddq_u16(v, vextq_u16(zero, v, 7));
  v = vaddq_u16(v, vextq_u16(zero, v, 6));
  v = vaddq_u16(v, vextq_u16(zero, v, 4));
  return (Vec)v;
#endif
}
static inline unsigned mask_bits(Vec const mask) {
#if defined(__x86_64__)
  return (unsigned)_mm_movemask_epi8((__m128i)mask);
#else
  uint16x8_t const weights = {1, 2, 4, 8, 16, 32, 64, 128};
  return vaddvq_u16(vandq_u16((uint16x8_t)mask, weights));
#endif
}
static inline int first(Vec const mask) {
  unsigned const bits = mask_bits(mask);
#if defined(__x86_64__)
  return bits == 0 ? 8 : __builtin_ctz(bits) / 2;
#else
  return bits == 0 ? 8 : __builtin_ctz(bits);
#endif
}
static inline int last(Vec const mask) {
  unsigned const bits = mask_bits(mask);
  int const index = bits == 0 ? -1 : 31 - __builtin_clz(bits);
#if defined(__x86_64__)
  return index < 0 ? -1 : index / 2;
#else
  return index;
#endif
}
static inline int minimum(int const a, int const b) { return a < b ? a : b; }
static inline bool top(Label const m) {
  return m.count != 0 && m.levels[0] == UINT16_MAX;
}
static inline void inactive(Label *m) {
  Vec const active = (Vec)(lanes < splat(m->count));
  m->bits &= active;
  m->masks &= active;
}
static inline void make_top(Label *m) {
  m->count = 1;
  m->levels[0] = UINT16_MAX;
  inactive(m);
}

static inline void successor(Label *m, int const cutoff, int const depth,
                             int const k, int const t) {
  if (top(*m)) {
    return;
  }
  if (m->count == 0 || cutoff < 0) {
    make_top(m);
    return;
  }
  Vec const has_bits = (Vec)(m->masks > splat(0));
  Vec const per_lane = popcount(m->masks) - (has_bits & splat(1));
  Vec counts = prefix(per_lane);
  Vec const before = counts - per_lane;
  Vec const filled =
      (Vec)(m->levels == splat((unsigned)(depth - (int)m->count)) + lanes);
  Vec const stuck =
      has_bits & ((Vec)(before == splat((unsigned)t)) |
                  ((Vec)(counts == splat((unsigned)t)) &
                   (((Vec)(m->bits == (m->masks & splat(0xfffe))) & filled) |
                    (Vec)(m->bits == m->masks))));
  Vec const valid =
      has_bits & (Vec)(m->levels <= splat((unsigned)cutoff)) & ~stuck;
  int match = last(valid);
  if (match < 0) {
    if (m->levels[0] == 0) {
      make_top(m);
      return;
    }
    m->bits[0] = 1;
    match = 0;
    m->levels[0] = (uint16_t)minimum((int)m->levels[0] - 1, cutoff);
  } else if (counts[match] == t) {
    /* The selected string carries; all other strings stay in vector registers.
     * Oink has the same scalar branch on the selected lane. */
    unsigned const zeros = (~m->bits[match]) & m->masks[match];
    assert(zeros != 0);
    int const reset =
        __builtin_clz(zeros) - (int)(sizeof(unsigned) * CHAR_BIT) + 9;
    int const old_length = __builtin_popcount(m->masks[match]);
    unsigned const keep = (1U << (8 - reset)) - 1;
    m->bits[match] &= (uint16_t)keep;
    m->masks[match] &= (uint16_t)keep;
    if (reset < 8) {
      counts[match] -=
          (uint16_t)(old_length - __builtin_popcount(m->masks[match]));
      match++;
      if (match < k - 1) {
        m->levels[match] = m->levels[match - 1] + 1;
        m->bits[match] = 0;
      }
    } else {
      counts[match] -= (uint16_t)(old_length - 1);
      if ((match + 1 == (int)m->count && m->levels[match] < depth - 1) ||
          (match + 1 < (int)m->count &&
           m->levels[match] + 1 < m->levels[match + 1])) {
        m->levels[match] = match + 1 == (int)m->count
                               ? m->levels[match] + 1
                               : m->levels[match + 1] - 1;
      } else {
        m->levels[match]++;
      }
    }
  } else if (match + 1 < (int)m->count &&
             m->levels[match] + 1 < m->levels[match + 1] &&
             m->levels[match] != cutoff) {
    match++;
    m->bits[match] = 1;
    m->levels[match] = (uint16_t)minimum(cutoff, (int)m->levels[match] - 1);
  } else {
    m->bits[match] |= (uint16_t)(1U << __builtin_popcount(m->masks[match]));
  }
  if (match < k - 1) {
    int const used = match > 0 ? counts[match - 1] : 0;
    m->masks[match] = (uint16_t)((1U << (t - used + 1)) - 1);
    int fill = minimum(k - 2 - match, depth - 1 - (int)m->levels[match]);
    if (fill < 0) {
      fill = 0;
    }
    m->count = (unsigned)(match + 1 + fill);
    Vec const after = (Vec)(lanes > splat((unsigned)match));
    Vec const tail = after & (Vec)(lanes < splat(m->count));
    m->levels =
        choose(tail, splat((unsigned)((int)m->levels[match] - match)) + lanes,
               m->levels);
    m->masks = choose(tail, splat(1), m->masks);
    m->bits &= ~after;
    inactive(m);
  }
}

static inline int compare(Label const a, Label const b, int const cutoff) {
  if (top(a) || top(b)) {
    return (int)top(a) - (int)top(b);
  }
  if (cutoff < 0) {
    return 0;
  }
  Vec const shared = a.masks & b.masks;
  Vec const delta = ((a.bits ^ b.bits) & shared) | (a.masks ^ b.masks);
  Vec const bit = delta & (splat(0) - delta);
  Vec const greater =
      (Vec)((((a.bits & a.masks) | (~a.masks & ~b.bits)) & bit) != splat(0));
  Vec const different = (Vec)(delta != splat(0));
  Vec const pi = splat((unsigned)cutoff);
  Vec const in_a = (Vec)(lanes < splat(a.count)) & (Vec)(a.levels <= pi);
  Vec const in_b = (Vec)(lanes < splat(b.count)) & (Vec)(b.levels <= pi);
  Vec const a_earlier = in_a & (~in_b | (Vec)(a.levels < b.levels));
  Vec const b_earlier = in_b & (~in_a | (Vec)(b.levels < a.levels));
  Vec const same = in_a & in_b & (Vec)(a.levels == b.levels) & different;
  Vec const a_one = (Vec)((a.bits & splat(1)) != splat(0));
  Vec const b_one = (Vec)((b.bits & splat(1)) != splat(0));
  Vec const wins =
      (a_earlier & a_one) | (b_earlier & ~b_one) | (same & greater);
  int const index = first(a_earlier | b_earlier | same);
  return index == 8 ? 0 : wins[index] ? 1 : -1;
}

void strpm_simd_successor(STRPMVector *m, int const cutoff, int const depth,
                          int const k, int const t) {
  Label label = load(m);
  successor(&label, cutoff, depth, k, t);
  save(m, label);
}
int strpm_simd_compare(STRPMVector const *a, STRPMVector const *b,
                       int const cutoff) {
  return compare(load(a), load(b), cutoff);
}

bool strpm_simd_lift(PGGame const *game, PGSet const *active, PMResult *result,
                     STRPMVector *measures, size_t const vertex,
                     size_t const target) {
  result->attempts++;
  Label const current = load(measures + vertex);
  if (top(current)) {
    return false;
  }
  uint64_t const priority = game->vertices[vertex].priority;
  int const cutoff =
      result->depth -
      (int)(result->player == PG_ODD ? (priority + 1) / 2 : priority / 2) - 1;
  bool const maximum = game->vertices[vertex].owner != result->player;
  bool const strict = priority % 2 != result->player;
  int const k = (int)result->parameters.k, t = (int)result->parameters.t;
  Label best;
  if (maximum && target != SIZE_MAX) {
    best = load(measures + target);
    if (strict) {
      successor(&best, cutoff, result->depth, k, t);
    }
  } else {
    bool have_best = false;
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      size_t const to = game->successors[edge];
      if (!pg_set_contains(active, to)) {
        continue;
      }
      Label candidate = load(measures + to);
      if (strict) {
        successor(&candidate, cutoff, result->depth, k, t);
      }
      int const cmp = have_best ? compare(candidate, best, cutoff) : 0;
      if (!have_best || (maximum ? cmp > 0 : cmp < 0)) {
        best = candidate;
        have_best = true;
      }
    }
    assert(have_best);
  }
  if (compare(best, current, cutoff) <= 0) {
    return false;
  }
  save(measures + vertex, best);
  result->lifts++;
  return true;
}
#else
void strpm_simd_successor(STRPMVector *m, int cutoff, int depth, int k, int t) {
  (void)m;
  (void)cutoff;
  (void)depth;
  (void)k;
  (void)t;
  assert(false);
}
int strpm_simd_compare(STRPMVector const *a, STRPMVector const *b, int cutoff) {
  (void)a;
  (void)b;
  (void)cutoff;
  assert(false);
  return 0;
}
bool strpm_simd_lift(PGGame const *game, PGSet const *active, PMResult *result,
                     STRPMVector *measures, size_t vertex, size_t target) {
  (void)game;
  (void)active;
  (void)result;
  (void)measures;
  (void)vertex;
  (void)target;
  assert(false);
  return false;
}
#endif
