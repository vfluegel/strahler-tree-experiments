#ifndef UTREES_H
#define UTREES_H 1

enum : char { ONE = '1', ZERO = '0', EPSILON = 'e', COMMA = ',', EOS = '|' };

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
