#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void print_usage(char *argv[]) {
  fprintf(stderr, "Usage: %s -k K -t T -h H \n", argv[0]);
}

typedef struct _node {
  int k;
  int t;
  int h;
  short u;
} Node;

void strahler_tree(int k, int t, int h) {
  assert(h >= k);
  unsigned (*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  size_t maxq = 10;
  Node* stack = calloc(maxq, sizeof(Node));
  if (tree == nullptr || stack == nullptr) {
    fprintf(stderr, "Memory allocation error\n");
    exit(EXIT_FAILURE);
  }

  // this is the node of interest
  stack[0].k = k;
  stack[0].t = t;
  stack[0].h = h;
  stack[0].u = true;
  size_t lenq = 1;

  while (lenq > 0) {
    Node* cur = stack + lenq - 1;
    // check if it's children are nonzero and then pop, otherwise push the
    // children into the stack
  }



  // Def 21 items 2: no. of bits in total is at most k + t
  // TODO: We need a type for subtrees now

  // Epilogue
  free(stack);
  free(tree);
}

int main(int argc, char *argv[]) {
  int opt;
  int k;
  int t;
  int h;
  bool kset = false;
  bool tset = false;
  bool hset = false;

  while ((opt = getopt(argc, argv, "k:t:h:")) != -1) {
    switch (opt) {
    case 'k':
      k = atoi(optarg);
      if (k < 1) {
        fprintf(stderr, "K must be a positive integer\n");
        return EXIT_FAILURE;
      }
      kset = true;
      break;
    case 't':
      t = atoi(optarg);
      if (t < 0) {
        fprintf(stderr, "T must be a nonnegative integer\n");
        return EXIT_FAILURE;
      }
      tset = true;
      break;
    case 'h':
      h = atoi(optarg);
      if (h < 1) {
        fprintf(stderr, "H must be a positive integer\n");
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
    fprintf(stderr, "Expected three arguments\n");
    print_usage(argv);
    return EXIT_FAILURE;
  }

  // Continuing with the actual computation of the
  strahler_tree(k, t, h);

  return EXIT_SUCCESS;
}
