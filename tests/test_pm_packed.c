#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lift_simd.h"
#include "pm_internal.h"
#include "pm_packed.h"

static unsigned rng = 7019;
static size_t successor_checks;
static unsigned random_value(void) {
  rng = rng * 1664525U + 1013904223U;
  return rng;
}

static int specification(unsigned const a, unsigned const na, unsigned const b,
                         unsigned const nb) {
  for (unsigned i = 0; i < 9; i++) {
    int const x = i >= na ? 1 : ((a >> i) & 1) ? 2 : 0;
    int const y = i >= nb ? 1 : ((b >> i) & 1) ? 2 : 0;
    if (x != y) {
      return x < y ? -1 : 1;
    }
    if (i >= na && i >= nb) {
      return 0;
    }
  }
  assert(false);
  return 0;
}

static void strings(void) {
  for (unsigned na = 0; na <= 8; na++) {
    unsigned const A = (1U << na) - 1;
    for (unsigned nb = 0; nb <= 8; nb++) {
      unsigned const B = (1U << nb) - 1;
      for (unsigned a = 0; a <= A; a++) {
        for (unsigned b = 0; b <= B; b++) {
          int const expected = specification(a, na, b, nb);
          assert(pm_string_compare((uint16_t)a, (uint16_t)A, (uint16_t)b,
                                   (uint16_t)B) == expected);
          PMPacked x = {
              .bits = {(uint16_t)a}, .masks = {(uint16_t)A}, .count = na != 0};
          PMPacked y = {
              .bits = {(uint16_t)b}, .masks = {(uint16_t)B}, .count = nb != 0};
          assert(pm_packed_compare(&x, &y, 0, true) == expected);
          assert(pm_packed_compare(&x, &y, 0, false) == expected);
        }
      }
    }
  }
  for (unsigned shift = 0; shift < 9; shift++) {
    uint16_t masks[8], counts[8];
    for (unsigned i = 0; i < 8; i++) {
      masks[i] = (uint16_t)((1U << ((i + shift) % 9)) - 1);
    }
    pm_packed_counts(masks, counts, true);
    for (unsigned i = 0; i < 8; i++) {
      unsigned const n = (i + shift) % 9;
      assert(counts[i] == (n == 0 ? 0 : n - 1));
    }
  }
}

static void equal(PMMeasure const *native, PMPacked const *packed) {
  unsigned char bits[16] = {0};
  int levels[16] = {0};
  PMMeasure restored = {.bits = bits, .levels = levels};
  pm_unpack(packed, &restored);
  assert(native->length == restored.length && native->top == restored.top);
  assert(memcmp(native->bits, restored.bits, (size_t)native->length) == 0);
  assert(memcmp(native->levels, restored.levels,
                (size_t)native->length * sizeof(int)) == 0);
}

static void successors(void) {
  for (int k = 2; k <= 9; k++) {
    for (int t = 0; t <= 7; t++) {
      int const depth = k + 4;
      assert(pm_packed_eligible(depth, (size_t)k, (size_t)t));
      for (int cutoff = -1; cutoff < depth; cutoff++) {
        unsigned char bits[16] = {0};
        int levels[16] = {0};
        PMMeasure native = {
            .length = k - 1 + t, .bits = bits, .levels = levels};
        for (int i = t + 1; i < native.length; i++) {
          levels[i] = i - t;
        }
        PMPacked packed, plain;
        assert(pm_pack(&native, &packed));
        plain = packed;
        equal(&native, &packed);
        for (unsigned step = 0; step < 1200; step++) {
          if (native.top) {
            break;
          }
          PMPacked previous = packed;
          unsigned char old_bits[16];
          int old_levels[16];
          memcpy(old_bits, bits, sizeof(bits));
          memcpy(old_levels, levels, sizeof(levels));
          PMMeasure old = native;
          old.bits = old_bits;
          old.levels = old_levels;
          strpm_successor(&native, cutoff, depth, k, t);
          successor_checks++;
          pm_packed_successor(&packed, cutoff, depth, k, t, true);
          pm_packed_successor(&plain, cutoff, depth, k, t, false);
          equal(&native, &packed);
          equal(&native, &plain);
          for (int p = -1; p <= depth; p++) {
            int const expected = pm_compare(&native, &old, p);
            assert(pm_packed_compare(&packed, &previous, p, true) == expected);
            assert(pm_packed_compare(&packed, &previous, p, false) == expected);
          }
          /* Also replay a varying cutoff, starting from a reachable state. */
          if (step % 3 == 0) {
            int const p = (int)(random_value() % (unsigned)depth);
            strpm_successor(&native, p, depth, k, t);
            successor_checks++;
            pm_packed_successor(&packed, p, depth, k, t, true);
            pm_packed_successor(&plain, p, depth, k, t, false);
            equal(&native, &packed);
            equal(&native, &plain);
          }
        }
      }
    }
  }
  assert(!pm_packed_eligible(10, 1, 3));
  assert(!pm_packed_eligible(10, 10, 3));
  assert(!pm_packed_eligible(10, 3, 8));
  assert(!pm_packed_eligible(UINT16_MAX, 3, 1));
  unsigned char bits[] = {1};
  int levels[] = {1};
  PMMeasure native = {.bits = bits, .levels = levels, .length = 1};
  PMPacked one, empty = {0};
  assert(pm_pack(&native, &one));
  assert(pm_packed_compare(&one, &empty, 0, true) == 0);
  assert(pm_packed_compare(&one, &empty, 1, true) > 0);
  empty.top = true;
  assert(pm_packed_compare(&one, &empty, -1, true) < 0);
}

int main(void) {
  strings();
  successors();
  printf(
      "PM packed: 261121 string pairs and %zu native successor checks; %s%s\n",
      successor_checks, lift_isa_name(lift_isa()),
      lift_ssse3() ? " + ssse3" : "");
  return 0;
}
