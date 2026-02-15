#ifndef PARGAME_H
#define PARGAME_H 1

#include <stddef.h>

[[nodiscard]]
size_t proc_pgsolver_header(size_t const n, char const line[restrict n])
    [[unsequenced]];

typedef struct PGNode {
  unsigned id;
  unsigned prio;
  unsigned char owner;
  size_t outdeg;
  unsigned *succ;
  char *name;
} PGNode;

[[nodiscard]]
PGNode *proc_pgsolver_node(PGNode *restrict node, size_t const n,
                           char const line[restrict n]) [[unsequenced]];

#endif
