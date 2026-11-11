#ifndef AD_BATCH_H
#define AD_BATCH_H

#include "ad_lift.h"

/* Opt-in independent fixed-host requests on one induced residual arena.
 * Conditional results certify ONLY their modified arena: cutoff vertices
 * become parity(cutoff) sinks, and higher vertices opposing sinks. They must
 * never be shaved from the original game without an original-game certificate.
 */
typedef struct {
  ADLiftTree const *host;
  bool conditional;
  uint64_t cutoff;
} ADBatchRequest;

/* Build an independent native reference arena. The caller owns the result. */
bool ad_batch_arena(PGGame const *game, PGSet const *domain,
                    ADBatchRequest const *request, PGGame *arena);
bool ad_lift_batch(PGGame const *game, PGSet const *domain,
                   ADBatchRequest const *requests, size_t count,
                   ADLiftLabelling *results, ADLiftError *error);

#endif
