// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utrees.h"

static void print_usage(char *argv[static 1]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(stderr,
          "Usage: %s [-h] prints information about pgsolver-format parity games\n",
          progname);
  fputs("-h\t Prints this message.\n", stderr);
  fputs("The program receives the game via stdin\n",
        stderr);
}

[[nodiscard]]
static unsigned process_header(size_t const n,
                               char const line[restrict n]) [[unsequenced]] {
  if (strncmp(line, "parity ", 7) != 0) {
    fprintf(stderr, "Expected header to start with parity X; but got %s\n", line);
    return EXIT_FAILURE;
  }

  unsigned res = atoi(line + 7);
  printf("Maximal node index: %d\n", res);
  return res;
}

int main(int argc, char *argv[argc + 1]) {
  opterr = 0;
  int opt;

  while ((opt = getopt(argc, argv, "h")) != -1) {
    switch (opt) {
    case 'h':
    default: /* '?' */
      print_usage(argv);
      return EXIT_FAILURE;
    }
  }

  size_t buf_size = 2048;
  char *buffer = malloc(buf_size);
  if (!fgets(buffer, buf_size, stdin)) {
    free(buffer);
    return EXIT_FAILURE;
  }
  unsigned res = process_header(buf_size, buffer);
  free(buffer);

  return EXIT_SUCCESS;
}
