// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "prtstree.h"

static void print_usage(char *argv[]) {
  fprintf(stderr,
          "Usage: %s [-h] reads progress measures and prints their prefix tree.\n",
          argv[0]);
  fputs("-h\t Prints this message.\n", stderr);
  fputs("The program receives progress measures ending with '|' via stdin. Each\n",
        stderr);
  fputs("progress measure is a comma-separated sequence of bits.\n",
        stderr);
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

  unsigned total = 0;
  for (unsigned i = 0; buffer[i] != '\0'; i++)
    if (buffer[i] == EOS)
      total++;
  print_tree_dot(total, buffer);
  free(buffer);

  return EXIT_SUCCESS;
}
