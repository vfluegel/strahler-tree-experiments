#include "pargame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

[[nodiscard]]
unsigned proc_pgsolver_header(size_t const n, char const line[restrict n])
    [[unsequenced]] {
  if (strncmp(line, "parity ", 7) != 0) {
    fprintf(stderr, "Expected header to start with parity X; but got %s\n",
            line);
    return EXIT_FAILURE;
  }

  unsigned res = atoi(line + 7);
  printf("Maximal node index: %d\n", res);
  return res;
}
