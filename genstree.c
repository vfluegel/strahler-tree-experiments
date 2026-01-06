#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : char { ONE = '1', ZERO = '0', EPSILON = 'e', COMMA = ',', EOS = '|' };
enum { UTREE = 0, VTREE = 1 };

#define PUSH(UVAL, KVAL, TVAL, HVAL, LENQ, MAXQ, STACK)                        \
  do {                                                                         \
    if (LENQ >= MAXQ) {                                                        \
      MAXQ = 2 * (MAXQ + 1);                                                   \
      STACK = realloc(STACK, MAXQ * sizeof(*STACK));                           \
      assert(LENQ < MAXQ);                                                     \
    }                                                                          \
    STACK[LENQ].u = UVAL;                                                      \
    STACK[LENQ].k = KVAL;                                                      \
    STACK[LENQ].t = TVAL;                                                      \
    STACK[LENQ].h = HVAL;                                                      \
    LENQ++;                                                                    \
    assert(LENQ <= MAXQ);                                                      \
  } while (0)

typedef struct Node Node;
typedef struct Node {
  int k;
  int t;
  int h;
  char u;
} Node;

void print_usage(char *argv[]) {
  fprintf(stderr, "Usage: %s -k K -t T -h H \n", argv[0]);
}

unsigned count_leaves(int k, int t, int h) {
  assert(h >= k);
  unsigned (*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  // NOTE: calloc sets all entries to zero

  size_t maxq = 0;
  size_t lenq = 0;
  Node *stack = nullptr;

  // this is the node of interest
  PUSH(UTREE, k, t, h, lenq, maxq, stack);

  while (lenq > 0) {
    Node tos = stack[lenq - 1];
    if (tos.u == UTREE && tos.h == 1 && tos.k == 1) {
      tree[UTREE][tos.k][tos.t][tos.h] = 1;
      lenq--; // pop
    } else if (tos.u == UTREE && tos.h > 1 && tos.k == 1) {
      unsigned son = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (son > 0) {
        tree[UTREE][tos.k][tos.t][tos.h] = son;
        lenq--; // pop
      } else {
        PUSH(UTREE, tos.k, tos.t, tos.h - 1, lenq, maxq, stack);
      }
    } else if (tos.h >= tos.k && tos.k >= 2 && tos.t == 0) {
      unsigned son = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (son > 0) {
        tree[(int)tos.u][tos.k][tos.t][tos.h] = son;
        lenq--; // pop
      } else {
        PUSH(UTREE, tos.k - 1, tos.t, tos.h - 1, lenq, maxq, stack);
      }
    } else if (tos.u == VTREE && tos.h >= tos.k && tos.k >= 2 && tos.t >= 1) {
      unsigned child1 = tree[VTREE][tos.k][tos.t - 1][tos.h];
      unsigned child2 = tree[UTREE][tos.k - 1][tos.t][tos.h - 1];
      if (child1 > 0 && child2 > 0) {
        tree[VTREE][tos.k][tos.t][tos.h] = child1 * 2 + child2;
        lenq--; // pop
      } else {
        PUSH(VTREE, tos.k, tos.t - 1, tos.h, lenq, maxq, stack);
        PUSH(UTREE, tos.k - 1, tos.t, tos.h - 1, lenq, maxq, stack);
      }
    } else if (tos.u == UTREE && tos.h == tos.k && tos.k >= 2) {
      unsigned son = tree[VTREE][tos.k][tos.t][tos.h];
      if (son > 0) {
        tree[UTREE][tos.k][tos.t][tos.h] = son;
        lenq--; // pop
      } else {
        PUSH(VTREE, tos.k, tos.t, tos.h, lenq, maxq, stack);
      }
    } else if (tos.u == UTREE && tos.h > tos.k && tos.k >= 2) {
      unsigned child1 = tree[VTREE][tos.k][tos.t][tos.h];
      unsigned child2 = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (child1 > 0 && child2 > 0) {
        tree[UTREE][tos.k][tos.t][tos.h] = child1 * 2 + child2;
        lenq--; // pop
      } else {
        PUSH(VTREE, tos.k, tos.t, tos.h, lenq, maxq, stack);
        PUSH(UTREE, tos.k, tos.t, tos.h - 1, lenq, maxq, stack);
      }
    } else {
      assert(false);
    }
  }

  unsigned total = tree[UTREE][k][t][h];
  printf("U^%d_{%d,%d} has %d leaves\n", k, t, h, total);

  // Epilogue
  free(stack);
  free(tree);

  return total;
}

char *prepend(size_t n, char const *pref, char const *str) {
  // overshooting: if every character is a bitstring, we need to prepend the
  // prefix to each of them, and add an end-of-string symbol
  size_t len = 1 + strlen(str) * (n + 1);
  char *res = malloc(len * sizeof(char));
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

char *concat3(char const *left, char const *midl, char const *right) {
  assert(left != nullptr);
  assert(midl != nullptr);
  assert(right != nullptr);
  size_t len = strlen(left) + strlen(midl) + strlen(right) + 1;
  char *res = malloc(len * sizeof(char));
  strcpy(res, left);
  strcat(res, midl);
  strcat(res, right);
  return res;
}

char *labels_leaves(int k, int t, int h) {
  assert(h >= k);
  char *(*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  // NOTE: Technically, the pointers are not required to be null'd at this
  // point. However, all implementations of C currently take 0-bits as nullptr
  // and so calloc does null all pointers.

  size_t maxq = 0;
  size_t lenq = 0;
  Node *stack = nullptr;

  // this is the node of interest
  PUSH(UTREE, k, t, h, lenq, maxq, stack);

  while (lenq > 0) {
    Node tos = stack[lenq - 1];
    if (tos.u == UTREE && tos.h == 1 && tos.k == 1) {
      char *lab = calloc(2, sizeof(char));
      lab[0] = EOS;
      tree[UTREE][tos.k][tos.t][tos.h] = lab;
      lenq--; // pop
    } else if (tos.u == UTREE && tos.h > 1 && tos.k == 1) {
      char *son = tree[UTREE][tos.k][tos.t][tos.h - 1];
      if (son != nullptr) {
        char pref[3] = {EPSILON, COMMA, '\0'};
        tree[UTREE][tos.k][tos.t][tos.h] = prepend(2, pref, son);
        lenq--; // pop
      } else {
        PUSH(UTREE, tos.k, tos.t, tos.h - 1, lenq, maxq, stack);
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
        lenq--; // pop
      } else {
        PUSH(UTREE, tos.k - 1, tos.t, tos.h - 1, lenq, maxq, stack);
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
        lenq--; // pop
      } else {
        PUSH(VTREE, tos.k, tos.t - 1, tos.h, lenq, maxq, stack);
        PUSH(UTREE, tos.k - 1, tos.t, tos.h - 1, lenq, maxq, stack);
      }
    } else if (tos.u == UTREE && tos.h == tos.k && tos.k >= 2) {
      char *son = tree[VTREE][tos.k][tos.t][tos.h];
      if (son != nullptr) {
        char pref[2] = {ZERO, '\0'};
        tree[UTREE][tos.k][tos.t][tos.h] = prepend(1, pref, son);
        lenq--; // pop
      } else {
        PUSH(VTREE, tos.k, tos.t, tos.h, lenq, maxq, stack);
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
        lenq--; // pop
      } else {
        PUSH(VTREE, tos.k, tos.t, tos.h, lenq, maxq, stack);
        PUSH(UTREE, tos.k, tos.t, tos.h - 1, lenq, maxq, stack);
      }
    } else {
      assert(false);
    }
  }

  // Epilogue
  free(stack);

  char *ret = malloc(sizeof(char) * strlen(tree[UTREE][k][t][h]) + 1);
  strcpy(ret, tree[UTREE][k][t][h]);
  // NOTE: This could be smarter if we kept track of everything being set not
  // to nullptr above
  for (char epu = 0; epu <= 1; epu++)
    for (unsigned epk = 0; epk <= k; epk++)
      for (unsigned ept = 0; ept <= t; ept++)
        for (unsigned eph = 0; eph <= h; eph++)
          free(tree[(int)epu][epk][ept][eph]);
  free(tree);

  return ret;
}

void print_bits(char const *labels) {
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

void print_blocks(char const *labels) {
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
  int opt;
  int k;
  int t;
  int h;
  bool kset = false;
  bool tset = false;
  bool hset = false;
  bool just_count = false;

  while ((opt = getopt(argc, argv, "k:t:h:j")) != -1) {
    switch (opt) {
    case 'j':
      just_count = true;
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
    fputs("Expected three arguments\n", stderr);
    print_usage(argv);
    return EXIT_FAILURE;
  }

  count_leaves(k, t, h);
  if (!just_count) {
    char *labels = labels_leaves(k, t, h);
    print_bits(labels);
    print_blocks(labels);
    free(labels);
  }

  return EXIT_SUCCESS;
}
