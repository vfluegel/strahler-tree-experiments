#include "pargame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

[[nodiscard]]
size_t proc_pgsolver_header(size_t const n, char const line[restrict n])
    [[unsequenced]] {
  if (strncmp(line, "parity ", 7) != 0) {
    fprintf(stderr, "Expected header to start with parity X; but got %s\n",
            line);
    return EXIT_FAILURE;
  }

  size_t res = atoi(line + 7);
  printf("Maximal node index: %ld\n", res);
  return res;
}

[[nodiscard]]
PGNode *proc_pgsolver_node(PGNode *restrict node, size_t const n,
                           char const line[restrict n]) [[unsequenced]] {
  return nullptr;
}
