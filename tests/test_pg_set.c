#include <assert.h>
#include <stdint.h>

#include "pg_set.h"

int main(void) {
  PGSet set = {0};
  PGSet other = {0};
  PGSet moved = {0};
  assert(pg_set_init(&set, 70));
  assert(pg_set_empty(&set));
  assert(pg_set_count(&set) == 0);
  assert(pg_set_next(&set, 0) == SIZE_MAX);

  pg_set_add(&set, 0);
  pg_set_add(&set, 63);
  pg_set_add(&set, 69);
  assert(pg_set_contains(&set, 0));
  assert(pg_set_contains(&set, 63));
  assert(pg_set_contains(&set, 69));
  assert(!pg_set_contains(&set, 70));
  assert(pg_set_count(&set) == 3);
  assert(pg_set_next(&set, 1) == 63);
  assert(pg_set_next(&set, 64) == 69);

  assert(pg_set_clone(&other, &set));
  assert(pg_set_equal(&set, &other));
  pg_set_remove(&other, 63);
  assert(pg_set_subset(&other, &set));
  assert(!pg_set_subset(&set, &other));
  pg_set_union_into(&other, &set);
  assert(pg_set_equal(&other, &set));
  pg_set_subtract_into(&other, &set);
  assert(pg_set_empty(&other));
  pg_set_fill(&other);
  assert(pg_set_count(&other) == 70);
  pg_set_intersect_into(&other, &set);
  assert(pg_set_equal(&other, &set));
  pg_set_clear(&other);
  assert(pg_set_empty(&other));

  pg_set_move(&moved, &set);
  assert(set.words == nullptr && set.bit_count == 0 && set.word_count == 0);
  assert(pg_set_count(&moved) == 3);
  pg_set_move(&moved, &moved);
  assert(pg_set_count(&moved) == 3);

  PGSet zero = {0};
  assert(pg_set_init(&zero, 0));
  pg_set_fill(&zero);
  assert(pg_set_empty(&zero));
  pg_set_destroy(&zero);
  assert(!pg_set_init(nullptr, 1));
  pg_set_destroy(nullptr);
  pg_set_destroy(&set);
  pg_set_destroy(&other);
  pg_set_destroy(&moved);
  return 0;
}
