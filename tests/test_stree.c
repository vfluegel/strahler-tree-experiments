#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "stree.h"

static void test_count_leaves(void) {
  assert(stree_count_leaves(2, 1, 2) == 3);
  assert(stree_count_leaves(3, 1, 3) == 5);
  assert(stree_count_leaves(1, 2, 4) == 1);
  assert(stree_count_leaves(0, 1, 2) == 0);
  assert(stree_count_leaves(2, -1, 2) == 0);
  assert(stree_count_leaves(3, 1, 2) == 0);
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
  test_count_leaves();
  test_leaf_label();
  test_leaf_stream();
  return 0;
}
