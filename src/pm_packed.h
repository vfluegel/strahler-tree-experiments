#ifndef PM_PACKED_H
#define PM_PACKED_H

#include "pm_lift.h"

/* One SIMD lane is one nonempty component of a single progress measure.
 * The symbol cache is internal, compact, and never a canonical PMMeasure.
 * It retains native successor tails and TOP payloads exactly. */
typedef struct {
  uint16_t bits[8], masks[8], levels[8];
  uint16_t symbols;
  int16_t symbol_levels[16];
  int length;
  unsigned count;
  bool top;
} PMPacked;

bool pm_packed_eligible(int depth, size_t k, size_t t);
bool pm_pack(PMMeasure const *from, PMPacked *to);
void pm_unpack(PMPacked const *from, PMMeasure *to);
int pm_string_compare(uint16_t a, uint16_t A, uint16_t b, uint16_t B);
int pm_packed_compare(PMPacked const *a, PMPacked const *b, int cutoff,
                      bool optimized);
void pm_packed_counts(uint16_t const masks[8], uint16_t counts[8],
                      bool optimized);
void pm_packed_successor(PMPacked *measure, int cutoff, int depth, int k, int t,
                         bool optimized);

#endif
