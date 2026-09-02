#include <stdio.h>
#include <stdlib.h>

#include "ad_tree.h"
#include "pg_attractor.h"
#include "utils.h"

static void verify_error(ADVerifyError *error, char const *message) {
  if (error != nullptr) {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

ADNode *ad_node_create(PGPlayer const player, uint64_t const priority_bound,
                       size_t const vertex_count) {
  if (player > PG_ODD) {
    return nullptr;
  }
  ADNode *node = calloc(1, sizeof(*node));
  if (node == nullptr || !pg_set_init(&node->top_attractor, vertex_count)) {
    free(node);
    return nullptr;
  }
  node->player = player;
  node->priority_bound = priority_bound;
  return node;
}

void ad_node_destroy(ADNode *node) {
  if (node == nullptr) {
    return;
  }
  pg_set_destroy(&node->top_attractor);
  for (size_t index = 0; index < node->child_count; index++) {
    pg_set_destroy(&node->children[index].trap);
    pg_set_destroy(&node->children[index].attractor);
    ad_node_destroy(node->children[index].subtree);
  }
  free(node->children);
  free(node);
}

bool ad_node_append_child(ADNode *parent, ADChild *child) {
  if (parent == nullptr || child == nullptr || child->subtree == nullptr) {
    return false;
  }
  if (parent->child_count == parent->child_capacity) {
    ArrayGrowth const growth = grow_array(
        parent->children, parent->child_capacity, sizeof(parent->children[0]));
    if (!growth.succeeded) {
      return false;
    }
    parent->children = growth.data;
    parent->child_capacity = growth.capacity;
  }
  parent->children[parent->child_count++] = *child;
  *child = (ADChild){0};
  return true;
}

void zielonka_result_destroy(ZielonkaResult *result) {
  if (result == nullptr) {
    return;
  }
  for (size_t player = 0; player < 2; player++) {
    pg_set_destroy(&result->winning[player]);
    ad_node_destroy(result->decomposition[player]);
  }
  *result = (ZielonkaResult){0};
}

ADTreeMetrics ad_tree_metrics(ADNode const *root) {
  if (root == nullptr) {
    return (ADTreeMetrics){0};
  }
  ADTreeMetrics result = {.nodes = 1, .height = 1};
  if (root->child_count == 0) {
    result.leaves = 1;
    result.strahler = 1;
    return result;
  }

  size_t maximum_strahler = 0;
  size_t maximum_count = 0;
  for (size_t index = 0; index < root->child_count; index++) {
    ADTreeMetrics const child = ad_tree_metrics(root->children[index].subtree);
    if (SIZE_MAX - result.nodes < child.nodes ||
        SIZE_MAX - result.leaves < child.leaves) {
      return (ADTreeMetrics){0};
    }
    result.nodes += child.nodes;
    result.leaves += child.leaves;
    if (child.height >= result.height) {
      result.height = child.height + 1;
    }
    if (child.strahler > maximum_strahler) {
      maximum_strahler = child.strahler;
      maximum_count = 1;
    } else if (child.strahler == maximum_strahler) {
      maximum_count++;
    }
  }
  result.strahler =
      maximum_count >= 2 ? maximum_strahler + 1 : maximum_strahler;
  return result;
}

[[nodiscard]] static bool priorities_at_most(PGGame const *game,
                                             PGSet const *domain,
                                             uint64_t const bound) {
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (game->vertices[vertex].priority > bound) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] static bool trap_conditions(PGGame const *game,
                                          PGSet const *residual,
                                          PGSet const *trap,
                                          PGPlayer const player) {
  for (size_t vertex = pg_set_next(trap, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(trap, vertex + 1)) {
    bool has_active = false;
    bool has_trap = false;
    bool all_in_trap = true;
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      size_t const successor = game->successors[edge];
      if (!pg_set_contains(residual, successor)) {
        continue;
      }
      has_active = true;
      if (pg_set_contains(trap, successor)) {
        has_trap = true;
      } else {
        all_in_trap = false;
      }
    }
    if (!has_active ||
        (game->vertices[vertex].owner == player ? !has_trap : !all_in_trap)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] static bool verify_node(PGGame const *game, PGSet const *domain,
                                      ADNode const *root,
                                      ADVerifyError *error) {
  /* A valid alpha/d node stores Attr_alpha^W(priority d) as its top
   * attractor. In the remaining domain, every child is a nonempty opponent
   * trap with priorities at most d-2 and a verified alpha/(d-2) subtree. Its
   * stored alpha-attractor is removed, in order, and the child attractors must
   * exhaust the residual domain exactly. */
  if (root == nullptr || root->player > PG_ODD ||
      root->priority_bound % 2 != (uint64_t)root->player ||
      root->top_attractor.bit_count != game->vertex_count) {
    verify_error(error, "invalid decomposition node metadata");
    return false;
  }
  if (!priorities_at_most(game, domain, root->priority_bound)) {
    verify_error(error, "a domain priority exceeds the node bound");
    return false;
  }

  PGSet target = {0};
  PGSet expected = {0};
  PGSet residual = {0};
  if (!pg_set_init(&target, game->vertex_count) ||
      !pg_set_clone(&residual, domain)) {
    verify_error(error, "failed to allocate verifier sets");
    goto failure;
  }
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (game->vertices[vertex].priority == root->priority_bound) {
      pg_set_add(&target, vertex);
    }
  }
  if (!pg_attractor(game, domain, &target, root->player, &expected) ||
      !pg_set_equal(&expected, &root->top_attractor)) {
    verify_error(error, "the stored top attractor is incorrect");
    goto failure;
  }
  pg_set_subtract_into(&residual, &root->top_attractor);

  if (root->priority_bound < 2 && root->child_count != 0) {
    verify_error(error, "a bound below two cannot have child traps");
    goto failure;
  }
  for (size_t index = 0; index < root->child_count; index++) {
    ADChild const *child = root->children + index;
    if (pg_set_empty(&child->trap) || !pg_set_subset(&child->trap, &residual) ||
        !trap_conditions(game, &residual, &child->trap, root->player)) {
      verify_error(error, "a child is not a nonempty opponent trap");
      goto failure;
    }
    uint64_t const child_bound = root->priority_bound - 2;
    if (!priorities_at_most(game, &child->trap, child_bound) ||
        child->subtree == nullptr || child->subtree->player != root->player ||
        child->subtree->priority_bound != child_bound ||
        !verify_node(game, &child->trap, child->subtree, error)) {
      if (error == nullptr || error->message[0] == '\0') {
        verify_error(error, "invalid recursive child decomposition");
      }
      goto failure;
    }
    PGSet child_attractor = {0};
    if (!pg_attractor(game, &residual, &child->trap, root->player,
                      &child_attractor) ||
        !pg_set_equal(&child_attractor, &child->attractor)) {
      pg_set_destroy(&child_attractor);
      verify_error(error, "a stored child attractor is incorrect");
      goto failure;
    }
    pg_set_subtract_into(&residual, &child_attractor);
    pg_set_destroy(&child_attractor);
  }
  if (!pg_set_empty(&residual)) {
    verify_error(error, "child attractors do not exhaust the residual domain");
    goto failure;
  }

  pg_set_destroy(&target);
  pg_set_destroy(&expected);
  pg_set_destroy(&residual);
  return true;

failure:
  pg_set_destroy(&target);
  pg_set_destroy(&expected);
  pg_set_destroy(&residual);
  return false;
}

bool ad_tree_verify(PGGame const *game, PGSet const *domain, ADNode const *root,
                    ADVerifyError *error) {
  if (error != nullptr) {
    *error = (ADVerifyError){0};
  }
  if (game == nullptr || domain == nullptr || root == nullptr ||
      domain->bit_count != game->vertex_count || pg_set_empty(domain) ||
      !pg_subgame_is_total(game, domain)) {
    verify_error(error, "invalid verifier input domain");
    return false;
  }
  return verify_node(game, domain, root, error);
}

bool zielonka_result_verify(PGGame const *game, PGSet const *domain,
                            ZielonkaResult const *result,
                            ADVerifyError *error) {
  if (error != nullptr) {
    *error = (ADVerifyError){0};
  }
  if (game == nullptr || domain == nullptr || result == nullptr ||
      domain->bit_count != game->vertex_count ||
      result->winning[0].bit_count != game->vertex_count ||
      result->winning[1].bit_count != game->vertex_count) {
    verify_error(error, "invalid result verifier input");
    return false;
  }
  PGSet intersection = {0};
  PGSet combined = {0};
  if (!pg_set_clone(&intersection, &result->winning[0]) ||
      !pg_set_clone(&combined, &result->winning[0])) {
    verify_error(error, "failed to allocate result verifier sets");
    goto failure;
  }
  pg_set_intersect_into(&intersection, &result->winning[1]);
  pg_set_union_into(&combined, &result->winning[1]);
  if (!pg_set_empty(&intersection) || !pg_set_equal(&combined, domain)) {
    verify_error(error, "winning regions do not partition the input domain");
    goto failure;
  }
  for (size_t player = 0; player < 2; player++) {
    bool const empty = pg_set_empty(&result->winning[player]);
    if ((result->decomposition[player] == nullptr) != empty ||
        (!empty && (result->decomposition[player]->player != player ||
                    !ad_tree_verify(game, &result->winning[player],
                                    result->decomposition[player], error)))) {
      if (error == nullptr || error->message[0] == '\0') {
        verify_error(error, "winning region/decomposition mismatch");
      }
      goto failure;
    }
  }
  pg_set_destroy(&intersection);
  pg_set_destroy(&combined);
  return true;

failure:
  pg_set_destroy(&intersection);
  pg_set_destroy(&combined);
  return false;
}
