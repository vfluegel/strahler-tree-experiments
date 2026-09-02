#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ordered_tree.h"

[[nodiscard]] static OrderedTreeNode *parse(char const *input) {
  OrderedTreeNode *tree = nullptr;
  OrderedTreeError error = {0};
  bool const parsed = ordered_tree_parse_leaf_stream(input, &tree, &error);
  if (!parsed) {
    fprintf(stderr, "parse failed at %zu:%zu: %s\n", error.line, error.column,
            error.message);
  }
  assert(parsed);
  assert(tree != nullptr);
  return tree;
}

static void reject(char const *input) {
  OrderedTreeNode *tree = nullptr;
  OrderedTreeError error = {0};
  assert(!ordered_tree_parse_leaf_stream(input, &tree, &error));
  assert(tree == nullptr);
  assert(error.line > 0);
  assert(error.column > 0);
  assert(error.message[0] != '\0');
}

[[nodiscard]] static char *capture(bool (*writer)(FILE *,
                                                  OrderedTreeNode const *),
                                   OrderedTreeNode const *tree) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  assert(writer(stream, tree));
  assert(fflush(stream) == 0);
  long const length = ftell(stream);
  assert(length >= 0);
  assert(fseek(stream, 0, SEEK_SET) == 0);
  char *output = malloc((size_t)length + 1);
  assert(output != nullptr);
  assert(fread(output, 1, (size_t)length, stream) == (size_t)length);
  output[length] = '\0';
  assert(fclose(stream) == 0);
  return output;
}

static void test_trivial_tree(void) {
  OrderedTreeNode *tree = parse("|\n");
  assert(ordered_tree_leaf_count(tree) == 1);
  assert(ordered_tree_height(tree) == 0);

  char *stream = capture(ordered_tree_write_leaf_stream, tree);
  assert(strcmp(stream, "|") == 0);
  free(stream);

  char *dot = capture(ordered_tree_write_dot, tree);
  assert(strcmp(dot, "strict graph {\n\t1;\n}\n") == 0);
  free(dot);
  ordered_tree_destroy(tree);
}

static void test_stable_order_and_round_trip(void) {
  OrderedTreeNode *tree = parse("1,0|0,0|1,1|0,1|");
  assert(ordered_tree_leaf_count(tree) == 4);
  assert(ordered_tree_height(tree) == 2);

  char *stream = capture(ordered_tree_write_leaf_stream, tree);
  assert(strcmp(stream, "1,0|1,1|0,0|0,1|") == 0);

  OrderedTreeNode *round_trip = parse(stream);
  char *second_stream = capture(ordered_tree_write_leaf_stream, round_trip);
  assert(strcmp(stream, second_stream) == 0);

  free(second_stream);
  ordered_tree_destroy(round_trip);
  free(stream);
  ordered_tree_destroy(tree);
}

static void test_epsilon_normalization(void) {
  OrderedTreeNode *tree = parse("e,1|0,ee|");
  char *stream = capture(ordered_tree_write_leaf_stream, tree);
  assert(strcmp(stream, "e,1|0,e|") == 0);
  free(stream);
  ordered_tree_destroy(tree);
}

static void test_partitions(void) {
  OrderedTreeNode *tree = parse("0,0|0,1|1,0|1,1|");

  FILE *stream = tmpfile();
  assert(stream != nullptr);
  assert(ordered_tree_write_p_partition(stream, tree, 0));
  assert(fflush(stream) == 0);
  assert(fseek(stream, 0, SEEK_SET) == 0);
  char output[32] = {0};
  size_t const length = fread(output, 1, sizeof(output) - 1, stream);
  assert(length > 0);
  assert(strcmp(output, "Part 2\nPart 2\n") == 0);
  assert(fclose(stream) == 0);

  ordered_tree_destroy(tree);
}

static void test_rejections(void) {
  reject("");
  reject("0");
  reject("0,,1|");
  reject("0,x|");
  reject("e|ee|");
  reject("0|0,1|");
  reject("0,1|0|");
  reject("0| trailing");
}

int main(void) {
  test_trivial_tree();
  test_stable_order_and_round_trip();
  test_epsilon_normalization();
  test_partitions();
  test_rejections();
  return 0;
}
