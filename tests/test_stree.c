#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "stree.h"

static void test_exact_table(void) {
  uint64_t *counts = nullptr;
  assert(stree_count_table(6, 6, 1000, &counts));
  for (int h = 1; h <= 10; h++) {
    for (int k = 1; k <= 6 && k <= h; k++) {
      for (int t = 0; t <= 6; t++) {
        uint64_t const exact =
            counts[((size_t)h * 7 + (size_t)k) * 7 + (size_t)t];
        assert(stree_count_leaves(k, t, h) ==
               (exact < UINT_MAX ? exact : UINT_MAX));
      }
    }
  }
  assert(counts[(1000 * 7 + 6) * 7 + 6] == UINT64_C(3557321882121489343));
  free(counts);
  errno = 0;
  assert(!stree_count_table(6, 30, 1000, &counts));
  assert(counts == nullptr && errno == ERANGE);
  assert(!stree_count_table(0, 0, 1, &counts));
}

static void test_count_leaves(void) {
  assert(stree_count_leaves(2, 1, 2) == 3);
  assert(stree_count_leaves(3, 1, 3) == 5);
  assert(stree_count_leaves(1, 2, 4) == 1);
  assert(stree_count_leaves(0, 1, 2) == 0);
  assert(stree_count_leaves(2, -1, 2) == 0);
  assert(stree_count_leaves(3, 1, 2) == 0);
  /* Large trees saturate instead of wrapping around. */
  assert(stree_count_leaves(6, 20, 12) == UINT_MAX);
  assert(stree_leaf_label(6, 20, 12, 1) == nullptr);
}

static void test_leaf_label(void) {
  char *label = stree_leaf_label(2, 1, 2, 1);
  assert(label != nullptr);
  assert(strcmp(label, "00e,|") == 0);
  free(label);

  label = stree_leaf_label(2, 1, 2, 2);
  assert(label != nullptr);
  assert(strcmp(label, "0e,|") == 0);
  free(label);

  label = stree_leaf_label(2, 1, 2, 3);
  assert(label != nullptr);
  assert(strcmp(label, "01e,|") == 0);
  free(label);

  assert(stree_leaf_label(2, 1, 1, 1) == nullptr);
  assert(stree_leaf_label(2, 1, 2, 0) == nullptr);
  assert(stree_leaf_label(2, 1, 2, 4) == nullptr);
}

static void test_leaf_stream(void) {
  char *stream = stree_leaf_stream(2, 1, 2);
  assert(stream != nullptr);
  assert(strcmp(stream, "00e,|0e,|01e,|") == 0);
  free(stream);

  assert(stree_leaf_stream(0, 1, 2) == nullptr);
  assert(stree_leaf_stream(2, -1, 2) == nullptr);
  assert(stree_leaf_stream(3, 1, 2) == nullptr);
}

int main(void) {
  assert(stree_count_leaves(1, 0, INT_MAX) == 0);
  assert(stree_leaf_label(INT_MAX, 0, INT_MAX, 1) == nullptr);
  assert(stree_leaf_stream(1, INT_MAX, 1) == nullptr);
  assert(stree_count_leaves(INT_MAX - 1, INT_MAX - 1, INT_MAX - 1) == 0);

  test_count_leaves();
  test_exact_table();
  test_leaf_label();
  test_leaf_stream();
  return 0;
}
