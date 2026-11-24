#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pm_internal.h"
#include "strpm_simd.h"

static unsigned rng = 7019;
static size_t successors, comparisons, truncations;
static unsigned random_value(void) {
  rng = rng * 1664525U + 1013904223U;
  return rng;
}
static void equal(PMMeasure const *native, STRPMVector const *packed) {
  unsigned char bits[16] = {0};
  int levels[16] = {0};
  PMMeasure restored = {.bits = bits, .levels = levels};
  strpm_simd_unpack(packed, &restored);
  assert(native->top == restored.top);
  if (!native->top) {
    assert(native->length == restored.length);
    assert(memcmp(native->bits, bits, (size_t)native->length) == 0);
    assert(memcmp(native->levels, levels,
                  (size_t)native->length * sizeof(int)) == 0);
  }
}
static void check_successors(void) {
  for (int k = 2; k <= 9; k++) {
    for (int t = 0; t <= 7; t++) {
      int const depth = k + 4;
      assert(strpm_simd_eligible(depth, (size_t)k, (size_t)t));
      for (int cutoff = -1; cutoff < depth; cutoff++) {
        unsigned char bits[16] = {0};
        int levels[16] = {0};
        PMMeasure native = {
            .length = k - 1 + t, .bits = bits, .levels = levels};
        for (int i = t + 1; i < native.length; i++) {
          levels[i] = i - t;
        }
        STRPMVector packed;
        assert(strpm_simd_pack(&native, &packed));
        equal(&native, &packed);
        for (unsigned step = 0; step < 1200 && !native.top; step++) {
          unsigned char old_bits[16];
          int old_levels[16];
          memcpy(old_bits, bits, sizeof(bits));
          memcpy(old_levels, levels, sizeof(levels));
          PMMeasure old = native;
          old.bits = old_bits;
          old.levels = old_levels;
          STRPMVector const previous = packed;
          if (step % 50 == 0) {
            int const p = (int)(random_value() % (unsigned)(depth + 1)) - 1;
            PMMeasure canonical = native;
            canonical.bits = old_bits;
            canonical.levels = old_levels;
            STRPMVector completion = previous;
            strpm_truncate(&canonical, p, k, t);
            strpm_simd_truncate(&completion, p, k, t);
            equal(&canonical, &completion);
            assert(pm_compare(&canonical, &native, p) == 0);
            assert(pm_compare(&canonical, &native, depth) <= 0);
            truncations++;
            memcpy(old_bits, bits, sizeof(bits));
            memcpy(old_levels, levels, sizeof(levels));
          }
          strpm_successor(&native, cutoff, depth, k, t);
          strpm_simd_successor(&packed, cutoff, depth, k, t);
          successors++;
          equal(&native, &packed);
          for (int p = -1; p <= depth; p++) {
            assert(strpm_simd_compare(&packed, &previous, p) ==
                   pm_compare(&native, &old, p));
            comparisons++;
          }
          if (step % 3 == 0) {
            int const p = (int)(random_value() % (unsigned)depth);
            strpm_successor(&native, p, depth, k, t);
            strpm_simd_successor(&packed, p, depth, k, t);
            successors++;
            equal(&native, &packed);
          }
        }
      }
    }
  }
}
static void check_strings(void) {
  /* Independent scalar bitstring ordering, including unequal string lengths
   * and strings on either side of the priority cutoff. */
  for (unsigned na = 0; na <= 8; na++) {
    for (unsigned nb = 0; nb <= 8; nb++) {
      unsigned const A = (1U << na) - 1, B = (1U << nb) - 1;
      for (unsigned a = 0; a <= A; a++) {
        for (unsigned b = 0; b <= B; b++) {
          unsigned char abits[8], bbits[8];
          int alevels[8] = {0}, blevels[8] = {0};
          for (unsigned i = 0; i < 8; i++) {
            abits[i] = (unsigned char)((a >> i) & 1);
            bbits[i] = (unsigned char)((b >> i) & 1);
          }
          PMMeasure const x = {
              .length = (int)na, .bits = abits, .levels = alevels};
          PMMeasure const y = {
              .length = (int)nb, .bits = bbits, .levels = blevels};
          STRPMVector left, right;
          assert(strpm_simd_pack(&x, &left));
          assert(strpm_simd_pack(&y, &right));
          assert(strpm_simd_compare(&left, &right, 0) == pm_compare(&x, &y, 0));
          assert(strpm_simd_compare(&left, &right, -1) == 0);
          comparisons++;
        }
      }
    }
  }
}
int main(void) {
  assert(!strpm_simd_eligible(10, 10, 3));
  assert(!strpm_simd_eligible(10, 3, 8));
  assert(!strpm_simd_eligible(UINT16_MAX, 3, 1));
  if (!strpm_simd_available()) {
    puts("SKIP: Strahler PM SIMD requires SSE2 or NEON");
    return 77;
  }
  check_strings();
  check_successors();
  printf("Strahler PM SIMD: %zu successors, %zu truncations, %zu comparisons\n",
         successors, truncations, comparisons);
}
