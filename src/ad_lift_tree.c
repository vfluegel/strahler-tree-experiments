#include <stdio.h>
#include <stdlib.h>

#include "ad_lift_tree.h"

static void tree_error(ADLiftError *error, char const *message) {
  if (error != nullptr) {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

[[nodiscard]] static bool count_nodes(OrderedTreeNode const *node,
                                      size_t *count) {
  if (*count == SIZE_MAX) {
    return false;
  }
  (*count)++;
  for (size_t index = 0; index < node->child_count; index++) {
    if (!count_nodes(node->children[index].child, count)) {
      return false;
    }
  }
  return true;
}

typedef struct {
  ADLiftTree *tree;
  size_t next_node;
  size_t next_position;
} TreeBuilder;

static size_t add_position(TreeBuilder *builder, ADPosition const position) {
  size_t const index = builder->next_position++;
  builder->tree->positions[index] = position;
  return index;
}

/* The caller has checked that every level stays nonnegative, so only the
 * MINUS_INF child of a level-zero node lacks a level. */
static size_t build_node(TreeBuilder *builder, OrderedTreeNode const *base,
                         uint64_t const level, size_t const parent) {
  size_t const index = builder->next_node++;
  ADLiftNode *node = builder->tree->nodes + index;
  *node = (ADLiftNode){
      .level = level,
      .parent = parent,
      .first_child = SIZE_MAX,
      .next_sibling = SIZE_MAX,
      .child_count = base->child_count,
  };
  node->regular = add_position(builder, (ADPosition){.kind = AD_POS_REGULAR,
                                                     .has_level = true,
                                                     .level = level,
                                                     .node = index});
  node->minus_inf =
      add_position(builder, (ADPosition){.kind = AD_POS_MINUS_INF,
                                         .has_level = level != 0,
                                         .level = level == 0 ? 0 : level - 1,
                                         .node = index});

  size_t previous = SIZE_MAX;
  for (size_t child_index = 0; child_index < base->child_count; child_index++) {
    size_t const child = build_node(builder, base->children[child_index].child,
                                    level - 2, index);
    ADLiftNode *nodes = builder->tree->nodes;
    nodes[child].after = add_position(builder, (ADPosition){.kind = AD_POS_PLUS,
                                                            .has_level = true,
                                                            .level = level - 1,
                                                            .node = child});
    if (previous == SIZE_MAX) {
      nodes[index].first_child = child;
    } else {
      nodes[previous].next_sibling = child;
    }
    previous = child;
  }
  return index;
}

bool ad_lift_tree_build(OrderedTreeNode const *base, PGPlayer const player,
                        uint64_t const root_level, ADLiftTree *tree,
                        ADLiftError *error) {
  if (tree != nullptr) {
    *tree = (ADLiftTree){0};
  }
  if (base == nullptr || tree == nullptr || player > PG_ODD) {
    tree_error(error, "invalid host tree input");
    return false;
  }
  if (root_level % 2 != (uint64_t)player) {
    tree_error(error, "the host root level must have the player's parity");
    return false;
  }
  size_t const height = ordered_tree_height(base);
  if (height == SIZE_MAX || height > root_level / 2) {
    tree_error(error, "the host tree is too deep for its root level");
    return false;
  }

  size_t node_count = 0;
  if (!count_nodes(base, &node_count) || node_count > SIZE_MAX / 3 ||
      node_count > SIZE_MAX / sizeof(ADLiftNode) ||
      3 * node_count > SIZE_MAX / sizeof(ADPosition)) {
    tree_error(error, "the host tree is too large");
    return false;
  }
  /* One REGULAR and one MINUS_INF per node, one PLUS per non-root node, and
   * TOP: 3 * node_count positions in total. */
  size_t const position_count = 3 * node_count;
  tree->nodes = malloc(node_count * sizeof(tree->nodes[0]));
  tree->positions = malloc(position_count * sizeof(tree->positions[0]));
  if (tree->nodes == nullptr || tree->positions == nullptr) {
    ad_lift_tree_destroy(tree);
    tree_error(error, "failed to allocate the host tree");
    return false;
  }

  TreeBuilder builder = {.tree = tree};
  (void)build_node(&builder, base, root_level, SIZE_MAX);
  tree->top = add_position(
      &builder,
      (ADPosition){.kind = AD_POS_TOP, .has_level = false, .node = SIZE_MAX});
  tree->nodes[0].after = tree->top;
  tree->player = player;
  tree->node_count = node_count;
  tree->position_count = position_count;
  tree->height = height;
  for (size_t index = 0; index < node_count; index++) {
    if (tree->nodes[index].child_count == 0) {
      tree->leaf_count++;
    }
  }
  if (builder.next_node != node_count ||
      builder.next_position != position_count) {
    ad_lift_tree_destroy(tree);
    tree_error(error, "inconsistent host tree size");
    return false;
  }
  return true;
}

void ad_lift_tree_destroy(ADLiftTree *tree) {
  if (tree == nullptr) {
    return;
  }
  free(tree->positions);
  free(tree->nodes);
  *tree = (ADLiftTree){0};
}

bool ad_position_accepts_priority(ADLiftTree const *tree, size_t const position,
                                  uint64_t const priority) {
  if (tree == nullptr || position >= tree->position_count) {
    return false;
  }
  ADPosition const *current = tree->positions + position;
  switch (current->kind) {
  case AD_POS_REGULAR:
    return priority == current->level;
  case AD_POS_MINUS_INF:
  case AD_POS_PLUS:
    return current->has_level && priority <= current->level;
  case AD_POS_TOP:
    return true;
  }
  return false;
}

size_t ad_lift_tree_first_compatible(ADLiftTree const *tree, size_t start,
                                     uint64_t const priority) {
  /* Every position inside the block of a node η has a level at most that of
   * η, and every MINUS_INF or PLUS position below η lies at most one level
   * below η. Hence a rejecting REGULAR(η) with a low level, a rejecting
   * MINUS_INF(η), and a rejecting PLUS(c) rule out, respectively, the rest of
   * η's block, the rest of η's block, and the rest of c's parent's block. */
  if (tree == nullptr || start > tree->top) {
    return SIZE_MAX;
  }
  while (!ad_position_accepts_priority(tree, start, priority)) {
    ADPosition const *current = tree->positions + start;
    ADLiftNode const *node = tree->nodes + current->node;
    switch (current->kind) {
    case AD_POS_REGULAR:
      start = current->level < priority ? node->after : start + 1;
      break;
    case AD_POS_MINUS_INF:
      start = node->after;
      break;
    case AD_POS_PLUS:
      start = tree->nodes[node->parent].after;
      break;
    case AD_POS_TOP:
      return start;
    }
  }
  return start;
}
