#include "lift_simd.h"

LiftISA lift_isa(void) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
  if (__builtin_cpu_supports("avx2")) {
    return LIFT_AVX2;
  }
#elif defined(__aarch64__)
  return LIFT_NEON;
#endif
  return LIFT_PORTABLE;
}

bool lift_ssse3(void) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
  return __builtin_cpu_supports("ssse3") != 0;
#else
  return false;
#endif
}

char const *lift_isa_name(LiftISA const isa) {
  return isa == LIFT_AVX2 ? "avx2" : isa == LIFT_NEON ? "neon" : "portable";
}

unsigned lift_block_portable(int *keys, int const *edges, size_t const degree,
                             int const *owned, int const *limits) {
  unsigned changed = 0;
  int next[8];
  for (unsigned lane = 0; lane < 8; lane++) {
    int best = keys[edges[lane]];
    for (size_t edge = 1; edge < degree; edge++) {
      int const value = keys[edges[8 * edge + lane]];
      if (owned[lane] ? value < best : value > best) {
        best = value;
      }
    }
    next[lane] = keys[lane];
    if (best <= limits[lane] && (next[lane] & 1) != 0) {
      next[lane]--;
      changed |= 1U << lane;
    }
  }
  for (unsigned lane = 0; lane < 8; lane++) {
    keys[lane] = next[lane];
  }
  return changed;
}

unsigned lift_batch_edge(uint16_t const ranks[8], uint16_t const limits[8],
                         uint16_t pending[8]) {
#if defined(__x86_64__)
  return lift_batch_edge_x86(ranks, limits, pending);
#elif defined(__aarch64__)
  return lift_batch_edge_neon(ranks, limits, pending);
#else
  unsigned mask = 0;
  for (unsigned i = 0; i < 8; i++) {
    bool const lower = ranks[i] < limits[i];
    pending[i] += !lower;
    mask |= (unsigned)lower << i;
  }
  return mask;
#endif
}

unsigned lift_batch_zero(uint16_t const pending[8]) {
#if defined(__x86_64__)
  return lift_batch_zero_x86(pending);
#elif defined(__aarch64__)
  return lift_batch_zero_neon(pending);
#else
  unsigned mask = 0;
  for (unsigned i = 0; i < 8; i++) {
    mask |= (unsigned)(pending[i] == 0) << i;
  }
  return mask;
#endif
}
unsigned lift_batch_notify(uint16_t const ranks[8], uint16_t const reached[8],
                           uint16_t pending[8], unsigned const eligible,
                           unsigned const owned) {
#if defined(__x86_64__)
  return lift_batch_notify_x86(ranks, reached, pending, eligible, owned);
#elif defined(__aarch64__)
  return lift_batch_notify_neon(ranks, reached, pending, eligible, owned);
#else
  unsigned good = 0;
  for (unsigned i = 0; i < 8; i++) {
    unsigned const bit = 1U << i;
    if ((eligible & bit) && ranks[i] == reached[i] &&
        ((owned & bit) || --pending[i] == 0)) {
      good |= bit;
    }
  }
  return good;
#endif
}
