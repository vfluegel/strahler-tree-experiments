// We will be using getopt, and it is NOT part of the C standard, so we
// use the feature test macro for the 2008 edition of the POSIX standard

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "prtstree.h"

enum { UTREE = 0, VTREE = 1 };

#define PUSH(STACK, LENS, MAXS, ELEM)                                          \
  do {                                                                         \
    if ((LENS) == (MAXS)) {                                                    \
      (MAXS) = 2 * ((MAXS) + 1);                                               \
      (STACK) = realloc((STACK), (MAXS) * sizeof((STACK)[0]));                 \
      assert((LENS) < (MAXS));                                                 \
    }                                                                          \
    (STACK)[(LENS)] = (ELEM);                                                  \
    (LENS)++;                                                                  \
  } while (0)

static void print_usage(char *argv[static 1]) {
  fprintf(stderr, "Usage: %s -k K -t T -h H [-j -d -p P]\n", argv[0]);
  fputs("-j\t Can be used to obtain just the leaf count\n", stderr);
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

[[nodiscard]]
static unsigned count_leaves(int k, int t, int h) [[unsequenced]] {
  assert(h >= k);
  unsigned (*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  // NOTE: calloc sets all entries to zero

  size_t maxs = 0;
  size_t lens = 0;
  Node *stack = nullptr;

  // this is the node of interest
  Node node = {.u = UTREE, .k = k, .t = t, .h = h};
  PUSH(stack, lens, maxs, node);

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
        PUSH(stack, lens, maxs, node);
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
        PUSH(stack, lens, maxs, node);
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
        PUSH(stack, lens, maxs, node);
        node.u = UTREE;
        node.k = tos.k - 1;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH(stack, lens, maxs, node);
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
        PUSH(stack, lens, maxs, node);
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
        PUSH(stack, lens, maxs, node);
        node.u = UTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH(stack, lens, maxs, node);
      }
    } else {
      assert(false);
    }
  }

  unsigned total = tree[UTREE][k][t][h];

  // Epilogue
  free(stack);
  free(tree);

  return total;
}

[[nodiscard]]
static char *prepend(size_t n, char const pref[restrict static n],
                     char const *restrict str) [[unsequenced]] {
  // overshooting: if every character is a bitstring, we need to prepend the
  // prefix to each of them, and add an end-of-string symbol, i.e.
  // len(str) + len(str) * n + 1 = 1 + len(str) * (n + 1)
  size_t len = 1 + strlen(str) * (n + 1);
  char *res = malloc(len);
  size_t reslen = 0;
  // Now we copy the prefix, then copy up until the next EOS,
  // and repeat until end of string marker
  char const *next = str;
  assert(next != nullptr);
  while (*next != '\0') {
    // we first walk up until the next EOS
    size_t lenlab = 0;
    while (next[lenlab] != EOS)
      lenlab++;
    lenlab++;
    // now we copy
    strncpy(res + reslen, pref, n);
    strncpy(res + reslen + n, next, lenlab);
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
                     char const right[restrict static 1]) [[unsequenced]] {
  assert(left != nullptr);
  assert(midl != nullptr);
  assert(right != nullptr);
  size_t len = strlen(left) + strlen(midl) + strlen(right) + 1;
  char *res = malloc(len);
  strcpy(res, left);
  strcat(res, midl);
  strcat(res, right);
  return res;
}

[[nodiscard]]
static char *labels_leaves(int k, int t, int h) [[unsequenced]] {
  assert(h >= k);
  char *(*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  // NOTE: Technically, the pointers are not required to be null'd at this
  // point. However, all implementations of C currently take 0-bits as nullptr
  // and so calloc does null all pointers.

  size_t maxs = 0;
  size_t lens = 0;
  Node *stack = nullptr;

  // this is the node of interest
  Node node = {.u = UTREE, .k = k, .t = t, .h = h};
  PUSH(stack, lens, maxs, node);

  while (lens > 0) {
    Node const tos = stack[lens - 1];
    if (tos.u == UTREE && tos.h == 1 && tos.k == 1) {
      char *lab = malloc(2);
      lab[0] = EOS;
      lab[1] = '\0';
      tree[UTREE][tos.k][tos.t][tos.h] = lab;
      lens--; // pop
    } else if (tos.u == UTREE && tos.h > 1 && tos.k == 1) {
      char *son = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (son != nullptr) {
        char pref[3] = {EPSILON, COMMA, '\0'};
        tree[UTREE][tos.k][tos.t][tos.h] = prepend(2, pref, son);
        lens--; // pop
      } else {
        node.u = UTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH(stack, lens, maxs, node);
      }
    } else if (tos.h >= tos.k && tos.k >= 2 && tos.t == 0) {
      char *son = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (son != nullptr) {
        if (tos.u == VTREE) {
          char pref[3] = {EPSILON, COMMA, '\0'};
          tree[VTREE][tos.k][tos.t][tos.h] = prepend(2, pref, son);
        } else {
          char pref[3] = {ZERO, COMMA, '\0'};
          tree[UTREE][tos.k][tos.t][tos.h] = prepend(2, pref, son);
        }
        lens--; // pop
      } else {
        node.u = UTREE;
        node.k = tos.k - 1;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH(stack, lens, maxs, node);
      }
    } else if (tos.u == VTREE && tos.h >= tos.k && tos.k >= 2 && tos.t >= 1) {
      char *child1 = tree[VTREE][tos.k][tos.t - 1][tos.h];
      char *child2 = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (child1 != nullptr && child2 != nullptr) {
        char pref[3] = {EPSILON, COMMA, '\0'};
        char *midl = prepend(2, pref, child2);
        pref[0] = ZERO;
        char *left = prepend(1, pref, child1);
        pref[0] = ONE;
        char *right = prepend(1, pref, child1);
        tree[VTREE][tos.k][tos.t][tos.h] = concat3(left, midl, right);
        free(left);
        free(midl);
        free(right);
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t - 1;
        node.h = tos.h;
        PUSH(stack, lens, maxs, node);
        node.u = UTREE;
        node.k = tos.k - 1;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH(stack, lens, maxs, node);
      }
    } else if (tos.u == UTREE && tos.h == tos.k && tos.k >= 2) {
      char *son = tree[VTREE][tos.k][tos.t][tos.h];
      if (son != nullptr) {
        char pref[2] = {ZERO, '\0'};
        tree[UTREE][tos.k][tos.t][tos.h] = prepend(1, pref, son);
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h;
        PUSH(stack, lens, maxs, node);
      }
    } else if (tos.u == UTREE && tos.h > tos.k && tos.k >= 2) {
      char *child1 = tree[VTREE][tos.k][tos.t][tos.h];
      char *child2 = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (child1 != nullptr && child2 != nullptr) {
        char pref[3] = {EPSILON, COMMA, '\0'};
        char *midl = prepend(2, pref, child2);
        pref[0] = ZERO;
        char *left = prepend(1, pref, child1);
        pref[0] = ONE;
        char *right = prepend(1, pref, child1);
        tree[UTREE][tos.k][tos.t][tos.h] = concat3(left, midl, right);
        free(left);
        free(midl);
        free(right);
        lens--; // pop
      } else {
        node.u = VTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h;
        PUSH(stack, lens, maxs, node);
        node.u = UTREE;
        node.k = tos.k;
        node.t = tos.t;
        node.h = tos.h - 1;
        PUSH(stack, lens, maxs, node);
      }
    } else {
      assert(false);
    }
  }

  // Epilogue
  free(stack);

  // Remember the extra byte for the end-of-string symbol
  char *ret = malloc(strlen(tree[UTREE][k][t][h]) + 1);
  strcpy(ret, tree[UTREE][k][t][h]);
  // FIXME: This could be smarter if we kept track of everything being set not
  // to nullptr above
  for (char epu = 0; epu <= 1; epu++)
    for (int epk = 0; epk <= k; epk++)
      for (int ept = 0; ept <= t; ept++)
        for (int eph = 0; eph <= h; eph++)
          free(tree[(int)epu][epk][ept][eph]);
  free(tree);

  return ret;
}

static void print_bits(char const labels[static 1]) [[unsequenced]] {
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

static void print_blocks(char const labels[static 1]) [[unsequenced]] {
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

int main(int argc, char *argv[argc + 1]) {
  opterr = 0;
  int opt;
  int k;
  int t;
  int h;
  int p = 0;
  bool kset = false;
  bool tset = false;
  bool hset = false;
  bool just_count = false;
  bool print_dot = false;

  while ((opt = getopt(argc, argv, "k:t:h:jdp:")) != -1) {
    switch (opt) {
    case 'j':
      just_count = true;
      break;
    case 'd':
      print_dot = true;
      break;
    case 'p':
      p = atoi(optarg);
      if (p < 1) {
        fputs("P must be a positive integer\n", stderr);
        return EXIT_FAILURE;
      }
      break;
    case 'k':
      k = atoi(optarg);
      if (k < 1) {
        fputs("K must be a positive integer\n", stderr);
        return EXIT_FAILURE;
      }
      kset = true;
      break;
    case 't':
      t = atoi(optarg);
      if (t < 0) {
        fputs("T must be a nonnegative integer\n", stderr);
        return EXIT_FAILURE;
      }
      tset = true;
      break;
    case 'h':
      h = atoi(optarg);
      if (h < 1) {
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

  unsigned total = count_leaves(k, t, h);
  if (just_count) {
    printf("U^%d_{%d,%d} has %d leaves\n", k, t, h, total);
    return EXIT_SUCCESS;
  }

  char *labels = labels_leaves(k, t, h);
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
