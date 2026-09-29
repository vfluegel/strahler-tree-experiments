#ifndef AD_LIFT_H
#define AD_LIFT_H 1

#include <stddef.h>

#include "ad_lift_tree.h"
#include "ad_tree.h"
#include "pg_game.h"
#include "pg_set.h"

/* Attractor-decomposition lifting for the player of a host tree. One engine
 * serves both players: the Odd run is the Even run with the players swapped
 * and the regular levels made odd, which the host's player and root level
 * already encode.
 *
 * A labelling maps every active vertex to a host position of compatible
 * priority. Relative to the host's player P, a vertex v is valid when:
 *
 * - at REGULAR(η): its edges that stay below after(η) are allowed; v needs one
 *   allowed edge if P owns it and only allowed edges otherwise;
 * - at a MINUS_INF or PLUS position ℓ: P can force a visit to {μ < ℓ} while
 *   staying in {μ <= ℓ} until then;
 * - at TOP: always.
 *
 * The lifting starts from the least compatible labelling and repeatedly moves
 * invalid vertices to their next compatible position. It stops at the least
 * labelling in which every vertex is valid. */

typedef enum {
  /* Evaluate every vertex against one labelling and lift all invalid ones. */
  AD_LIFT_SCHEDULE_ROUNDS,
  /* Lift only the invalid vertex with the smallest index per step. This is
   * slow and exists to cross-check the round schedule. */
  AD_LIFT_SCHEDULE_SINGLE,
} ADLiftSchedule;

typedef struct {
  size_t rounds;
  size_t lifts;
  /* The largest position below TOP used by the final labelling, or SIZE_MAX
   * if every vertex is at TOP. */
  size_t max_position;
  size_t top_count;
} ADLiftStats;

typedef struct {
  /* Indexed by game vertex; SIZE_MAX outside the active domain. */
  size_t *labels;
  size_t vertex_count;
  /* D_P(T): the active vertices below TOP. For every host, this is a dominion
   * of the host's player. */
  PGSet dominion;
  ADLiftStats stats;
} ADLiftLabelling;

/* active must induce a total subgame. */
[[nodiscard]]
bool ad_lift_run(PGGame const *game, PGSet const *active,
                 ADLiftTree const *host, ADLiftSchedule schedule,
                 ADLiftLabelling *result, ADLiftError *error);

void ad_lift_labelling_destroy(ADLiftLabelling *labelling);

/* Check a labelling from scratch: ranges, priority compatibility, every
 * regular and lazy condition, and that the dominion is the non-TOP part. */
[[nodiscard]]
bool ad_lift_verify(PGGame const *game, PGSet const *active,
                    ADLiftTree const *host, ADLiftLabelling const *labelling,
                    ADVerifyError *error);

/* Convert the used part of the host into a classic decomposition of the
 * dominion. For a regular node η:
 *
 *   top attractor = fibre(REGULAR(η)) ∪ fibre(MINUS_INF(η)),
 *   child c       = trap Core(c), attractor Core(c) ∪ fibre(PLUS(c)),
 *
 * where Core(c) holds the vertices labelled in [regular(c), after(c)). Host
 * children with an empty block are pruned. A nonempty fibre(PLUS(c)) with an
 * empty Core(c) cannot occur in the least labelling and is reported as an
 * error. *root is nullptr when the dominion is empty. */
[[nodiscard]]
bool ad_lift_materialize(PGGame const *game, ADLiftTree const *host,
                         ADLiftLabelling const *labelling, ADNode **root,
                         ADLiftError *error);

#endif
