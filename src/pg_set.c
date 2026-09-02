#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pg_set.h"

enum { WORD_BITS = 64 };

static void mask_last_word(PGSet *set) {
  size_t const remainder = set->bit_count % WORD_BITS;
  if (set->word_count != 0 && remainder != 0) {
    set->words[set->word_count - 1] &= (UINT64_C(1) << remainder) - 1;
  }
}

[[nodiscard]] static bool compatible(PGSet const *left, PGSet const *right) {
  return left != nullptr && right != nullptr &&
         left->bit_count == right->bit_count &&
         left->word_count == right->word_count;
}

bool pg_set_init(PGSet *set, size_t const bit_count) {
  if (set == nullptr || bit_count > SIZE_MAX - (WORD_BITS - 1)) {
    return false;
  }
  size_t const word_count = (bit_count + WORD_BITS - 1) / WORD_BITS;
  if (word_count > SIZE_MAX / sizeof(uint64_t)) {
    return false;
  }
  uint64_t *words =
      word_count == 0 ? nullptr : calloc(word_count, sizeof(words[0]));
  if (word_count != 0 && words == nullptr) {
    return false;
  }
  *set =
      (PGSet){.bit_count = bit_count, .word_count = word_count, .words = words};
  return true;
}

void pg_set_destroy(PGSet *set) {
  if (set == nullptr) {
    return;
  }
  free(set->words);
  *set = (PGSet){0};
}

bool pg_set_clone(PGSet *destination, PGSet const *source) {
  if (destination == nullptr || source == nullptr ||
      (source->word_count != 0 && source->words == nullptr)) {
    return false;
  }
  PGSet copy = {0};
  if (!pg_set_init(&copy, source->bit_count)) {
    return false;
  }
  if (source->word_count != 0) {
    if (copy.words == nullptr) {
      pg_set_destroy(&copy);
      return false;
    }
    memcpy(copy.words, source->words,
           source->word_count * sizeof(source->words[0]));
  }
  pg_set_move(destination, &copy);
  return true;
}

void pg_set_move(PGSet *destination, PGSet *source) {
  assert(destination != nullptr);
  assert(source != nullptr);
  if (destination == source) {
    return;
  }
  pg_set_destroy(destination);
  *destination = *source;
  *source = (PGSet){0};
}

void pg_set_clear(PGSet *set) {
  assert(set != nullptr);
  if (set->word_count != 0) {
    memset(set->words, 0, set->word_count * sizeof(set->words[0]));
  }
}

void pg_set_fill(PGSet *set) {
  assert(set != nullptr);
  if (set->word_count != 0) {
    memset(set->words, 0xff, set->word_count * sizeof(set->words[0]));
    mask_last_word(set);
  }
}

void pg_set_add(PGSet *set, size_t const vertex) {
  assert(set != nullptr && vertex < set->bit_count);
  set->words[vertex / WORD_BITS] |= UINT64_C(1) << (vertex % WORD_BITS);
}

void pg_set_remove(PGSet *set, size_t const vertex) {
  assert(set != nullptr && vertex < set->bit_count);
  set->words[vertex / WORD_BITS] &= ~(UINT64_C(1) << (vertex % WORD_BITS));
}

bool pg_set_contains(PGSet const *set, size_t const vertex) {
  return set != nullptr && vertex < set->bit_count &&
         (set->words[vertex / WORD_BITS] &
          (UINT64_C(1) << (vertex % WORD_BITS))) != 0;
}

bool pg_set_empty(PGSet const *set) {
  if (set == nullptr) {
    return true;
  }
  for (size_t index = 0; index < set->word_count; index++) {
    if (set->words[index] != 0) {
      return false;
    }
  }
  return true;
}

size_t pg_set_count(PGSet const *set) {
  if (set == nullptr) {
    return 0;
  }
  size_t count = 0;
  for (size_t index = 0; index < set->word_count; index++) {
    count += (size_t)__builtin_popcountll(set->words[index]);
  }
  return count;
}

bool pg_set_equal(PGSet const *left, PGSet const *right) {
  return compatible(left, right) &&
         (left->word_count == 0 ||
          memcmp(left->words, right->words,
                 left->word_count * sizeof(left->words[0])) == 0);
}

bool pg_set_subset(PGSet const *left, PGSet const *right) {
  if (!compatible(left, right)) {
    return false;
  }
  for (size_t index = 0; index < left->word_count; index++) {
    if ((left->words[index] & ~right->words[index]) != 0) {
      return false;
    }
  }
  return true;
}

void pg_set_union_into(PGSet *destination, PGSet const *source) {
  assert(compatible(destination, source));
  for (size_t index = 0; index < destination->word_count; index++) {
    destination->words[index] |= source->words[index];
  }
  mask_last_word(destination);
}

void pg_set_intersect_into(PGSet *destination, PGSet const *source) {
  assert(compatible(destination, source));
  for (size_t index = 0; index < destination->word_count; index++) {
    destination->words[index] &= source->words[index];
  }
}

void pg_set_subtract_into(PGSet *destination, PGSet const *source) {
  assert(compatible(destination, source));
  for (size_t index = 0; index < destination->word_count; index++) {
    destination->words[index] &= ~source->words[index];
  }
  mask_last_word(destination);
}

size_t pg_set_next(PGSet const *set, size_t start) {
  if (set == nullptr || start >= set->bit_count) {
    return SIZE_MAX;
  }
  size_t word_index = start / WORD_BITS;
  uint64_t word = set->words[word_index];
  word &= UINT64_MAX << (start % WORD_BITS);
  while (true) {
    if (word != 0) {
      size_t const result =
          word_index * WORD_BITS + (size_t)__builtin_ctzll(word);
      return result < set->bit_count ? result : SIZE_MAX;
    }
    word_index++;
    if (word_index == set->word_count) {
      return SIZE_MAX;
    }
    word = set->words[word_index];
  }
}
