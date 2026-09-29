#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "ad_lift_tree.h"
#include "stree.h"

[[nodiscard]] static ADLiftTree build(char const *stream, PGPlayer const player,
                                      uint64_t const root_level) {
  OrderedTreeNode *base = nullptr;
  OrderedTreeError parse_error = {0};
  assert(ordered_tree_parse_leaf_stream(stream, &base, &parse_error));
  ADLiftTree tree = {0};
  ADLiftError error = {0};
  if (!ad_lift_tree_build(base, player, root_level, &tree, &error)) {
    fprintf(stderr, "host build failed: %s\n", error.message);
    assert(false);
  }
  ordered_tree_destroy(base);
  return tree;
}

static void reject(char const *stream, PGPlayer const player,
                   uint64_t const root_level) {
  OrderedTreeNode *base = nullptr;
  OrderedTreeError parse_error = {0};
  assert(ordered_tree_parse_leaf_stream(stream, &base, &parse_error));
  ADLiftTree tree = {0};
  ADLiftError error = {0};
  assert(!ad_lift_tree_build(base, player, root_level, &tree, &error));
  assert(error.message[0] != '\0');
  assert(tree.positions == nullptr && tree.nodes == nullptr);
  ordered_tree_destroy(base);
}

static void expect_position(ADLiftTree const *tree, size_t const index,
                            ADPositionKind const kind, bool const has_level,
                            uint64_t const level, size_t const node) {
  ADPosition const *position = tree->positions + index;
  assert(position->kind == kind);
  assert(position->has_level == has_level);
  assert(!has_level || position->level == level);
  assert(position->node == node);
}

static void test_two_children(void) {
  ADLiftTree tree = build("0|1|", PG_EVEN, 2);
  assert(tree.player == PG_EVEN);
  assert(tree.node_count == 3 && tree.leaf_count == 2 && tree.height == 1);
  assert(tree.position_count == 9 && tree.top == 8);

  expect_position(&tree, 0, AD_POS_REGULAR, true, 2, 0);
  expect_position(&tree, 1, AD_POS_MINUS_INF, true, 1, 0);
  expect_position(&tree, 2, AD_POS_REGULAR, true, 0, 1);
  expect_position(&tree, 3, AD_POS_MINUS_INF, false, 0, 1);
  expect_position(&tree, 4, AD_POS_PLUS, true, 1, 1);
  expect_position(&tree, 5, AD_POS_REGULAR, true, 0, 2);
  expect_position(&tree, 6, AD_POS_MINUS_INF, false, 0, 2);
  expect_position(&tree, 7, AD_POS_PLUS, true, 1, 2);
  expect_position(&tree, 8, AD_POS_TOP, false, 0, SIZE_MAX);

  ADLiftNode const *root = tree.nodes;
  assert(root->level == 2 && root->parent == SIZE_MAX);
  assert(root->regular == 0 && root->minus_inf == 1 && root->after == 8);
  assert(root->first_child == 1 && root->child_count == 2);
  assert(root->next_sibling == SIZE_MAX);
  assert(tree.nodes[1].parent == 0 && tree.nodes[1].next_sibling == 2);
  assert(tree.nodes[1].regular == 2 && tree.nodes[1].after == 4);
  assert(tree.nodes[2].first_child == SIZE_MAX);
  assert(tree.nodes[2].regular == 5 && tree.nodes[2].after == 7);

  /* Compatibility: exact at REGULAR, bounded at MINUS_INF and PLUS, nothing
   * at level -1, and everything at TOP. */
  assert(ad_position_accepts_priority(&tree, 0, 2));
  assert(!ad_position_accepts_priority(&tree, 0, 1));
  assert(!ad_position_accepts_priority(&tree, 0, 0));
  assert(ad_position_accepts_priority(&tree, 1, 0));
  assert(ad_position_accepts_priority(&tree, 1, 1));
  assert(!ad_position_accepts_priority(&tree, 1, 2));
  assert(!ad_position_accepts_priority(&tree, 3, 0));
  assert(ad_position_accepts_priority(&tree, 4, 1));
  assert(ad_position_accepts_priority(&tree, 8, UINT64_MAX));
  assert(!ad_position_accepts_priority(&tree, 9, 0));
  assert(!ad_position_accepts_priority(nullptr, 0, 0));

  assert(ad_lift_tree_first_compatible(&tree, 0, 0) == 1);
  assert(ad_lift_tree_first_compatible(&tree, 2, 0) == 2);
  assert(ad_lift_tree_first_compatible(&tree, 3, 0) == 4);
  assert(ad_lift_tree_first_compatible(&tree, 0, 1) == 1);
  assert(ad_lift_tree_first_compatible(&tree, 2, 1) == 4);
  assert(ad_lift_tree_first_compatible(&tree, 0, 2) == 0);
  assert(ad_lift_tree_first_compatible(&tree, 1, 2) == 8);
  assert(ad_lift_tree_first_compatible(&tree, 0, 5) == 8);
  assert(ad_lift_tree_first_compatible(&tree, 8, 0) == 8);
  assert(ad_lift_tree_first_compatible(&tree, 9, 0) == SIZE_MAX);
  ad_lift_tree_destroy(&tree);

  /* The Odd host with the same shape lives one level higher, so the level-1
   * leaves have a usable MINUS_INF at level 0. */
  tree = build("0|1|", PG_ODD, 3);
  expect_position(&tree, 0, AD_POS_REGULAR, true, 3, 0);
  expect_position(&tree, 3, AD_POS_MINUS_INF, true, 0, 1);
  expect_position(&tree, 4, AD_POS_PLUS, true, 2, 1);
  assert(ad_position_accepts_priority(&tree, 3, 0));
  assert(ad_lift_tree_first_compatible(&tree, 3, 0) == 3);
  ad_lift_tree_destroy(&tree);
}

static void test_plus_skips_parent_block(void) {
  /* root -> a -> {aa, ab}, root -> b -> ba with root level 4. */
  ADLiftTree tree = build("0,0|0,1|1,0|", PG_EVEN, 4);
  assert(tree.node_count == 6 && tree.leaf_count == 3 && tree.height == 2);
  size_t const a = tree.nodes[0].first_child;
  size_t const aa = tree.nodes[a].first_child;
  size_t const ab = tree.nodes[aa].next_sibling;
  size_t const b = tree.nodes[a].next_sibling;
  assert(tree.nodes[a].level == 2 && tree.nodes[aa].level == 0);
  assert(tree.nodes[ab].next_sibling == SIZE_MAX);
  assert(tree.nodes[b].child_count == 1);
  /* PLUS(aa) is at level 1; priority 2 must skip to PLUS(a) at level 3. */
  assert(tree.positions[tree.nodes[aa].after].level == 1);
  assert(ad_lift_tree_first_compatible(&tree, tree.nodes[aa].after, 2) ==
         tree.nodes[a].after);
  ad_lift_tree_destroy(&tree);
}

static void test_rejections(void) {
  reject("0|1|", PG_EVEN, 3);
  reject("0|1|", PG_EVEN, 0);
  reject("0|1|", PG_ODD, 1);
  reject("0,0|", PG_ODD, 3);

  ADLiftTree tree = {0};
  ADLiftError error = {0};
  assert(!ad_lift_tree_build(nullptr, PG_EVEN, 0, &tree, &error));
  assert(error.message[0] != '\0');

  tree = build("|", PG_EVEN, 0);
  assert(tree.node_count == 1 && tree.leaf_count == 1 && tree.height == 0);
  assert(tree.position_count == 3 && tree.top == 2);
  assert(ad_lift_tree_first_compatible(&tree, 0, 0) == 0);
  assert(ad_lift_tree_first_compatible(&tree, 1, 0) == 2);
  ad_lift_tree_destroy(&tree);
  ad_lift_tree_destroy(nullptr);
}

[[nodiscard]] static size_t linear_first_compatible(ADLiftTree const *tree,
                                                    size_t start,
                                                    uint64_t const priority) {
  while (!ad_position_accepts_priority(tree, start, priority)) {
    start++;
  }
  return start;
}

static void check_invariants(ADLiftTree const *tree, uint64_t const root) {
  assert(tree->position_count == 3 * tree->node_count);
  assert(tree->positions[tree->top].kind == AD_POS_TOP);
  for (size_t index = 0; index < tree->node_count; index++) {
    ADLiftNode const *node = tree->nodes + index;
    assert(node->regular < node->minus_inf && node->minus_inf < node->after);
    assert(tree->positions[node->regular].node == index);
    assert(tree->positions[node->minus_inf].node == index);
    if (index == 0) {
      assert(node->level == root && node->after == tree->top);
      continue;
    }
    ADLiftNode const *parent = tree->nodes + node->parent;
    assert(node->level + 2 == parent->level);
    assert(parent->regular < node->regular && node->after < parent->after);
    assert(tree->positions[node->after].kind == AD_POS_PLUS);
    assert(tree->positions[node->after].node == index);
    /* A subtree's block contains exactly its descendants' positions. */
    for (size_t position = node->regular; position < node->after; position++) {
      size_t owner = tree->positions[position].node;
      if (tree->positions[position].kind == AD_POS_PLUS) {
        owner = tree->nodes[owner].parent;
      }
      while (owner != index && owner != SIZE_MAX) {
        owner = tree->nodes[owner].parent;
      }
      assert(owner == index);
      assert(!tree->positions[position].has_level ||
             tree->positions[position].level <= node->level);
    }
  }
  for (uint64_t priority = 0; priority <= root + 1; priority++) {
    for (size_t start = 0; start <= tree->top; start++) {
      assert(ad_lift_tree_first_compatible(tree, start, priority) ==
             linear_first_compatible(tree, start, priority));
    }
  }
}

static void test_strahler_hosts(void) {
  int const cases[][3] = {{1, 2, 3}, {2, 1, 2}, {2, 2, 3}, {3, 2, 4}};
  for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
    char *stream =
        stree_leaf_stream(cases[index][0], cases[index][1], cases[index][2]);
    assert(stream != nullptr);
    for (size_t player = 0; player < 2; player++) {
      uint64_t const root = 2 * (uint64_t)(cases[index][2] - 1) + player;
      ADLiftTree tree = build(stream, (PGPlayer)player, root);
      assert(tree.leaf_count == stree_count_leaves(cases[index][0],
                                                   cases[index][1],
                                                   cases[index][2]));
      assert(tree.height + 1 == (size_t)cases[index][2]);
      check_invariants(&tree, root);
      ad_lift_tree_destroy(&tree);
    }
    free(stream);
  }
}

int main(void) {
  test_two_children();
  test_plus_skips_parent_block();
  test_rejections();
  test_strahler_hosts();
  return 0;
}
