#ifndef PM_LIFT_H
#define PM_LIFT_H 1

#include "parameter_queue.h"
#include "pg_attractor.h"

typedef enum { PM_STRAHLER, PM_SUCCINCT } PMAlgorithm;

typedef ParameterPair PMParameters;

typedef struct {
  bool top;
  int length;
  unsigned char *bits;
  int *levels;
} PMMeasure;

typedef struct {
  PMAlgorithm algorithm;
  PMParameters parameters;
  PGPlayer player; /* Certified winner; Oink's counter player is 1-player. */
  int depth;
  size_t vertex_count;
  size_t active_count;
  PMMeasure *measure;
  size_t *order;
  PGSet region;
  PGSet attracted; /* Incremental removal outside the non-top set. */
  size_t lifts;
  size_t attempts;
} PMResult;

typedef struct {
  PMAlgorithm algorithm;
  PMParameters parameters;
  bool fixed;
  bool incremental;
  /* Exhaust t=0..floor(log2 n) before increasing k. With incremental removal,
   * repeat a pair while it removes vertices. */
  bool lexicographic;
  bool ratio;
  /* Lexicographic start; zero k means the default 1. */
  size_t start_k;
  size_t start_t;
  bool verify;
} PMOptions;

typedef struct {
  char message[256];
} PMError;

[[nodiscard]] bool pm_lift(PGGame const *game, PGSet const *active,
                           PMAlgorithm algorithm, PMParameters parameters,
                           PGPlayer player, PMResult *result, PMError *error);
void pm_result_destroy(PMResult *result);
[[nodiscard]] int pm_compare(PMMeasure const *left, PMMeasure const *right,
                             int pindex);

/* Checks progress at the algorithm's truncation and dominion closure. */
[[nodiscard]] bool pm_verify(PGGame const *game, PGSet const *active,
                             PMResult const *result, PMError *error);

/* In incremental mode, each result is on its own active subgame. The visitor
 * is called before that round's results are destroyed. In full-game mode it
 * is called only for the selected parameters. unresolved includes neither
 * earlier rounds nor shared preprocessing's shaved vertices. */
typedef bool (*PMVisit)(PMResult const result[static 2],
                        PGSet const *unresolved, size_t round, void *context);
[[nodiscard]] bool pm_solve(PGGame const *game, PGSet const *active,
                            PMOptions const *options, PMVisit visit,
                            void *context, PMError *error);

#endif
