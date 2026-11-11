#ifndef LIFT_SIMD_H
#define LIFT_SIMD_H

#include <stddef.h>
#include <stdint.h>

/* These functions never execute an optional instruction before dispatch. The
 * baseline objects and wheels deliberately do not use -march=native. */
typedef enum { LIFT_PORTABLE, LIFT_AVX2, LIFT_NEON } LiftISA;
LiftISA lift_isa(void);
bool lift_ssse3(void);
char const *lift_isa_name(LiftISA isa);

typedef unsigned (*LiftBlock)(int *keys, int const *edges, size_t degree,
                              int const *owned, int const *limits);
unsigned lift_block_portable(int *keys, int const *edges, size_t degree,
                             int const *owned, int const *limits);
unsigned lift_block_avx2(int *keys, int const *edges, size_t degree,
                         int const *owned, int const *limits);
unsigned lift_block_neon(int *keys, int const *edges, size_t degree,
                         int const *owned, int const *limits);
void lift_counts_ssse3(uint16_t const masks[8], uint16_t counts[8]);
void lift_counts_neon(uint16_t const masks[8], uint16_t counts[8]);
void lift_strings_ssse3(uint16_t const a[8], uint16_t const A[8],
                        uint16_t const b[8], uint16_t const B[8], int out[8]);
void lift_strings_neon(uint16_t const a[8], uint16_t const A[8],
                       uint16_t const b[8], uint16_t const B[8], int out[8]);
unsigned lift_batch_edge(uint16_t const ranks[8], uint16_t const limits[8],
                         uint16_t pending[8]);
unsigned lift_batch_edge_x86(uint16_t const ranks[8], uint16_t const limits[8],
                             uint16_t pending[8]);
unsigned lift_batch_edge_neon(uint16_t const ranks[8], uint16_t const limits[8],
                              uint16_t pending[8]);
unsigned lift_batch_zero(uint16_t const pending[8]);
unsigned lift_batch_notify(uint16_t const ranks[8], uint16_t const reached[8],
                           uint16_t pending[8], unsigned eligible,
                           unsigned owned);
unsigned lift_batch_zero_x86(uint16_t const pending[8]);
unsigned lift_batch_notify_x86(uint16_t const ranks[8],
                               uint16_t const reached[8], uint16_t pending[8],
                               unsigned eligible, unsigned owned);
unsigned lift_batch_zero_neon(uint16_t const pending[8]);
unsigned lift_batch_notify_neon(uint16_t const ranks[8],
                                uint16_t const reached[8], uint16_t pending[8],
                                unsigned eligible, unsigned owned);
#endif
