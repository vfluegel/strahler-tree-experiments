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
/* PM kernels adapted from the pinned Oink artifact; see SIMD.md. */
void lift_counts_neon(uint16_t const masks[8], uint16_t counts[8]) {
  uint16x8_t const m = vld1q_u16(masks);
  uint16x8_t const n = vpaddlq_u8(vcntq_u8(vreinterpretq_u8_u16(m)));
  vst1q_u16(counts, vqsubq_u16(n, vdupq_n_u16(1)));
}

void lift_strings_neon(uint16_t const a[8], uint16_t const A[8],
                       uint16_t const b[8], uint16_t const B[8], int out[8]) {
  uint16x8_t const va = vld1q_u16(a), ma = vld1q_u16(A);
  uint16x8_t const vb = vld1q_u16(b), mb = vld1q_u16(B);
  uint16x8_t const delta = vorrq_u16(
      vandq_u16(veorq_u16(va, vb), vandq_u16(ma, mb)), veorq_u16(ma, mb));
  uint16x8_t const first = vandq_u16(delta, vsubq_u16(vdupq_n_u16(0), delta));
  uint16x8_t const takes =
      vorrq_u16(vandq_u16(va, ma), vmvnq_u16(vorrq_u16(ma, vb)));
  uint16_t d[8], wins[8];
  vst1q_u16(d, delta);
  vst1q_u16(wins, vandq_u16(takes, first));
  for (unsigned i = 0; i < 8; i++) {
    out[i] = d[i] == 0 ? 0 : wins[i] != 0 ? 1 : -1;
  }
}
#endif
