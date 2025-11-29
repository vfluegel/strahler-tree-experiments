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

void push(short u, int k, int t, int h, size_t* lenq, size_t* maxq, Node** stack) {
  if (*lenq >= *maxq) {
    (*maxq) = 2 * ((*maxq) + 1);
    (*stack) = realloc(*stack, (*maxq) * sizeof(Node));
    assert (*lenq < *maxq);
  }
  Node* fresh = *stack + *lenq;
  fresh->u = (u == 0);
  fresh->k = k;
  fresh->t = t;
  fresh->h = h;
  (*lenq)++;
  assert (*lenq <= *maxq);
}

void strahler_tree(int k, int t, int h) {
  assert(h >= k);
  unsigned (*tree)[k + 1][t + 1][h + 1] = calloc(2, sizeof(*tree));
  const short UTREE = 0;
  const short VTREE = 1;

  size_t maxq = 0;
  size_t lenq = 0;
  Node* stack = nullptr;

  // this is the node of interest
  push(0, k, t, h, &lenq, &maxq, &stack);

  while (lenq > 0) {
    Node* cur = stack + lenq - 1;
    if (cur->u && cur->h == 1 && cur->k == 1) {
      tree[UTREE][cur->k][cur->t][cur->h] = 1;
      lenq--; // pop
    } else if (cur->u && cur->h > 1 && cur->k == 1) {
      unsigned son = tree[UTREE][cur->k][cur->t][cur->h - 1];
      if (son > 0) {
        tree[UTREE][cur->k][cur->t][cur->h] = son;
        lenq--; // pop
      } else {
        push(0, cur->k, cur->t, cur->h - 1, &lenq, &maxq, &stack);
      }
    } else if (cur->h >= cur->k && cur->k >= 2 && cur->t == 0) {
      unsigned son = tree[UTREE][cur->k - 1][cur->t][cur->h - 1];
      if (son > 0) {
        tree[cur->u ? 0 : 1][cur->k][cur->t][cur->h] = son;
        lenq--; // pop
      } else {
        push(0, cur->k - 1, cur->t, cur->h - 1, &lenq, &maxq, &stack);
      }
    } else if (!cur->u && cur->h >= cur->k && cur->k >= 2 && cur->t >= 1) {
      unsigned child1 = tree[VTREE][cur->k][cur->t - 1][cur->h];
      unsigned child2 = tree[UTREE][cur->k - 1][cur->t][cur->h - 1];
      if (child1 > 0 && child2 > 0) {
        tree[VTREE][cur->k][cur->t][cur->h] = child1 * 2 + child2;
        lenq--; // pop
      } else {
       push(1, cur->k, cur->t - 1, cur->h, &lenq, &maxq, &stack);
       push(0, cur->k - 1, cur->t, cur->h - 1, &lenq, &maxq, &stack);
      }
    } else if (cur->u && cur->h == cur->k && cur->k >= 2) {
      unsigned son = tree[VTREE][cur->k][cur->t][cur->h];
      if (son > 0) {
        tree[UTREE][cur->k][cur->t][cur->h] = son;
        lenq--; // pop
      } else {
        push(1, cur->k, cur->t, cur->h, &lenq, &maxq, &stack);
      }
    } else if (cur->u && cur->h > cur->k && cur->k >= 2) {
      unsigned child1 = tree[VTREE][cur->k][cur->t][cur->h];
      unsigned child2 = tree[UTREE][cur->k][cur->t][cur->h - 1];
      if (child1 > 0 && child2 > 0) {
        tree[UTREE][cur->k][cur->t][cur->h] = child1 * 2 + child2;
        lenq--; // pop
      } else {
       push(1, cur->k, cur->t, cur->h, &lenq, &maxq, &stack);
       push(0, cur->k, cur->t, cur->h - 1, &lenq, &maxq, &stack);
      }
    } else {
      assert (false);
    }    
  }

  unsigned total = tree[UTREE][k][t][h];
  printf("U^%d_{%d,%d} has %d leaves\n", k, t, h, total);

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
