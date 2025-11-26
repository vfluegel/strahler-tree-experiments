#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void print_usage(char* argv[]) {
  fprintf(stderr, "Usage: %s -k K -t T -h H \n", argv[0]);
}

void strahler_tree(int k, int t, int h) {
  printf("K = %d, T = %d, H = %d\n", k, t, h);
  // TODO: We need a type for subtrees now 
}

int main(int argc, char* argv[]) {
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
      if (k < 0) {
        fprintf(stderr, "K must be a nonnegative integer\n");
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
      if (h < 0) {
        fprintf(stderr, "H must be a nonnegative integer\n");
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
