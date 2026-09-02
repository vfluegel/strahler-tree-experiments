// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pargame.h"
#include "utrees.h"

static void print_usage(char *argv[static 1]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(
      stderr,
      "Usage: %s [-h] prints information about pgsolver-format parity games\n",
      progname);
  fputs("-h\t Prints this message.\n", stderr);
  fputs("The program receives the game via stdin.\n", stderr);
}

int main(int argc, char *argv[argc + 1]) {
  opterr = 0;
  int opt;

  while ((opt = getopt(argc, argv, "h")) != -1) {
    switch (opt) {
    case 'h':
      print_usage(argv);
      return EXIT_SUCCESS;
    default: /* '?' */
      print_usage(argv);
      return EXIT_FAILURE;
    }
  }

  size_t buf_size = 0;
  char *buffer = nullptr;
  if (getline(&buffer, &buf_size, stdin) == -1) {
    free(buffer);
    fputs("Failed to read line!\n", stderr);
    return EXIT_FAILURE;
  }
  size_t n_nodes = proc_pgsolver_header(buf_size, buffer);
  if (n_nodes == SIZE_MAX) {
    free(buffer);
    fputs("Node index is too large\n", stderr);
    return EXIT_FAILURE;
  }

  PGNode *restrict vertices = calloc(n_nodes + 1, sizeof(*vertices));
  if (vertices == nullptr) {
    free(buffer);
    fputs("Failed to allocate vertices\n", stderr);
    return EXIT_FAILURE;
  }
  for (size_t i = 0; i <= n_nodes; i++) {
    if (getline(&buffer, &buf_size, stdin) == -1) {
      free(buffer);
      free(vertices);
      fprintf(stderr, "Failed to read node spec %zu/%zu\n", i + 1, n_nodes + 1);
      return EXIT_FAILURE;
    }
    if (proc_pgsolver_node(vertices + i, buf_size, buffer) == nullptr) {
      fprintf(stderr, "Failed to parse node spec:\n%s\n", buffer);
      free(buffer);
      free(vertices);
      return EXIT_FAILURE;
    }
  }

  free(vertices);
  free(buffer);

  return EXIT_SUCCESS;
}
