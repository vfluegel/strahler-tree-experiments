#ifndef ZIELONKA_H
#define ZIELONKA_H 1

#include <stddef.h>
#include <stdint.h>

#include "ad_tree.h"

typedef struct {
  char message[256];
} ZielonkaError;

[[nodiscard]]
bool zielonka_decompose(PGGame const *game, PGSet const *domain, uint64_t bound,
                        ZielonkaResult *result, ZielonkaError *error);

#endif
