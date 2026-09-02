#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pargame.h"

[[nodiscard]]
size_t proc_pgsolver_header(size_t const n, char const line[restrict n]) {
  (void)n;
  if (strncmp(line, "parity ", 7) != 0) {
    fprintf(stderr, "Expected header to start with parity X; but got %s\n",
            line);
    return SIZE_MAX;
  }

  errno = 0;
  char *end = nullptr;
  uintmax_t const parsed = strtoumax(line + 7, &end, 10);
  if (errno == ERANGE || end == line + 7 || parsed >= SIZE_MAX) {
    fputs("Invalid maximal node index\n", stderr);
    return SIZE_MAX;
  }
  while (isspace((unsigned char)*end)) {
    end++;
  }
  if (*end != ';') {
    fputs("Expected a semicolon after the maximal node index\n", stderr);
    return SIZE_MAX;
  }

  size_t res = (size_t)parsed;
  printf("Maximal node index: %zu\n", res);
  return res;
}

[[nodiscard]] static bool parse_unsigned(char const *text, char **end,
                                         unsigned *result) {
  assert(text != nullptr);
  assert(end != nullptr);
  assert(result != nullptr);

  if (*text == '-') {
    return false;
  }
  errno = 0;
  uintmax_t const parsed = strtoumax(text, end, 10);
  if (errno == ERANGE || *end == text || parsed > UINT_MAX) {
    return false;
  }
  *result = (unsigned)parsed;
  return true;
}

[[nodiscard]]
PGNode *proc_pgsolver_node(PGNode *restrict node, size_t const n,
                           char const line[n]) {
  assert(node != nullptr);
  char *next_space = nullptr;
  if (!parse_unsigned(line, &next_space, &node->id) ||
      !isspace((unsigned char)next_space[0])) {
    fprintf(stderr, "Expected an unsigned identifier followed by space in %s",
            line);
    return nullptr;
  }

  char *rest = next_space;
  while (isspace((unsigned char)rest[0]))
    rest++;
  if (!parse_unsigned(rest, &next_space, &node->prio) ||
      !isspace((unsigned char)next_space[0])) {
    fprintf(stderr, "Expected an unsigned priority followed by space in %s",
            line);
    return nullptr;
  }

  rest = next_space;
  while (isspace((unsigned char)rest[0]))
    rest++;
  unsigned owner = 0;
  if (!parse_unsigned(rest, &next_space, &owner) || owner > 1 ||
      !isspace((unsigned char)next_space[0])) {
    fprintf(stderr, "Expected owner 0 or 1 followed by space in %s", line);
    return nullptr;
  }
  node->owner = (unsigned char)owner;

  return nullptr;
}
