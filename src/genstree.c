#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ordered_tree.h"
#include "stree.h"
#include "utrees.h"

enum { USAGE_ERROR = 2 };

[[nodiscard]] static bool parse_int(char const *text, int minimum,
                                    int *result) {
  assert(text != nullptr);
  assert(result != nullptr);

  errno = 0;
  char *end = nullptr;
  long const value = strtol(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' || value < minimum ||
      value > INT_MAX) {
    return false;
  }

  *result = (int)value;
  return true;
}

static void print_usage(FILE *out, char *argv[static 1]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(out, "Usage: %s -k K -t T -h H [-j -l L -d -p P]\n", progname);
  fputs("  --help  Print this message\n", out);
  fputs("  -j      Print only the leaf count\n", out);
  fputs("  -l L    Print the L-th leaf\n", out);
  fputs("  -d      Print the tree in DOT format\n", out);
  fputs("  -p P    Print the partition at p-level P\n", out);
  fputs("With -p or no output option, leaf labels are printed.\n", out);
}

static void print_bits(char const labels[static 1]) {
  assert(labels != nullptr);
  bool first = true;

  puts("Bits:");
  for (char const *cur = labels; *cur != '\0'; cur++) {
    switch (*cur) {
    case ZERO:
    case ONE:
      printf(first ? "{%c" : ", %c", *cur);
      first = false;
      break;
    case EPSILON:
    case COMMA:
      break;
    case EOS:
      if (first) {
        fputs("{ ", stdout);
      }
      puts("}");
      first = true;
      break;
    default:
      assert(false);
    }
  }
}

static void print_blocks(char const labels[static 1]) {
  assert(labels != nullptr);
  unsigned block = 0;
  bool first = true;

  puts("Blocks:");
  for (char const *cur = labels; *cur != '\0'; cur++) {
    switch (*cur) {
    case ZERO:
    case ONE:
      printf(first ? "{%u" : ", %u", block);
      first = false;
      break;
    case EPSILON:
      break;
    case COMMA:
      block++;
      break;
    case EOS:
      if (first) {
        fputs("{ ", stdout);
      }
      puts("}");
      block = 0;
      first = true;
      break;
    default:
      assert(false);
    }
  }
}

[[nodiscard]] static OrderedTreeNode *parse_generated_tree(char const *labels) {
  OrderedTreeNode *tree = nullptr;
  OrderedTreeError error = {0};
  if (!ordered_tree_parse_leaf_stream(labels, &tree, &error)) {
    fprintf(stderr, "Generated an invalid tree at %zu:%zu: %s\n", error.line,
            error.column, error.message);
    return nullptr;
  }
  return tree;
}

int main(int argc, char *argv[argc + 1]) {
  static struct option const long_options[] = {
      {"help", no_argument, nullptr, 1},
      {nullptr, 0, nullptr, 0},
  };

  opterr = 0;
  int k = 0;
  int t = 0;
  int h = 0;
  int p = 0;
  int leaf_number = 0;
  bool k_set = false;
  bool t_set = false;
  bool h_set = false;
  bool just_count = false;
  bool print_dot = false;

  int option = 0;
  while ((option = getopt_long(argc, argv, "k:t:h:jdp:l:", long_options,
                               nullptr)) != -1) {
    switch (option) {
    case 1:
      print_usage(stdout, argv);
      return EXIT_SUCCESS;
    case 'l':
      if (!parse_int(optarg, 1, &leaf_number)) {
        fputs("L must be a positive integer\n", stderr);
        return USAGE_ERROR;
      }
      break;
    case 'j':
      just_count = true;
      break;
    case 'd':
      print_dot = true;
      break;
    case 'p':
      if (!parse_int(optarg, 1, &p)) {
        fputs("P must be a positive integer\n", stderr);
        return USAGE_ERROR;
      }
      break;
    case 'k':
      if (!parse_int(optarg, 1, &k)) {
        fputs("K must be a positive integer\n", stderr);
        return USAGE_ERROR;
      }
      k_set = true;
      break;
    case 't':
      if (!parse_int(optarg, 0, &t)) {
        fputs("T must be a nonnegative integer\n", stderr);
        return USAGE_ERROR;
      }
      t_set = true;
      break;
    case 'h':
      if (!parse_int(optarg, 1, &h)) {
        fputs("H must be a positive integer\n", stderr);
        return USAGE_ERROR;
      }
      h_set = true;
      break;
    default:
      print_usage(stderr, argv);
      return USAGE_ERROR;
    }
  }

  if (optind != argc) {
    print_usage(stderr, argv);
    return USAGE_ERROR;
  }

  if (!(k_set && t_set && h_set)) {
    fputs("Some arguments are missing!\n", stderr);
    print_usage(stderr, argv);
    return USAGE_ERROR;
  }
  if (h < k) {
    fputs("H must be greater than or equal to K\n", stderr);
    return USAGE_ERROR;
  }

  unsigned const total = stree_count_leaves(k, t, h);
  if (total == 0) {
    fputs("Failed to construct the leaf-count cache\n", stderr);
    return EXIT_FAILURE;
  }

  if (leaf_number > 0) {
    if ((unsigned)leaf_number > total) {
      fputs("L exceeds the number of leaves\n", stderr);
      return USAGE_ERROR;
    }
    char *label = stree_leaf_label(k, t, h, leaf_number);
    if (label == nullptr) {
      fputs("Failed to allocate leaf label\n", stderr);
      return EXIT_FAILURE;
    }
    print_bits(label);
    print_blocks(label);
    free(label);
    return EXIT_SUCCESS;
  }

  if (just_count) {
    printf("U^%d_{%d,%d} has %u leaves\n", k, t, h, total);
    return EXIT_SUCCESS;
  }

  char *labels = stree_leaf_stream(k, t, h);
  if (labels == nullptr) {
    fputs("Failed to allocate leaf labels\n", stderr);
    return EXIT_FAILURE;
  }

  int result = EXIT_SUCCESS;
  if (print_dot || p > 0) {
    OrderedTreeNode *tree = parse_generated_tree(labels);
    if (tree == nullptr) {
      free(labels);
      return EXIT_FAILURE;
    }
    if (print_dot) {
      result =
          ordered_tree_write_dot(stdout, tree) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
      print_bits(labels);
      print_blocks(labels);
      result = ordered_tree_write_p_partition(stdout, tree, (unsigned)p)
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    ordered_tree_destroy(tree);
  } else {
    print_bits(labels);
    print_blocks(labels);
  }
  free(labels);
  return result;
}
