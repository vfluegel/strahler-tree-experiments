#ifndef STRPM_SIMD_H
#define STRPM_SIMD_H 1

#include "pm_lift.h"

/* Oink's NodePM layout: eight strings, each at most eight bits long.
 * No scalar symbol cache is maintained between lifts. */
typedef struct {
  uint16_t bits[8], masks[8], levels[8];
  uint8_t count, padding[7];
} STRPMVector;

static_assert(sizeof(STRPMVector) == 56);

[[nodiscard]] bool strpm_simd_available(void);
[[nodiscard]] bool strpm_simd_eligible(int depth, size_t k, size_t t);
[[nodiscard]] bool strpm_simd_pack(PMMeasure const *from, STRPMVector *to);
void strpm_simd_unpack(STRPMVector const *from, PMMeasure *to);
void strpm_simd_successor(STRPMVector *measure, int cutoff, int depth, int k,
                          int t);
[[nodiscard]] int strpm_simd_compare(STRPMVector const *left,
                                     STRPMVector const *right, int cutoff);
/* Successor and comparison stay in vector registers inside a vertex lift. */
[[nodiscard]] bool strpm_simd_lift(PGGame const *game, PGSet const *active,
                                   PMResult *result, STRPMVector *measures,
                                   size_t vertex, size_t target);

#endif
