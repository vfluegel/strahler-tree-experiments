// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cli_version.h"
#include "ordered_tree.h"

enum { USAGE_ERROR = 2, OPT_CHECK_ORDER = 256, OPT_REORDER };

static void print_usage(FILE *out, char *argv[]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(out, "Usage: %s [--check-order|--reorder] [-h]\n", progname);
  fputs("  -h, --help     Print this message.\n", out);
  fputs("  --version      Print the program version.\n", out);
  fputs("  --check-order  Reject branches outside bitstring-vector order.\n",
        out);
  fputs("  --reorder      Sort branches into bitstring-vector order.\n", out);
  fputs("The program receives progress measures ending with '|' via stdin. "
        "Each\n",
        out);
  fputs("progress measure is a comma-separated sequence of bitstrings. The\n",
        out);
  fputs("bitstring order is infinite-binary-tree DFS order "
        "(0 beta < e < 1 beta);\n",
        out);
  fputs("vectors are compared lexicographically.\n", out);
}

int main(int argc, char *argv[argc + 1]) {
  int const version_status =
      cli_handle_version_argument(argc, argv[0], argc > 1 ? argv[1] : nullptr);
  if (version_status != CLI_VERSION_NOT_REQUESTED) {
    return version_status;
  }

  opterr = 0;
  int opt;
  OrderedTreeBranchOrder branch_order = ORDERED_TREE_PRESERVE_INPUT_ORDER;
  static struct option const long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"check-order", no_argument, nullptr, OPT_CHECK_ORDER},
      {"reorder", no_argument, nullptr, OPT_REORDER},
      {nullptr, 0, nullptr, 0},
  };

  while ((opt = getopt_long(argc, argv, "h", long_options, nullptr)) != -1) {
    switch (opt) {
    case 'h':
      print_usage(stdout, argv);
      return EXIT_SUCCESS;
    case OPT_CHECK_ORDER:
      if (branch_order == ORDERED_TREE_REORDER_VECTOR_ORDER) {
        fputs("--check-order and --reorder are mutually exclusive\n", stderr);
        print_usage(stderr, argv);
        return USAGE_ERROR;
      }
      branch_order = ORDERED_TREE_CHECK_VECTOR_ORDER;
      break;
    case OPT_REORDER:
      if (branch_order == ORDERED_TREE_CHECK_VECTOR_ORDER) {
        fputs("--check-order and --reorder are mutually exclusive\n", stderr);
        print_usage(stderr, argv);
        return USAGE_ERROR;
      }
      branch_order = ORDERED_TREE_REORDER_VECTOR_ORDER;
      break;
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
  if (!ordered_tree_parse_leaf_stream_ordered(buffer, branch_order, &tree,
                                              &error)) {
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
