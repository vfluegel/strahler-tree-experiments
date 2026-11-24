#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_commands.h"
#include "cli_version.h"
#include "json_utils.h"
#include "ordered_tree.h"
#include "strahler_tree_version.h"
#include "stree.h"
#include "utrees.h"

[[nodiscard]] static bool parse_int(char const *text, int minimum,
                                    int *result) {
  assert(text != nullptr);
  assert(result != nullptr);

  errno = 0;
  char *end = nullptr;
  long const value = strtol(text, &end, 10);
  if (text[0] == '\0' || strspn(text, "0123456789") != strlen(text) ||
      errno == ERANGE || end == text || *end != '\0' || value < minimum ||
      value > INT_MAX) {
    return false;
  }

  *result = (int)value;
  return true;
}

static void print_usage(FILE *out, char *argv[]) {
  cli_usage(out, argv[0], "");
  fputs("  --strahler=K                                Strahler parameter "
        "(required, 1 <= K <= H)\n"
        "  --height=H                                  Tree height (required, "
        "H >= 1)\n"
        "  --log-leaves=T                              Leaf-budget exponent "
        "(required, T >= 0)\n"
        "  --count                                     Print only the leaf "
        "count (default: leaf labels)\n"
        "  --count-table                               Exact CSV counts; "
        "K,T,H are maxima\n"
        "  --leaf=L                                    Print the L-th leaf "
        "(1-based)\n"
        "  --dot                                       Print the tree in DOT "
        "format (default: off)\n"
        "  --json                                      Print the tree as JSON\n"
        "  --p-level=P                                 Print the partition at "
        "p-level P (P >= 1)\n"
        "With --p-level or no output option, leaf labels are printed.\n",
        out);
}

static void print_bits(CLIIO *io, char const labels[static 1]) {
  assert(labels != nullptr);
  bool first = true;

  fputs("Bits:\n", io->out);
  for (char const *cur = labels; *cur != '\0'; cur++) {
    switch (*cur) {
    case ZERO:
    case ONE:
      fprintf(io->out, first ? "{%c" : ", %c", *cur);
      first = false;
      break;
    case EPSILON:
    case COMMA:
      break;
    case EOS:
      if (first) {
        fputs("{ ", io->out);
      }
      fputs("}\n", io->out);
      first = true;
      break;
    default:
      assert(false);
    }
  }
}

static void print_blocks(CLIIO *io, char const labels[static 1]) {
  assert(labels != nullptr);
  unsigned block = 0;
  bool first = true;

  fputs("Blocks:\n", io->out);
  for (char const *cur = labels; *cur != '\0'; cur++) {
    switch (*cur) {
    case ZERO:
    case ONE:
      fprintf(io->out, first ? "{%u" : ", %u", block);
      first = false;
      break;
    case EPSILON:
      break;
    case COMMA:
      block++;
      break;
    case EOS:
      if (first) {
        fputs("{ ", io->out);
      }
      fputs("}\n", io->out);
      block = 0;
      first = true;
      break;
    default:
      assert(false);
    }
  }
}

[[nodiscard]] static OrderedTreeNode *parse_generated_tree(CLIIO *io,
                                                           char const *labels) {
  OrderedTreeNode *tree = nullptr;
  OrderedTreeError error = {0};
  if (!ordered_tree_parse_leaf_stream(labels, &tree, &error)) {
    fprintf(io->err, "Generated an invalid tree at %zu:%zu: %s\n", error.line,
            error.column, error.message);
    return nullptr;
  }
  return tree;
}

int genstree_command(int argc, char *argv[], CLIIO *io) {
  CLIParser parser = {.index = 1};
  static struct option const long_options[] = {
      {"help", no_argument, nullptr, 1},
      {"version", no_argument, nullptr, 'v'},
      {"strahler", required_argument, nullptr, 'k'},
      {"log-leaves", required_argument, nullptr, 't'},
      {"height", required_argument, nullptr, 'h'},
      {"count", no_argument, nullptr, 'j'},
      {"count-table", no_argument, nullptr, 'C'},
      {"leaf", required_argument, nullptr, 'l'},
      {"json", no_argument, nullptr, 'J'},
      {"dot", no_argument, nullptr, 'd'},
      {"p-level", required_argument, nullptr, 'p'},
      {nullptr, 0, nullptr, 0},
  };

  int k = 0;
  int t = 0;
  int h = 0;
  int p = 0;
  int leaf_number = 0;
  bool k_set = false;
  bool t_set = false;
  bool h_set = false;
  bool just_count = false;
  bool count_table = false;
  bool print_dot = false;
  bool json = false;

  int option = 0;
  while ((option = cli_next_option(argc, argv, &parser, long_options)) != -1) {
    switch (option) {
    case 'v':
      return cli_version_to(io->out, argv[0]);
    case 1:
      print_usage(io->out, argv);
      return EXIT_SUCCESS;
    case 'l':
      if (!parse_int(parser.argument, 1, &leaf_number)) {
        return cli_error_to(io->err, argv[0],
                            "--leaf must be a positive integer");
      }
      break;
    case 'j':
      just_count = true;
      break;
    case 'C':
      count_table = true;
      break;
    case 'J':
      json = true;
      break;
    case 'd':
      print_dot = true;
      break;
    case 'p':
      if (!parse_int(parser.argument, 1, &p)) {
        return cli_error_to(io->err, argv[0],
                            "--p-level must be a positive integer");
      }
      break;
    case 'k':
      if (!parse_int(parser.argument, 1, &k)) {
        return cli_error_to(io->err, argv[0],
                            "--strahler must be a positive integer");
      }
      k_set = true;
      break;
    case 't':
      if (!parse_int(parser.argument, 0, &t)) {
        return cli_error_to(io->err, argv[0],
                            "--log-leaves must be a nonnegative integer");
      }
      t_set = true;
      break;
    case 'h':
      if (!parse_int(parser.argument, 1, &h)) {
        return cli_error_to(io->err, argv[0],
                            "--height must be a positive integer");
      }
      h_set = true;
      break;
    default:
      return cli_error_to(
          io->err, argv[0],
          "Unknown option or invalid argument; use --name=value");
    }
  }

  if (parser.index != argc) {
    return cli_error_to(io->err, argv[0],
                        "Unknown option or invalid argument; use --name=value");
  }

  if (!(k_set && t_set && h_set)) {
    return cli_error_to(io->err, argv[0],
                        "--strahler, --log-leaves and --height are required");
  }
  if (h < k) {
    return cli_error_to(io->err, argv[0],
                        "--height must be at least --strahler");
  }

  if (count_table) {
    if (json || just_count || print_dot || leaf_number > 0 || p > 0) {
      return cli_error_to(io->err, argv[0],
                          "--count-table is a separate CSV output mode");
    }
    uint64_t *counts = nullptr;
    errno = 0;
    if (!stree_count_table(k, t, h, &counts)) {
      fputs(errno == ERANGE ? "Exact leaf count exceeds uint64_t\n"
                            : "Failed to allocate exact leaf-count table\n",
            io->err);
      return EXIT_FAILURE;
    }
    fputs("h,k,t,leaves\n", io->out);
    for (int height = 1; height <= h; height++) {
      for (int bound = 1; bound <= k && bound <= height; bound++) {
        for (int budget = 0; budget <= t; budget++) {
          size_t const at = ((size_t)height * ((size_t)k + 1) + (size_t)bound) *
                                ((size_t)t + 1) +
                            (size_t)budget;
          fprintf(io->out, "%d,%d,%d,%" PRIu64 "\n", height, bound, budget,
                  counts[at]);
        }
      }
    }
    free(counts);
    return ferror(io->out) ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  if (json && (just_count || print_dot || leaf_number > 0 || p > 0)) {
    return cli_error_to(io->err, argv[0],
                        "--json is mutually exclusive with --count, "
                        "--leaf, --dot and --p-level");
  }

  unsigned const total = stree_count_leaves(k, t, h);
  if (total == 0) {
    fputs("Failed to construct the leaf-count cache\n", io->err);
    return EXIT_FAILURE;
  }

  if (leaf_number > 0) {
    if ((unsigned)leaf_number > total) {
      return cli_error_to(io->err, argv[0],
                          "--leaf exceeds the number of leaves");
    }
    if (total == UINT_MAX) {
      fputs("The tree has too many leaves to index\n", io->err);
      return EXIT_FAILURE;
    }
    char *label = stree_leaf_label(k, t, h, leaf_number);
    if (label == nullptr) {
      fputs("Failed to allocate leaf label\n", io->err);
      return EXIT_FAILURE;
    }
    print_bits(io, label);
    print_blocks(io, label);
    free(label);
    return EXIT_SUCCESS;
  }

  if (just_count) {
    fprintf(io->out, "U^%d_{%d,%d} has %s%u leaves\n", k, t, h,
            total == UINT_MAX ? "at least " : "", total);
    return EXIT_SUCCESS;
  }

  char *labels = stree_leaf_stream(k, t, h);
  if (labels == nullptr) {
    fputs("Failed to allocate leaf labels\n", io->err);
    return EXIT_FAILURE;
  }

  int result = EXIT_SUCCESS;
  if (print_dot || json || p > 0) {
    OrderedTreeNode *tree = parse_generated_tree(io, labels);
    if (tree == nullptr) {
      free(labels);
      return EXIT_FAILURE;
    }
    if (json) {
      fputc('{', io->out);
      json_header(io->out, "strahler-tree/1",
                  STRAHLER_TREE_VERSION " (git " STRAHLER_TREE_GIT ")",
                  "strahler", argc, argv);
      fprintf(io->out,
              ",\"parameters\":{\"k\":%d,\"h\":%d,\"t\":%d},\"tree\":", k, h,
              t);
      result =
          ordered_tree_write_json(io->out, tree) && fputs("}\n", io->out) >= 0
              ? EXIT_SUCCESS
              : EXIT_FAILURE;
    } else if (print_dot) {
      result =
          ordered_tree_write_dot(io->out, tree) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
      print_bits(io, labels);
      print_blocks(io, labels);
      result = ordered_tree_write_p_partition(io->out, tree, (unsigned)p)
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    ordered_tree_destroy(tree);
  } else {
    print_bits(io, labels);
    print_blocks(io, labels);
  }
  free(labels);
  return result == EXIT_SUCCESS && fflush(io->out) == 0 && !ferror(io->out)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
