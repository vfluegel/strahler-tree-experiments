/* Optional x86 code lives in its own object, with per-function ISA targets.
 * It is called only after CPU and OS vector-state detection in lift_simd.c. */
#include "lift_simd.h"

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
static __m128i lane_mask(unsigned const bits) {
  __m128i const lanes = _mm_setr_epi16(1, 2, 4, 8, 16, 32, 64, 128);
  return _mm_cmpeq_epi16(_mm_and_si128(_mm_set1_epi16((short)bits), lanes),
                         lanes);
}
static unsigned lane_bits(__m128i const flags) {
  return (unsigned)_mm_movemask_epi8(
             _mm_packs_epi16(flags, _mm_setzero_si128())) &
         255U;
}
unsigned lift_batch_zero_x86(uint16_t const pending[8]) {
  return lane_bits(_mm_cmpeq_epi16(_mm_loadu_si128((__m128i const *)pending),
                                   _mm_setzero_si128()));
}
unsigned lift_batch_notify_x86(uint16_t const ranks[8],
                               uint16_t const reached[8], uint16_t pending[8],
                               unsigned const eligible, unsigned const owned) {
  __m128i const equal =
      _mm_cmpeq_epi16(_mm_loadu_si128((__m128i const *)ranks),
                      _mm_loadu_si128((__m128i const *)reached));
  __m128i const match = _mm_and_si128(equal, lane_mask(eligible));
  __m128i const owner = lane_mask(owned);
  __m128i const decrement =
      _mm_and_si128(_mm_andnot_si128(owner, match), _mm_set1_epi16(1));
  __m128i const p =
      _mm_sub_epi16(_mm_loadu_si128((__m128i const *)pending), decrement);
  _mm_storeu_si128((__m128i *)pending, p);
  return lane_bits(_mm_and_si128(
      match, _mm_or_si128(owner, _mm_cmpeq_epi16(p, _mm_setzero_si128()))));
}

unsigned lift_batch_edge_x86(uint16_t const ranks[8], uint16_t const limits[8],
                             uint16_t pending[8]) {
  __m128i const sign = _mm_set1_epi16(-32768);
  __m128i const r =
      _mm_xor_si128(_mm_loadu_si128((__m128i const *)ranks), sign);
  __m128i const t =
      _mm_xor_si128(_mm_loadu_si128((__m128i const *)limits), sign);
  __m128i const lower = _mm_cmpgt_epi16(t, r);
  __m128i const p = _mm_loadu_si128((__m128i const *)pending);
  _mm_storeu_si128(
      (__m128i *)pending,
      _mm_add_epi16(p, _mm_andnot_si128(lower, _mm_set1_epi16(1))));
  return (unsigned)_mm_movemask_epi8(
             _mm_packs_epi16(lower, _mm_setzero_si128())) &
         255U;
}

__attribute__((target("avx2"))) unsigned
lift_block_avx2(int *keys, int const *edges, size_t const degree,
                int const *owned, int const *limits) {
  /* edges contain indices relative to keys, which points at this block. */
  __m256i indices = _mm256_loadu_si256((__m256i const *)edges);
  __m256i lo = _mm256_i32gather_epi32(keys, indices, 4);
  __m256i hi = lo;
  for (size_t edge = 1; edge < degree; edge++) {
    indices = _mm256_loadu_si256((__m256i const *)(edges + 8 * edge));
    __m256i const value = _mm256_i32gather_epi32(keys, indices, 4);
    lo = _mm256_min_epi32(lo, value);
    hi = _mm256_max_epi32(hi, value);
  }
  __m256i const owner = _mm256_loadu_si256((__m256i const *)owned);
  __m256i const best = _mm256_blendv_epi8(hi, lo, owner);
  __m256i const limit = _mm256_loadu_si256((__m256i const *)limits);
  __m256i const old = _mm256_loadu_si256((__m256i const *)keys);
  __m256i const clear =
      _mm256_andnot_si256(_mm256_cmpgt_epi32(best, limit),
                          _mm256_and_si256(old, _mm256_set1_epi32(1)));
  _mm256_storeu_si256((__m256i *)keys, _mm256_xor_si256(old, clear));
  return (unsigned)_mm256_movemask_ps(
      _mm256_castsi256_ps(_mm256_cmpeq_epi32(clear, _mm256_set1_epi32(1))));
}

#endif
