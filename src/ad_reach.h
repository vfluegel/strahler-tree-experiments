#ifndef AD_REACH_H
#define AD_REACH_H

#include "lift_simd.h"
#include "pg_game.h"
#include "pg_set.h"

/* A read-only view of one frozen labelling, in the order of positions, never
 * node IDs. after is meaningful only at a regular position. */
typedef enum { AD_REACH_REGULAR, AD_REACH_LAZY, AD_REACH_TOP } ADReachKind;
typedef struct {
  size_t rank;
  size_t after;
  ADReachKind kind;
} ADReachLabel;

typedef struct {
  size_t snapshots, sweeps, queue_switches, dirty_visits;
  size_t symbols, byte_snapshots, byte_fallbacks, padded_edges, real_edges;
  LiftISA isa;
  double layout_seconds, dictionary_seconds;
} ADReachStats;

typedef struct ADReach ADReach;
/* Optional workspace. A null return means the original scalar algorithm can
 * still run; no error is hidden if that algorithm also cannot allocate. */
ADReach *ad_reach_create(PGGame const *game, PGSet const *domain,
                         PGPlayer player);
ADReach *ad_reach_create_backend(PGGame const *game, PGSet const *domain,
                                 PGPlayer player, char const *choice);
void ad_reach_destroy(ADReach *work);
ADReachLabel *ad_reach_labels(ADReach *work);
unsigned char const *ad_reach_good(ADReach const *work);
ADReachStats ad_reach_stats(ADReach const *work);
void ad_reach_compute(ADReach *work);
bool ad_reach_scalar_requested(void);

#endif
