#ifndef UTILS_H
#define UTILS_H 1

#include <stdint.h>
#include <stdlib.h>

typedef struct {
  void *data;
  size_t capacity;
  int succeeded;
} ArrayGrowth;

[[nodiscard]] static inline ArrayGrowth grow_array(void *data, size_t capacity,
                                                   size_t element_size) {
  ArrayGrowth failure = {.data = data, .capacity = capacity, .succeeded = 0};
  if (capacity > (SIZE_MAX / 2) - 1) {
    return failure;
  }

  size_t new_capacity = 2 * (capacity + 1);
  if (element_size != 0 && new_capacity > SIZE_MAX / element_size) {
    return failure;
  }

  void *resized = realloc(data, new_capacity * element_size);
  if (resized == nullptr) {
    return failure;
  }

  return (ArrayGrowth){
      .data = resized, .capacity = new_capacity, .succeeded = 1};
}

// Append to a dynamic array, executing ON_FAILURE without losing the original
// allocation if growth fails.
#define PUSH_OR(STACK, LENS, MAXS, ELEM, ON_FAILURE)                           \
  do {                                                                         \
    if ((LENS) == (MAXS)) {                                                    \
      ArrayGrowth const push_growth =                                          \
          grow_array((STACK), (MAXS), sizeof((STACK)[0]));                     \
      if (!push_growth.succeeded) {                                            \
        ON_FAILURE;                                                            \
      }                                                                        \
      (STACK) = push_growth.data;                                              \
      (MAXS) = push_growth.capacity;                                           \
    }                                                                          \
    (STACK)[(LENS)] = (ELEM);                                                  \
    (LENS)++;                                                                  \
  } while (0)

#endif
