// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utrees.h"
#include "pargame.h"

static void print_usage(char *argv[static 1]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(stderr,
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
    default: /* '?' */
      print_usage(argv);
      return EXIT_FAILURE;
    }
  }

  size_t buf_size;
  char *buffer = nullptr;
  if (getline(&buffer, &buf_size, stdin) == -1) {
    fprintf(stderr, "Failed to read line!\n");
    return EXIT_FAILURE;
  }
  unsigned res = proc_pgsolver_header(buf_size, buffer);
  free(buffer);

  return EXIT_SUCCESS;
}
