#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pargame.h"

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
                           char const line[n]) [[unsequenced]] {
  assert(node != nullptr);
  char *next_space;
  node->id = strtol(line, &next_space, 10);
  if (!isspace(next_space[0])) {
    fprintf(stderr, "Expected a space after identifier %d in %s", node->id,
            line);
    return nullptr;
  }

  char *rest = next_space;
  while (isspace(rest[0])) rest++;
  printf("priority starts: %s\n", rest);
  node->prio = strtol(rest, &next_space, 10);
  if (!isspace(next_space[0])) {
    fprintf(stderr, "Expected a space after priority %d in %s", node->prio,
            line);
    return nullptr;
  }

  rest = next_space;
  while (isspace(rest[0])) rest++;
  printf("owner starts: %s\n", rest);
  node->owner = strtol(rest, &next_space, 10);
  if (!isspace(next_space[0])) {
    fprintf(stderr, "Expected a space after owned %d in %s", (int)node->owner,
            line);
    return nullptr;
  }

  return nullptr;
}
