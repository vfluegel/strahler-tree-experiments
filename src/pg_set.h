#ifndef PG_SET_H
#define PG_SET_H 1

#include <stddef.h>
#include <stdint.h>

typedef struct {
  size_t bit_count;
  size_t word_count;
  uint64_t *words;
} PGSet;

[[nodiscard]] bool pg_set_init(PGSet *set, size_t bit_count);
void pg_set_destroy(PGSet *set);
[[nodiscard]] bool pg_set_clone(PGSet *destination, PGSet const *source);

/* The moved-from set becomes the valid empty object {0, 0, nullptr}. */
void pg_set_move(PGSet *destination, PGSet *source);

void pg_set_clear(PGSet *set);
void pg_set_fill(PGSet *set);
void pg_set_add(PGSet *set, size_t vertex);
void pg_set_remove(PGSet *set, size_t vertex);

[[nodiscard]] bool pg_set_contains(PGSet const *set, size_t vertex);
[[nodiscard]] bool pg_set_empty(PGSet const *set);
[[nodiscard]] size_t pg_set_count(PGSet const *set);
[[nodiscard]] bool pg_set_equal(PGSet const *left, PGSet const *right);
[[nodiscard]] bool pg_set_subset(PGSet const *left, PGSet const *right);

void pg_set_union_into(PGSet *destination, PGSet const *source);
void pg_set_intersect_into(PGSet *destination, PGSet const *source);
void pg_set_subtract_into(PGSet *destination, PGSet const *source);

[[nodiscard]] size_t pg_set_next(PGSet const *set, size_t start);

#endif
