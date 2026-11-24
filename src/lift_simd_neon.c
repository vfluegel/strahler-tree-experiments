#include "lift_simd.h"

#if defined(__aarch64__)
#include <arm_neon.h>
static uint16x8_t lane_mask(unsigned const bits) {
  uint16_t const lanes[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  uint16x8_t const indices = vld1q_u16(lanes);
  return vceqq_u16(vandq_u16(vdupq_n_u16((uint16_t)bits), indices), indices);
}
static unsigned lane_bits(uint16x8_t const flags) {
  uint16_t const lanes[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  return vaddvq_u16(vandq_u16(flags, vld1q_u16(lanes)));
}
unsigned lift_batch_zero_neon(uint16_t const pending[8]) {
  return lane_bits(vceqq_u16(vld1q_u16(pending), vdupq_n_u16(0)));
}
unsigned lift_batch_notify_neon(uint16_t const ranks[8],
                                uint16_t const reached[8], uint16_t pending[8],
                                unsigned const eligible, unsigned const owned) {
  uint16x8_t const match = vandq_u16(
      vceqq_u16(vld1q_u16(ranks), vld1q_u16(reached)), lane_mask(eligible));
  uint16x8_t const owner = lane_mask(owned);
  uint16x8_t const decrement =
      vandq_u16(vbicq_u16(match, owner), vdupq_n_u16(1));
  uint16x8_t const p = vsubq_u16(vld1q_u16(pending), decrement);
  vst1q_u16(pending, p);
  return lane_bits(
      vandq_u16(match, vorrq_u16(owner, vceqq_u16(p, vdupq_n_u16(0)))));
}

unsigned lift_batch_edge_neon(uint16_t const ranks[8], uint16_t const limits[8],
                              uint16_t pending[8]) {
  uint16x8_t const lower = vcltq_u16(vld1q_u16(ranks), vld1q_u16(limits));
  vst1q_u16(pending, vaddq_u16(vld1q_u16(pending),
                               vandq_u16(vmvnq_u16(lower), vdupq_n_u16(1))));
  uint16_t flags[8];
  vst1q_u16(flags, lower);
  unsigned mask = 0;
  for (unsigned i = 0; i < 8; i++) {
    mask |= (unsigned)(flags[i] != 0) << i;
  }
  return mask;
}

unsigned lift_block_neon(int *keys, int const *edges, size_t const degree,
                         int const *owned, int const *limits) {
  /* NEON has no gather: gather scalar addresses, reduce four lanes at once. */
  int next[8];
  unsigned changed = 0;
  for (unsigned half = 0; half < 8; half += 4) {
    int gathered[4];
    for (unsigned lane = 0; lane < 4; lane++) {
      gathered[lane] = keys[edges[half + lane]];
    }
    int32x4_t lo = vld1q_s32(gathered), hi = lo;
    for (size_t edge = 1; edge < degree; edge++) {
      for (unsigned lane = 0; lane < 4; lane++) {
        gathered[lane] = keys[edges[8 * edge + half + lane]];
      }
      int32x4_t const value = vld1q_s32(gathered);
      lo = vminq_s32(lo, value);
      hi = vmaxq_s32(hi, value);
    }
    int32x4_t const best =
        vbslq_s32(vreinterpretq_u32_s32(vld1q_s32(owned + half)), lo, hi);
    int32x4_t const old = vld1q_s32(keys + half);
    int32x4_t const clear = vandq_s32(
        vreinterpretq_s32_u32(vcleq_s32(best, vld1q_s32(limits + half))),
        vandq_s32(old, vdupq_n_s32(1)));
    vst1q_s32(next + half, vsubq_s32(old, clear));
  }
  for (unsigned lane = 0; lane < 8; lane++) {
    changed |= (unsigned)(next[lane] != keys[lane]) << lane;
    keys[lane] = next[lane];
  }
  return changed;
}

#endif
