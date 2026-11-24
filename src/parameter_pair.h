#ifndef PARAMETER_PAIR_H
#define PARAMETER_PAIR_H 1

#include <stddef.h>

/* bits is used only by succinct PM. */
typedef struct {
  size_t k;
  size_t t;
  size_t bits;
} ParameterPair;

#endif
