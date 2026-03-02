#ifndef UTILS_H
#define UTILS_H 1

// Macro to handle pushing into a stack
#define PUSH(STACK, LENS, MAXS, ELEM)                                          \
  do {                                                                         \
    if ((LENS) == (MAXS)) {                                                    \
      (MAXS) = 2 * ((MAXS) + 1);                                               \
      (STACK) = realloc((STACK), (MAXS) * sizeof((STACK)[0]));                 \
      assert((LENS) < (MAXS));                                                 \
    }                                                                          \
    (STACK)[(LENS)] = (ELEM);                                                  \
    (LENS)++;                                                                  \
  } while (0)

#endif
