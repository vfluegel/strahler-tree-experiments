#ifndef ORDERED_TREE_H
#define ORDERED_TREE_H 1

#include <stddef.h>
#include <stdio.h>

typedef struct OrderedTreeNode OrderedTreeNode;

typedef struct {
  char *label;
  OrderedTreeNode *child;
} OrderedTreeChild;

struct OrderedTreeNode {
  size_t child_count;
  size_t child_capacity;
  OrderedTreeChild *children;
  size_t leaf_count;
  bool is_leaf;
};

typedef struct {
  size_t line;
  size_t column;
  char message[256];
} OrderedTreeError;

[[nodiscard]]
bool ordered_tree_parse_leaf_stream(char const *input, OrderedTreeNode **root,
                                    OrderedTreeError *error);

void ordered_tree_destroy(OrderedTreeNode *root);

[[nodiscard]]
bool ordered_tree_write_leaf_stream(FILE *out, OrderedTreeNode const *root);

[[nodiscard]]
size_t ordered_tree_height(OrderedTreeNode const *root);

[[nodiscard]]
size_t ordered_tree_leaf_count(OrderedTreeNode const *root);

[[nodiscard]]
bool ordered_tree_write_dot(FILE *out, OrderedTreeNode const *root);

[[nodiscard]]
bool ordered_tree_write_p_partition(FILE *out, OrderedTreeNode const *root,
                                    unsigned p);

#endif
