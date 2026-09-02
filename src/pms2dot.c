// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ordered_tree.h"

enum { USAGE_ERROR = 2 };

static void print_usage(FILE *out, char *argv[]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(
      out,
      "Usage: %s [-h] reads progress measures and prints their prefix tree.\n",
      progname);
  fputs("-h\t Prints this message.\n", out);
  fputs("The program receives progress measures ending with '|' via stdin. "
        "Each\n",
        out);
  fputs("progress measure is a comma-separated sequence of bits.\n", out);
}

int main(int argc, char *argv[argc + 1]) {
  opterr = 0;
  int opt;

  while ((opt = getopt(argc, argv, "h")) != -1) {
    switch (opt) {
    case 'h':
      print_usage(stdout, argv);
      return EXIT_SUCCESS;
    default: /* '?' */
      print_usage(stderr, argv);
      return USAGE_ERROR;
    }
  }
  if (optind != argc) {
    print_usage(stderr, argv);
    return USAGE_ERROR;
  }

  size_t buf_size = 0;
  char *buffer = nullptr;
  if (getline(&buffer, &buf_size, stdin) == -1) {
    free(buffer);
    fputs("Failed to read line!\n", stderr);
    return EXIT_FAILURE;
  }

  OrderedTreeNode *tree = nullptr;
  OrderedTreeError error = {0};
  if (!ordered_tree_parse_leaf_stream(buffer, &tree, &error)) {
    fprintf(stderr, "Invalid leaf stream at %zu:%zu: %s\n", error.line,
            error.column, error.message);
    free(buffer);
    return EXIT_FAILURE;
  }

  bool const wrote_tree = ordered_tree_write_dot(stdout, tree);
  ordered_tree_destroy(tree);
  free(buffer);
  if (!wrote_tree) {
    fputs("Failed to write DOT output\n", stderr);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
