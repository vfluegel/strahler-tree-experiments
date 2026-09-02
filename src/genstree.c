// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "prtstree.h"
#include "utils.h"
#include "utrees.h"

typedef enum { UTREE = 0, VTREE = 1 } TType;

[[nodiscard, maybe_unused]] static bool parse_int(char const *text, int minimum,
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

[[maybe_unused]] static void print_usage(char *argv[static 1]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(stderr, "Usage: %s -k K -t T -h H [-j -l L -d -p P]\n", progname);
  fputs("-j\t Can be used to obtain just the leaf count\n", stderr);
  fputs("-l\t L can be used to indicate interest in the L-th leaf\n", stderr);
  fputs("-d\t Indicates the tree should be printed in dot format\n", stderr);
  fputs("-p\t P can be used to indicate a p-value of interest\n", stderr);
  fputs("With -p or no options, the labels of the leaves will be printed\n",
        stderr);
}

typedef struct Node {
  int k;
  int t;
  int h;
  char u;
} Node;

/**
 * FIXME: The DFS traversal/generation in this function is used again, almost
 * identically, in a function later on (hence the repeated comments, etc.) If
 * another function comes along that requires the same, it would be best to
 * factor it out and take a callback function as argument to call on the
 * leaves.
 */
[[nodiscard]]
static unsigned count_leaves_with_cache(
    TType tree_type,
    // The indices of interest
    int const k, int const t, int const h,
    // The real dimensions of the cache
    int const kdim, int const tdim, int const hdim,
    unsigned tree[restrict 2][kdim + 1][tdim + 1][hdim + 1]) {
  // early exit?
  unsigned cached = tree[tree_type][k][t][h];
  if (cached > 0)
    return cached;

  size_t maxs = 0;
  size_t lens = 0;
  Node *stack = nullptr;

  // this is the node of interest
  Node node = {.u = tree_type, .k = k, .t = t, .h = h};
  PUSH_OR(stack, lens, maxs, node, goto allocation_failure);

  while (lens > 0) {
    Node const tos = stack[lens - 1];
    if (tos.u == UTREE && tos.h == 1 && tos.k == 1) {
      tree[UTREE][tos.k][tos.t][tos.h] = 1;
      lens--; // pop
    } else if (tos.u == UTREE && tos.h > 1 && tos.k == 1) {
      unsigned son = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (son > 0) {
        tree[UTREE][tos.k][tos.t][tos.h] = son;
        lens--; // pop
      } else {
        node.u = UTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.h >= tos.k && tos.k >= 2 && tos.t == 0) {
      unsigned son = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (son > 0) {
        tree[(int)tos.u][tos.k][tos.t][tos.h] = son;
        lens--; // pop
      } else {
        node.u = UTREE;
        node.k = tos.k - 1;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.u == VTREE && tos.h >= tos.k && tos.k >= 2 && tos.t >= 1) {
      unsigned child1 = tree[VTREE][tos.k][tos.t - 1][tos.h];
      unsigned child2 = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (child1 > 0 && child2 > 0) {
        tree[VTREE][tos.k][tos.t][tos.h] = child1 * 2 + child2;
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t - 1;
        node.h = tos.h;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
        node.u = UTREE;
        node.k = tos.k - 1;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.u == UTREE && tos.h == tos.k && tos.k >= 2) {
      unsigned son = tree[VTREE][tos.k][tos.t][tos.h];
      if (son > 0) {
        tree[UTREE][tos.k][tos.t][tos.h] = son;
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.u == UTREE && tos.h > tos.k && tos.k >= 2) {
      unsigned child1 = tree[VTREE][tos.k][tos.t][tos.h];
      unsigned child2 = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (child1 > 0 && child2 > 0) {
        tree[UTREE][tos.k][tos.t][tos.h] = child1 * 2 + child2;
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
        node.u = UTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else {
      assert(false);
    }
  }

  unsigned total = tree[tree_type][k][t][h];
  assert(total > 0);

  free(stack);
  return total;

allocation_failure:
  free(stack);
  return 0;
}

[[nodiscard]]
static unsigned count_leaves(int const k, int const t, int const h) {
  assert(h >= k);
  unsigned(*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  if (tree == nullptr) {
    return 0;
  }
  // NOTE: calloc sets all entries to zero

  unsigned total = count_leaves_with_cache(UTREE, k, t, h, k, t, h, tree);

  free(tree);
  return total;
}

[[nodiscard]]
static char *prepend(size_t const n, char const pref[restrict static n],
                     char const *restrict str) {
  // overshooting: if every character is a bitstring, we need to prepend the
  // prefix to each of them, and add an end-of-string symbol, i.e.
  // len(str) + len(str) * n + 1 = 1 + len(str) * (n + 1)
  assert(str != nullptr);
  if (n == SIZE_MAX) {
    return nullptr;
  }
  size_t const str_length = strlen(str);
  if (str_length > (SIZE_MAX - 1) / (n + 1)) {
    return nullptr;
  }
  size_t len = 1 + str_length * (n + 1);
  char *res = malloc(len);
  if (res == nullptr) {
    return nullptr;
  }
  size_t reslen = 0;
  // Now we copy the prefix, then copy up until the next EOS,
  // and repeat until end of string marker
  char const *next = str;
  while (*next != '\0') {
    // we first walk up until the next EOS
    size_t lenlab = 0;
    while (next[lenlab] != EOS)
      lenlab++;
    lenlab++;
    // now we copy
    memcpy(res + reslen, pref, n);
    memcpy(res + reslen + n, next, lenlab);
    // finally, we update the pointer to the next label
    reslen += lenlab + n;
    assert(reslen < len);
    next += lenlab;
  }
  res[reslen] = '\0';
  return res;
}

[[nodiscard]]
static char *concat3(char const left[restrict static 1],
                     char const midl[restrict static 1],
                     char const right[restrict static 1]) {
  assert(left != nullptr);
  assert(midl != nullptr);
  assert(right != nullptr);
  size_t const left_length = strlen(left);
  size_t const middle_length = strlen(midl);
  size_t const right_length = strlen(right);
  if (left_length > SIZE_MAX - middle_length ||
      left_length + middle_length > SIZE_MAX - right_length ||
      left_length + middle_length + right_length == SIZE_MAX) {
    return nullptr;
  }
  size_t len = left_length + middle_length + right_length + 1;
  char *res = malloc(len);
  if (res == nullptr) {
    return nullptr;
  }
  memcpy(res, left, left_length);
  memcpy(res + left_length, midl, middle_length);
  memcpy(res + left_length + middle_length, right, right_length + 1);
  return res;
}

static char *label_lth_leaf(int const k, int const t, int const h,
                            int const lth) {
  assert(h >= k);
  unsigned(*count_cache)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*count_cache));
  if (count_cache == nullptr) {
    return nullptr;
  }
  // From Def. 21 in "The Strahler Number of a Parity Game"
  // plus the fact that we need the end-of-string character and we explicitly
  // keep EPSILON as a character; then times 2 because we keep explicit commas
  size_t slack = 2U * ((size_t)(k - 1 + t) + (size_t)h + 1U);
  char *lab = malloc(slack);
  if (lab == nullptr) {
    free(count_cache);
    return nullptr;
  }
  char *cur = lab;
  unsigned nth = lth;

  // this is the node of interest
  Node node = {.u = UTREE, .k = k, .t = t, .h = h};

  while (true) {
    assert(slack != 0);
    if (node.u == UTREE && node.h == 1 && node.k == 1) {
      assert(nth == 1);
      cur[0] = EOS;
      cur[1] = '\0';
      break;
    } else if (node.u == UTREE && node.h > 1 && node.k == 1) {
      cur[0] = EPSILON;
      cur[1] = COMMA;
      cur += 2;
      slack -= 2;
      // move to successor now
      node.u = UTREE;
      node.k = node.k;
      node.t = node.t;
      node.h = node.h - 1;
    } else if (node.h >= node.k && node.k >= 2 && node.t == 0) {
      if (node.u == VTREE) {
        cur[0] = EPSILON;
      } else {
        cur[0] = ZERO;
      }
      cur[1] = COMMA;
      cur += 2;
      slack -= 2;
      // move to successor now
      node.u = UTREE;
      node.k = node.k - 1;
      node.t = node.t;
      node.h = node.h - 1;
    } else if (node.u == VTREE && node.h >= node.k && node.k >= 2 &&
               node.t >= 1) {
      unsigned size_child1 = count_leaves_with_cache(
          VTREE, node.k, node.t - 1, node.h, k, t, h, count_cache);
      unsigned size_child2 = count_leaves_with_cache(
          UTREE, node.k - 1, node.t, node.h - 1, k, t, h, count_cache);
      if (size_child1 == 0 || size_child2 == 0) {
        free(count_cache);
        free(lab);
        return nullptr;
      }
      // which subtree to follow?
      if (size_child1 >= nth) {
        cur[0] = ZERO;
        node.u = VTREE;
        node.k = node.k;
        node.t = node.t - 1;
        node.h = node.h;
      } else if (size_child1 + size_child2 >= nth) {
        nth -= size_child1;
        cur[0] = EPSILON;
        cur[1] = COMMA;
        cur += 1;
        slack -= 1;
        node.u = UTREE;
        node.k = node.k - 1;
        node.t = node.t;
        node.h = node.h - 1;
      } else {
        nth -= size_child1 + size_child2;
        cur[0] = ONE;
        node.u = VTREE;
        node.k = node.k;
        node.t = node.t - 1;
        node.h = node.h;
      }
      cur += 1;
      slack -= 1;
    } else if (node.u == UTREE && node.h == node.k && node.k >= 2) {
      cur[0] = ZERO;
      cur += 1;
      slack -= 1;
      // move to successor
      node.u = VTREE;
      node.k = node.k;
      node.t = node.t;
      node.h = node.h;
    } else if (node.u == UTREE && node.h > node.k && node.k >= 2) {
      unsigned size_child1 = count_leaves_with_cache(
          VTREE, node.k, node.t, node.h, k, t, h, count_cache);
      unsigned size_child2 = count_leaves_with_cache(
          UTREE, node.k, node.t, node.h - 1, k, t, h, count_cache);
      if (size_child1 == 0 || size_child2 == 0) {
        free(count_cache);
        free(lab);
        return nullptr;
      }
      // which subtree to follow?
      if (size_child1 >= nth) {
        cur[0] = ZERO;
        node.u = VTREE;
        node.k = node.k;
        node.t = node.t;
        node.h = node.h;
      } else if (size_child1 + size_child2 >= nth) {
        nth -= size_child1;
        cur[0] = EPSILON;
        cur[1] = COMMA;
        cur += 1;
        slack -= 1;
        node.u = UTREE;
        node.k = node.k;
        node.t = node.t;
        node.h = node.h - 1;
      } else {
        nth -= size_child1 + size_child2;
        cur[0] = ONE;
        node.u = VTREE;
        node.k = node.k;
        node.t = node.t;
        node.h = node.h;
      }
      cur += 1;
      slack -= 1;
    } else {
      assert(false);
    }
  }

  // Epilogue
  free(count_cache);

  return lab;
}

[[nodiscard, maybe_unused]]
static char *labels_leaves(int const k, int const t, int const h) {
  assert(h >= k);
  char *(*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  if (tree == nullptr) {
    return nullptr;
  }
  // NOTE: Technically, the pointers are not required to be null'd at this
  // point. However, all implementations of C currently take 0-bits as nullptr
  // and so calloc does null all pointers.

  size_t maxs = 0;
  size_t lens = 0;
  Node *stack = nullptr;

  // this is the node of interest
  Node node = {.u = UTREE, .k = k, .t = t, .h = h};
  PUSH_OR(stack, lens, maxs, node, goto allocation_failure);

  while (lens > 0) {
    Node const tos = stack[lens - 1];
    if (tree[(int)tos.u][tos.k][tos.t][tos.h] != nullptr) {
      lens--; // A sibling traversal already populated this shared cache entry.
      continue;
    }
    if (tos.u == UTREE && tos.h == 1 && tos.k == 1) {
      char *lab = malloc(2);
      if (lab == nullptr) {
        goto allocation_failure;
      }
      lab[0] = EOS;
      lab[1] = '\0';
      tree[UTREE][tos.k][tos.t][tos.h] = lab;
      lens--; // pop
    } else if (tos.u == UTREE && tos.h > 1 && tos.k == 1) {
      char *son = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (son != nullptr) {
        char pref[3] = {EPSILON, COMMA, '\0'};
        tree[UTREE][tos.k][tos.t][tos.h] = prepend(2, pref, son);
        if (tree[UTREE][tos.k][tos.t][tos.h] == nullptr) {
          goto allocation_failure;
        }
        lens--; // pop
      } else {
        node.u = UTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.h >= tos.k && tos.k >= 2 && tos.t == 0) {
      char *son = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (son != nullptr) {
        if (tos.u == VTREE) {
          char pref[3] = {EPSILON, COMMA, '\0'};
          tree[VTREE][tos.k][tos.t][tos.h] = prepend(2, pref, son);
          if (tree[VTREE][tos.k][tos.t][tos.h] == nullptr) {
            goto allocation_failure;
          }
        } else {
          char pref[3] = {ZERO, COMMA, '\0'};
          tree[UTREE][tos.k][tos.t][tos.h] = prepend(2, pref, son);
          if (tree[UTREE][tos.k][tos.t][tos.h] == nullptr) {
            goto allocation_failure;
          }
        }
        lens--; // pop
      } else {
        node.u = UTREE;
        node.k = tos.k - 1;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.u == VTREE && tos.h >= tos.k && tos.k >= 2 && tos.t >= 1) {
      char *child1 = tree[VTREE][tos.k][tos.t - 1][tos.h];
      char *child2 = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (child1 != nullptr && child2 != nullptr) {
        char pref[3] = {EPSILON, COMMA, '\0'};
        char *midl = prepend(2, pref, child2);
        if (midl == nullptr) {
          goto allocation_failure;
        }
        pref[0] = ZERO;
        char *left = prepend(1, pref, child1);
        if (left == nullptr) {
          free(midl);
          goto allocation_failure;
        }
        pref[0] = ONE;
        char *right = prepend(1, pref, child1);
        if (right == nullptr) {
          free(left);
          free(midl);
          goto allocation_failure;
        }
        char *combined = concat3(left, midl, right);
        free(left);
        free(midl);
        free(right);
        if (combined == nullptr) {
          goto allocation_failure;
        }
        tree[VTREE][tos.k][tos.t][tos.h] = combined;
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t - 1;
        node.h = tos.h;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
        node.u = UTREE;
        node.k = tos.k - 1;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.u == UTREE && tos.h == tos.k && tos.k >= 2) {
      char *son = tree[VTREE][tos.k][tos.t][tos.h];
      if (son != nullptr) {
        char pref[2] = {ZERO, '\0'};
        tree[UTREE][tos.k][tos.t][tos.h] = prepend(1, pref, son);
        if (tree[UTREE][tos.k][tos.t][tos.h] == nullptr) {
          goto allocation_failure;
        }
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else if (tos.u == UTREE && tos.h > tos.k && tos.k >= 2) {
      char *child1 = tree[VTREE][tos.k][tos.t][tos.h];
      char *child2 = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (child1 != nullptr && child2 != nullptr) {
        char pref[3] = {EPSILON, COMMA, '\0'};
        char *midl = prepend(2, pref, child2);
        if (midl == nullptr) {
          goto allocation_failure;
        }
        pref[0] = ZERO;
        char *left = prepend(1, pref, child1);
        if (left == nullptr) {
          free(midl);
          goto allocation_failure;
        }
        pref[0] = ONE;
        char *right = prepend(1, pref, child1);
        if (right == nullptr) {
          free(left);
          free(midl);
          goto allocation_failure;
        }
        char *combined = concat3(left, midl, right);
        free(left);
        free(midl);
        free(right);
        if (combined == nullptr) {
          goto allocation_failure;
        }
        tree[UTREE][tos.k][tos.t][tos.h] = combined;
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
        node.u = UTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH_OR(stack, lens, maxs, node, goto allocation_failure);
      }
    } else {
      assert(false);
    }
  }

  // Remember the extra byte for the end-of-string symbol
  size_t const root_length = strlen(tree[UTREE][k][t][h]);
  char *ret = malloc(root_length + 1);
  if (ret != nullptr) {
    memcpy(ret, tree[UTREE][k][t][h], root_length + 1);
  }

  free(stack);
  // FIXME: This could be smarter if we kept track of everything being set not
  // to nullptr above
  for (char epu = 0; epu <= 1; epu++)
    for (int epk = 0; epk <= k; epk++)
      for (int ept = 0; ept <= t; ept++)
        for (int eph = 0; eph <= h; eph++)
          free(tree[(int)epu][epk][ept][eph]);
  free(tree);

  return ret;

allocation_failure:
  free(stack);
  for (char epu = 0; epu <= 1; epu++)
    for (int epk = 0; epk <= k; epk++)
      for (int ept = 0; ept <= t; ept++)
        for (int eph = 0; eph <= h; eph++)
          free(tree[(int)epu][epk][ept][eph]);
  free(tree);
  return nullptr;
}

[[maybe_unused]] static void print_bits(char const labels[static 1]) {
  assert(labels != nullptr);
  bool first = true;

  puts("Bits:");
  for (char const *cur = labels; *cur != '\0'; cur++) {
    switch (*cur) {
    case ZERO:
      if (first) {
        fputs("{0", stdout);
        first = false;
      } else {
        fputs(", 0", stdout);
      }
      break;
    case ONE:
      if (first) {
        fputs("{1", stdout);
        first = false;
      } else {
        fputs(", 1", stdout);
      }
      break;
    case EPSILON:
    case COMMA:
      break;
    case EOS:
      if (first)
        fputs("{ ", stdout);
      puts("}");
      first = true;
      break;
    default:
      assert(false);
    }
  }
}

[[maybe_unused]] static void print_blocks(char const labels[static 1]) {
  assert(labels != nullptr);
  unsigned b = 0;
  bool first = true;

  puts("Blocks:");
  for (char const *cur = labels; *cur != '\0'; cur++) {
    switch (*cur) {
    case ZERO:
    case ONE:
      if (first) {
        printf("{%d", b);
        first = false;
      } else {
        printf(", %d", b);
      }
      break;
    case EPSILON:
      break;
    case COMMA:
      b++;
      break;
    case EOS:
      if (first)
        fputs("{ ", stdout);
      puts("}");
      b = 0;
      first = true;
      break;
    default:
      assert(false);
    }
  }
}

#ifndef UNIT_TEST
int main(int argc, char *argv[argc + 1]) {
  opterr = 0;
  int opt;
  int k = 0;
  int t = 0;
  int h = 0;
  int p = 0;
  int lth = 0;
  bool kset = false;
  bool tset = false;
  bool hset = false;
  bool just_count = false;
  bool print_dot = false;

  while ((opt = getopt(argc, argv, "k:t:h:jdp:l:")) != -1) {
    switch (opt) {
    case 'l':
      if (!parse_int(optarg, 1, &lth)) {
        fputs("L must be a positive integer\n", stderr);
        return EXIT_FAILURE;
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
        return EXIT_FAILURE;
      }
      break;
    case 'k':
      if (!parse_int(optarg, 1, &k)) {
        fputs("K must be a positive integer\n", stderr);
        return EXIT_FAILURE;
      }
      kset = true;
      break;
    case 't':
      if (!parse_int(optarg, 0, &t)) {
        fputs("T must be a nonnegative integer\n", stderr);
        return EXIT_FAILURE;
      }
      tset = true;
      break;
    case 'h':
      if (!parse_int(optarg, 1, &h)) {
        fputs("H must be a positive integer\n", stderr);
        return EXIT_FAILURE;
      }
      hset = true;
      break;
    default: /* '?' */
      print_usage(argv);
      return EXIT_FAILURE;
    }
  }

  if (!(kset && tset && hset)) {
    fputs("Some arguments are missing!\n", stderr);
    print_usage(argv);
    return EXIT_FAILURE;
  }

  if (lth > 0) {
    char *label = label_lth_leaf(k, t, h, lth);
    if (label == nullptr) {
      fputs("Failed to allocate leaf label\n", stderr);
      return EXIT_FAILURE;
    }
    print_bits(label);
    print_blocks(label);
    free(label);
    return EXIT_SUCCESS;
  }

  unsigned total = count_leaves(k, t, h);
  if (total == 0) {
    fputs("Failed to allocate leaf-count cache\n", stderr);
    return EXIT_FAILURE;
  }
  if (just_count) {
    printf("U^%d_{%d,%d} has %d leaves\n", k, t, h, total);
    return EXIT_SUCCESS;
  }

  char *labels = labels_leaves(k, t, h);
  if (labels == nullptr) {
    fputs("Failed to allocate leaf labels\n", stderr);
    return EXIT_FAILURE;
  }
  if (print_dot) {
    print_tree_dot(total, labels);
  } else { // Default: print labels of leaves
    print_bits(labels);
    print_blocks(labels);
    if (p > 0) {
      print_tree_ppart(total, labels, p);
    }
  }
  free(labels);

  return EXIT_SUCCESS;
}
#endif
