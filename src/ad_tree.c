#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_tree.h"
#include "pg_attractor.h"
#include "utils.h"

void ad_verify_error(ADVerifyError *error, ADVerifyCondition const condition,
                     size_t const vertex, char const *message) {
  if (error != nullptr && error->condition == AD_VERIFY_OK) {
    error->condition = condition;
    error->vertex = vertex;
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

size_t ad_verify_dead_end(PGGame const *game, PGSet const *domain) {
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    bool found = false;
    for (size_t edge = game->succ_offsets[vertex];
         edge < game->succ_offsets[vertex + 1]; edge++) {
      found |= pg_set_contains(domain, game->successors[edge]);
    }
    if (!found) {
      return vertex;
    }
  }
  return SIZE_MAX;
}

static void child_error(ADVerifyError *error, size_t const index) {
  if (error != nullptr && error->path_length < AD_PATH_CAPACITY) {
    memmove(error->path + 1, error->path,
            error->path_length * sizeof(error->path[0]));
    error->path[0] = index;
    error->path_length++;
  }
}

static size_t difference(PGSet const *left, PGSet const *right) {
  if (left->bit_count != right->bit_count) {
    return SIZE_MAX;
  }
  for (size_t vertex = 0; vertex < left->bit_count; vertex++) {
    if (pg_set_contains(left, vertex) != pg_set_contains(right, vertex)) {
      return vertex;
    }
  }
  return SIZE_MAX;
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

void ad_result_destroy(ADResult *result) {
  if (result == nullptr) {
    return;
  }
  for (size_t player = 0; player < 2; player++) {
    pg_set_destroy(&result->region[player]);
    ad_node_destroy(result->decomposition[player]);
  }
  pg_set_destroy(&result->unresolved);
  for (size_t round = 0; round < result->shave_count; round++) {
    ADShaveRound *shave = result->shaves + round;
    ad_result_destroy(&shave->certified);
    pg_set_destroy(&shave->attracted[0]);
    pg_set_destroy(&shave->attracted[1]);
  }
  free(result->shaves);
  *result = (ADResult){0};
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

[[nodiscard]] static size_t priority_violation(PGGame const *game,
                                               PGSet const *domain,
                                               uint64_t const bound) {
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (game->vertices[vertex].priority > bound) {
      return vertex;
    }
  }
  return SIZE_MAX;
}

[[nodiscard]] static bool priorities_at_most(PGGame const *game,
                                             PGSet const *domain,
                                             uint64_t const bound) {
  return priority_violation(game, domain, bound) == SIZE_MAX;
}

[[nodiscard]] static size_t trap_violation(PGGame const *game,
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
      return vertex;
    }
  }
  return SIZE_MAX;
}

[[nodiscard]] static size_t trap_witness(PGGame const *game,
                                         PGSet const *residual,
                                         PGSet const *trap,
                                         PGPlayer const player) {
  if (trap->bit_count != game->vertex_count) {
    return SIZE_MAX;
  }
  for (size_t vertex = pg_set_next(trap, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(trap, vertex + 1)) {
    if (!pg_set_contains(residual, vertex)) {
      return vertex;
    }
  }
  return trap_violation(game, residual, trap, player);
}

[[nodiscard]] static bool trap_conditions(PGGame const *game,
                                          PGSet const *residual,
                                          PGSet const *trap,
                                          PGPlayer const player) {
  return trap_violation(game, residual, trap, player) == SIZE_MAX;
}

bool ad_verify_collect(ADVerifyErrors *errors, ADVerifyError *error,
                       size_t from, size_t to) {
  if (errors == nullptr || error->condition == AD_VERIFY_MEMORY) {
    return false;
  }
  size_t count = errors->count;
  if (count >= SIZE_MAX / sizeof(*errors->items) - 1) {
    return false;
  }
  ADVerifyError *items = realloc(errors->items, (count + 1) * sizeof(*items));
  if (items == nullptr) {
    return false;
  }
  errors->items = items;
  size_t(*edges)[2] = realloc(errors->edges, (count + 1) * sizeof(*edges));
  if (edges == nullptr) {
    return false;
  }
  errors->edges = edges;
  error->path_length = errors->path_length;
  memcpy(error->path, errors->path, errors->path_length * sizeof(*error->path));
  items[count] = *error;
  edges[count][0] = from;
  edges[count][1] = to;
  errors->count++;
  *error = (ADVerifyError){.vertex = SIZE_MAX};
  return true;
}

static size_t witness_edge(PGGame const *game, PGSet const *domain,
                           PGSet const *set, size_t vertex, bool inside) {
  if (vertex != SIZE_MAX && pg_set_contains(domain, vertex)) {
    for (size_t e = game->succ_offsets[vertex];
         e < game->succ_offsets[vertex + 1]; e++) {
      size_t v = game->successors[e];
      if (pg_set_contains(domain, v) && pg_set_contains(set, v) == inside) {
        return v;
      }
    }
  }
  return SIZE_MAX;
}

static size_t attractor_witness(PGGame const *game, PGSet const *domain,
                                PGSet const *target, PGSet const *expected,
                                PGSet const *stored, size_t vertex,
                                PGPlayer player) {
  if (vertex == SIZE_MAX || pg_set_contains(target, vertex)) {
    return SIZE_MAX;
  }
  if (pg_set_contains(expected, vertex) && !pg_set_contains(stored, vertex) &&
      game->vertices[vertex].owner == player) {
    return witness_edge(game, domain, target, vertex, true);
  }
  if (pg_set_contains(stored, vertex) && !pg_set_contains(expected, vertex) &&
      game->vertices[vertex].owner != player) {
    return witness_edge(game, domain, expected, vertex, false);
  }
  return SIZE_MAX;
}

[[nodiscard]] static bool verify_node(PGGame const *game, PGSet const *domain,
                                      ADNode const *root, ADVerifyError *error,
                                      ADVerifyErrors *errors) {
  /* A valid alpha/d node stores Attr_alpha^W(priority d) as its top
   * attractor. In the remaining domain, every child is a nonempty opponent
   * trap with priorities at most d-2 and a verified alpha/(d-2) subtree. Its
   * stored alpha-attractor is removed, in order, and the child attractors must
   * exhaust the residual domain exactly. */
  if (root == nullptr || root->player > PG_ODD ||
      root->priority_bound % 2 != (uint64_t)root->player ||
      root->top_attractor.bit_count != game->vertex_count) {
    ad_verify_error(error, AD_VERIFY_NODE, SIZE_MAX,
                    "invalid decomposition node metadata");
    return ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX);
  }
  if (!priorities_at_most(game, domain, root->priority_bound)) {
    ad_verify_error(error, AD_VERIFY_PRIORITY,
                    priority_violation(game, domain, root->priority_bound),
                    "a domain priority exceeds the node bound");
    if (!ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX)) {
      return false;
    }
  }

  PGSet target = {0};
  PGSet expected = {0};
  PGSet residual = {0};
  if (!pg_set_init(&target, game->vertex_count) ||
      !pg_set_clone(&residual, domain)) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate verifier sets");
    goto failure;
  }
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (game->vertices[vertex].priority == root->priority_bound) {
      pg_set_add(&target, vertex);
    }
  }
  if (!pg_attractor(game, domain, &target, root->player, &expected)) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate the top attractor");
    goto failure;
  }
  if (!pg_set_equal(&expected, &root->top_attractor)) {
    ad_verify_error(error, AD_VERIFY_TOP,
                    difference(&expected, &root->top_attractor),
                    "the stored top attractor is incorrect");
    size_t vertex = error == nullptr ? SIZE_MAX : error->vertex;
    size_t to = attractor_witness(game, domain, &target, &expected,
                                  &root->top_attractor, vertex, root->player);
    if (!ad_verify_collect(errors, error, to == SIZE_MAX ? SIZE_MAX : vertex,
                           to))
      goto failure;
  }
  pg_set_subtract_into(&residual, &root->top_attractor);

  if (root->priority_bound < 2 && root->child_count != 0) {
    ad_verify_error(error, AD_VERIFY_CHILD_BOUND, SIZE_MAX,
                    "a bound below two cannot have child traps");
    if (!ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX)) {
      goto failure;
    }
    goto done;
  }
  for (size_t index = 0; index < root->child_count; index++) {
    if (errors != nullptr) {
      errors->path[errors->path_length++] = index;
    }
    ADChild const *child = root->children + index;
    if (pg_set_empty(&child->trap) || !pg_set_subset(&child->trap, &residual) ||
        !trap_conditions(game, &residual, &child->trap, root->player)) {
      ad_verify_error(error, AD_VERIFY_TRAP,
                      trap_witness(game, &residual, &child->trap, root->player),
                      "a child is not a nonempty opponent trap");
      if (errors == nullptr) {
        child_error(error, index);
      }
      size_t vertex = error == nullptr ? SIZE_MAX : error->vertex;
      size_t to = witness_edge(game, &residual, &child->trap, vertex, false);
      if (!ad_verify_collect(errors, error, to == SIZE_MAX ? SIZE_MAX : vertex,
                             to))
        goto failure;
    }
    uint64_t const child_bound = root->priority_bound - 2;
    if (!priorities_at_most(game, &child->trap, child_bound)) {
      ad_verify_error(error, AD_VERIFY_PRIORITY,
                      priority_violation(game, &child->trap, child_bound),
                      "a child priority exceeds its bound");
      if (errors == nullptr) {
        child_error(error, index);
      }
      if (!ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX)) {
        goto failure;
      }
    }
    if (child->subtree == nullptr || child->subtree->player != root->player ||
        child->subtree->priority_bound != child_bound) {
      ad_verify_error(error, AD_VERIFY_CHILD, SIZE_MAX,
                      "invalid recursive child decomposition");
      if (errors == nullptr) {
        child_error(error, index);
      }
      if (!ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX)) {
        goto failure;
      }
    } else if ((errors == nullptr || pg_subgame_is_total(game, &child->trap)) &&
               !verify_node(game, &child->trap, child->subtree, error,
                            errors)) {
      if (errors == nullptr) {
        child_error(error, index);
      }
      goto failure;
    }
    if (errors != nullptr && (!pg_subgame_is_total(game, &residual) ||
                              !pg_set_subset(&child->trap, &residual))) {
      errors->path_length--;
      continue;
    }
    PGSet child_attractor = {0};
    if (!pg_attractor(game, &residual, &child->trap, root->player,
                      &child_attractor)) {
      ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                      "failed to allocate the child attractor");
      child_error(error, index);
      goto failure;
    }
    if (!pg_set_equal(&child_attractor, &child->attractor)) {
      ad_verify_error(error, AD_VERIFY_ATTRACTOR,
                      difference(&child_attractor, &child->attractor),
                      "a stored child attractor is incorrect");
      size_t vertex = error == nullptr ? SIZE_MAX : error->vertex;
      size_t to =
          attractor_witness(game, &residual, &child->trap, &child_attractor,
                            &child->attractor, vertex, root->player);
      if (errors == nullptr) {
        child_error(error, index);
      }
      if (!ad_verify_collect(errors, error, to == SIZE_MAX ? SIZE_MAX : vertex,
                             to)) {
        pg_set_destroy(&child_attractor);
        goto failure;
      }
    }
    pg_set_subtract_into(&residual, &child_attractor);
    pg_set_destroy(&child_attractor);
    if (errors != nullptr) {
      errors->path_length--;
    }
  }
  if (!pg_set_empty(&residual)) {
    ad_verify_error(error, AD_VERIFY_PARTITION, pg_set_next(&residual, 0),
                    "child attractors do not cover the subgame Gᵢ");
    if (!ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX)) {
      goto failure;
    }
  }

done:
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
    *error = (ADVerifyError){.vertex = SIZE_MAX};
  }
  if (game == nullptr || domain == nullptr || root == nullptr ||
      domain->bit_count != game->vertex_count || pg_set_empty(domain)) {
    ad_verify_error(error, AD_VERIFY_INPUT, SIZE_MAX,
                    "invalid verifier input domain");
    return false;
  }
  if (!pg_subgame_is_total(game, domain)) {
    ad_verify_error(error, AD_VERIFY_TOTALITY, ad_verify_dead_end(game, domain),
                    "verifier domain is not total");
    return false;
  }
  return verify_node(game, domain, root, error, nullptr);
}

void ad_tree_relative_parts_destroy(ADTreeRelativeParts *parts) {
  if (parts == nullptr) {
    return;
  }
  pg_set_destroy(&parts->highest);
  pg_set_destroy(&parts->top);
  pg_set_destroy(&parts->side);
  *parts = (ADTreeRelativeParts){0};
}

bool ad_tree_relative_parts(PGGame const *game, PGSet const *outer,
                            PGSet const *core, ADNode const *node,
                            ADTreeRelativeParts *parts) {
  if (parts != nullptr) {
    *parts = (ADTreeRelativeParts){0};
  }
  if (game == nullptr || outer == nullptr || core == nullptr ||
      node == nullptr || parts == nullptr ||
      outer->bit_count != game->vertex_count ||
      core->bit_count != game->vertex_count ||
      node->top_attractor.bit_count != game->vertex_count ||
      !pg_set_subset(core, outer) ||
      !pg_set_init(&parts->highest, game->vertex_count) ||
      !pg_set_clone(&parts->top, &node->top_attractor) ||
      !pg_set_clone(&parts->side, outer)) {
    ad_tree_relative_parts_destroy(parts);
    return false;
  }

  for (size_t vertex = pg_set_next(core, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(core, vertex + 1)) {
    if (game->vertices[vertex].priority == node->priority_bound) {
      pg_set_add(&parts->highest, vertex);
    }
  }
  pg_set_subtract_into(&parts->top, &parts->highest);
  pg_set_subtract_into(&parts->side, core);
  return true;
}

[[nodiscard]] static bool verify_tree_relative_node(PGGame const *game,
                                                    PGSet const *outer,
                                                    PGSet const *core,
                                                    ADNode const *root,
                                                    ADVerifyError *error) {
  if (root == nullptr || root->player > PG_ODD ||
      root->priority_bound % 2 != (uint64_t)root->player ||
      root->top_attractor.bit_count != game->vertex_count ||
      outer->bit_count != game->vertex_count ||
      core->bit_count != game->vertex_count || pg_set_empty(core)) {
    ad_verify_error(error, AD_VERIFY_NODE, SIZE_MAX,
                    "invalid tree-relative node domain");
    return false;
  }
  if (!pg_set_subset(core, outer)) {
    ad_verify_error(error, AD_VERIFY_TRAP,
                    trap_witness(game, outer, core, root->player),
                    "a tree-relative core leaves its outer domain");
    return false;
  }
  if (!pg_subgame_is_total(game, outer)) {
    ad_verify_error(error, AD_VERIFY_TOTALITY, ad_verify_dead_end(game, outer),
                    "a tree-relative outer domain is not total");
    return false;
  }
  if (!trap_conditions(game, outer, core, root->player)) {
    ad_verify_error(error, AD_VERIFY_TRAP,
                    trap_witness(game, outer, core, root->player),
                    "a tree-relative core is not an opponent trap");
    return false;
  }
  if (!priorities_at_most(game, core, root->priority_bound)) {
    ad_verify_error(error, AD_VERIFY_PRIORITY,
                    priority_violation(game, core, root->priority_bound),
                    "a tree-relative core priority exceeds the node bound");
    return false;
  }

  ADTreeRelativeParts parts = {0};
  PGSet expected = {0};
  PGSet residual = {0};
  if (!ad_tree_relative_parts(game, outer, core, root, &parts) ||
      !pg_attractor(game, core, &parts.highest, root->player, &expected)) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate tree-relative top sets");
    goto failure;
  }
  if (!pg_set_equal(&expected, &root->top_attractor)) {
    ad_verify_error(error, AD_VERIFY_TOP,
                    difference(&expected, &root->top_attractor),
                    "the tree-relative top attractor is incorrect");
    goto failure;
  }
  pg_set_destroy(&expected);

  if (!pg_attractor(game, outer, core, root->player, &expected)) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate the core attractor");
    goto failure;
  }
  if (!pg_set_equal(&expected, outer)) {
    ad_verify_error(error, AD_VERIFY_CORE, difference(&expected, outer),
                    "the tree-relative core does not attract its outer domain");
    goto failure;
  }
  pg_set_destroy(&expected);

  if (!pg_set_clone(&residual, outer)) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate tree-relative verifier sets");
    goto failure;
  }
  pg_set_subtract_into(&residual, &parts.highest);
  pg_set_subtract_into(&residual, &parts.top);

  if (root->priority_bound < 2 && root->child_count != 0) {
    ad_verify_error(error, AD_VERIFY_CHILD_BOUND, SIZE_MAX,
                    "a tree-relative bound below two cannot have children");
    goto failure;
  }
  for (size_t index = 0; index < root->child_count; index++) {
    ADChild const *child = root->children + index;
    uint64_t const child_bound = root->priority_bound - 2;
    if (pg_set_empty(&child->attractor) ||
        !pg_set_subset(&child->attractor, &residual) ||
        !trap_conditions(game, &residual, &child->attractor, root->player)) {
      ad_verify_error(
          error, AD_VERIFY_TRAP,
          trap_witness(game, &residual, &child->attractor, root->player),
          "a tree-relative child region is not an opponent trap");
      child_error(error, index);
      goto failure;
    }
    if (pg_set_empty(&child->trap) ||
        !pg_set_subset(&child->trap, &child->attractor)) {
      ad_verify_error(
          error, AD_VERIFY_TRAP,
          trap_witness(game, &child->attractor, &child->trap, root->player),
          "a tree-relative child core leaves its region or is empty");
      child_error(error, index);
      goto failure;
    }
    if (child->subtree == nullptr || child->subtree->player != root->player ||
        child->subtree->priority_bound != child_bound ||
        !verify_tree_relative_node(game, &child->attractor, &child->trap,
                                   child->subtree, error)) {
      if (error == nullptr || error->message[0] == '\0') {
        ad_verify_error(error, AD_VERIFY_CHILD, SIZE_MAX,
                        "invalid recursive tree-relative child");
      }
      child_error(error, index);
      goto failure;
    }
    pg_set_subtract_into(&residual, &child->attractor);
  }
  if (!pg_set_equal(&residual, &parts.side)) {
    ad_verify_error(error, AD_VERIFY_PARTITION,
                    difference(&residual, &parts.side),
                    "tree-relative regions do not partition the outer domain");
    goto failure;
  }

  ad_tree_relative_parts_destroy(&parts);
  pg_set_destroy(&expected);
  pg_set_destroy(&residual);
  return true;

failure:
  ad_tree_relative_parts_destroy(&parts);
  pg_set_destroy(&expected);
  pg_set_destroy(&residual);
  return false;
}

bool ad_tree_relative_verify(PGGame const *game, PGSet const *domain,
                             ADNode const *root, ADVerifyError *error) {
  if (error != nullptr) {
    *error = (ADVerifyError){.vertex = SIZE_MAX};
  }
  if (game == nullptr || domain == nullptr || root == nullptr ||
      domain->bit_count != game->vertex_count || pg_set_empty(domain)) {
    ad_verify_error(error, AD_VERIFY_INPUT, SIZE_MAX,
                    "invalid tree-relative verifier input domain");
    return false;
  }
  if (!pg_subgame_is_total(game, domain)) {
    ad_verify_error(error, AD_VERIFY_TOTALITY, ad_verify_dead_end(game, domain),
                    "tree-relative verifier domain is not total");
    return false;
  }
  return verify_tree_relative_node(game, domain, domain, root, error);
}

bool ad_result_verify_partial(PGGame const *game, PGSet const *domain,
                              ADResult const *result, ADVerifyError *error) {
  if (error != nullptr) {
    *error = (ADVerifyError){.vertex = SIZE_MAX};
  }
  if (game == nullptr || domain == nullptr || result == nullptr ||
      result->kind > AD_RESULT_PARTIAL ||
      (result->shave_count != 0 && result->shaves == nullptr) ||
      domain->bit_count != game->vertex_count ||
      result->region[0].bit_count != game->vertex_count ||
      result->region[1].bit_count != game->vertex_count ||
      result->unresolved.bit_count != game->vertex_count) {
    ad_verify_error(error, AD_VERIFY_INPUT, SIZE_MAX,
                    "invalid result verifier input");
    return false;
  }
  PGSet intersection = {0};
  PGSet rest = {0};
  PGSet residual = {0};
  if (!pg_set_clone(&intersection, &result->region[0]) ||
      !pg_set_clone(&rest, domain)) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate result verifier sets");
    goto failure;
  }
  for (size_t round = 0; round < result->shave_count; round++) {
    ADShaveRound const *shave = result->shaves + round;
    if (!ad_result_verify_partial(game, &rest, &shave->certified, error)) {
      goto failure;
    }
    for (size_t player = 0; player < 2; player++) {
      ADNode const *tree = shave->certified.decomposition[player];
      PGSet const *region = &shave->certified.region[player];
      if (tree != nullptr &&
          !ad_tree_relative_verify(game, region, tree, error)) {
        goto failure;
      }
      if (!pg_attractor(game, &rest, region, (PGPlayer)player, &intersection)) {
        ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                        "failed to allocate shaving attractor");
        goto failure;
      }
      if (!pg_set_equal(&intersection, &shave->attracted[player])) {
        ad_verify_error(error, AD_VERIFY_SHAVE,
                        difference(&intersection, &shave->attracted[player]),
                        "invalid iterative shaving certificate or attractor");
        goto failure;
      }
    }
    pg_set_subtract_into(&rest, &shave->attracted[0]);
    pg_set_subtract_into(&rest, &shave->attracted[1]);
    if (!pg_subgame_is_total(game, &rest)) {
      ad_verify_error(error, AD_VERIFY_TOTALITY,
                      ad_verify_dead_end(game, &rest),
                      "iterative shaving left a non-total subgame");
      goto failure;
    }
  }
  if (result->shave_count != 0) {
    pg_set_move(&residual, &rest);
    domain = &residual;
    if (!pg_set_clone(&rest, domain)) {
      ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                      "failed to allocate result verifier sets");
      goto failure;
    }
    pg_set_clear(&intersection);
    pg_set_union_into(&intersection, &result->region[0]);
  }
  pg_set_intersect_into(&intersection, &result->region[1]);
  pg_set_subtract_into(&rest, &result->region[0]);
  pg_set_subtract_into(&rest, &result->region[1]);
  if (!pg_set_empty(&intersection) ||
      !pg_set_subset(&result->region[0], domain) ||
      !pg_set_subset(&result->region[1], domain) ||
      !pg_set_equal(&rest, &result->unresolved)) {
    size_t vertex = pg_set_next(&intersection, 0);
    for (size_t player = 0; vertex == SIZE_MAX && player < 2; player++) {
      if (!pg_set_subset(&result->region[player], domain)) {
        vertex = trap_witness(game, domain, &result->region[player],
                              (PGPlayer)player);
      }
    }
    if (vertex == SIZE_MAX) {
      vertex = difference(&rest, &result->unresolved);
    }
    ad_verify_error(error, AD_VERIFY_PARTITION, vertex,
                    "regions and unresolved vertices do not partition the "
                    "input domain");
    goto failure;
  }
  for (size_t player = 0; player < 2; player++) {
    PGSet const *region = &result->region[player];
    bool const empty = pg_set_empty(region);
    if ((result->decomposition[player] == nullptr) != empty) {
      ad_verify_error(error, AD_VERIFY_REGION, pg_set_next(region, 0),
                      "region/decomposition mismatch");
      goto failure;
    }
    if (empty) {
      continue;
    }
    if (result->decomposition[player]->player != player) {
      ad_verify_error(error, AD_VERIFY_NODE, SIZE_MAX,
                      "a region has the other player's decomposition");
      goto failure;
    }
    /* A decomposition of the induced subgame certifies a dominion only if the
     * opponent cannot leave the region. */
    if (!trap_conditions(game, domain, region, (PGPlayer)player)) {
      ad_verify_error(error, AD_VERIFY_TRAP,
                      trap_violation(game, domain, region, (PGPlayer)player),
                      "a region is not an opponent trap");
      goto failure;
    }
    if (!ad_tree_verify(game, region, result->decomposition[player], error)) {
      goto failure;
    }
  }
  pg_set_destroy(&intersection);
  pg_set_destroy(&rest);
  pg_set_destroy(&residual);
  return true;

failure:
  pg_set_destroy(&intersection);
  pg_set_destroy(&rest);
  pg_set_destroy(&residual);
  return false;
}

bool ad_result_verify_complete(PGGame const *game, PGSet const *domain,
                               ADResult const *result, ADVerifyError *error) {
  if (!ad_result_verify_partial(game, domain, result, error)) {
    return false;
  }
  if (result->kind != AD_RESULT_COMPLETE ||
      !pg_set_empty(&result->unresolved)) {
    ad_verify_error(error, AD_VERIFY_INCOMPLETE,
                    pg_set_next(&result->unresolved, 0),
                    "winning regions do not partition the input domain");
    return false;
  }
  return true;
}

[[nodiscard]] static bool intersects(PGSet const *left, PGSet const *right) {
  for (size_t word = 0; word < left->word_count; word++) {
    if ((left->words[word] & right->words[word]) != 0) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] static bool child_in_subgame(ADChild const *child,
                                           PGSet const *subgame) {
  return intersects(&child->trap, subgame) ||
         intersects(&child->attractor, subgame);
}

/* The strict verifier compares stored sets exactly. Restrict once, then reuse
 * its conditions, keeping empty-core/nonempty-side children as actual errors.
 */
[[nodiscard]] static ADNode *restrict_node(PGSet const *subgame,
                                           ADNode const *root,
                                           ADVerifyError *error, bool collect) {
  if (root == nullptr || root->player > PG_ODD ||
      (!collect && root->priority_bound % 2 != (uint64_t)root->player) ||
      root->top_attractor.bit_count != subgame->bit_count ||
      (root->child_count != 0 && root->children == nullptr)) {
    ad_verify_error(error, AD_VERIFY_NODE, SIZE_MAX,
                    "invalid decomposition node metadata");
    return nullptr;
  }
  ADNode *copy =
      ad_node_create(root->player, root->priority_bound, subgame->bit_count);
  if (copy == nullptr) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate restricted node");
    return nullptr;
  }
  pg_set_union_into(&copy->top_attractor, &root->top_attractor);
  pg_set_intersect_into(&copy->top_attractor, subgame);
  for (size_t index = 0; index < root->child_count; index++) {
    ADChild const *source = root->children + index;
    if (source->trap.bit_count != subgame->bit_count ||
        source->attractor.bit_count != subgame->bit_count) {
      ad_verify_error(error, AD_VERIFY_NODE, SIZE_MAX,
                      "invalid child set dimensions");
      child_error(error, index);
      goto failure;
    }
    if (!child_in_subgame(source, subgame)) {
      continue;
    }
    if (!collect && root->priority_bound < 2) {
      ad_verify_error(error, AD_VERIFY_CHILD_BOUND, SIZE_MAX,
                      "a bound below two cannot have child traps");
      goto failure;
    }
    if (!collect &&
        (source->subtree == nullptr ||
         source->subtree->player != root->player ||
         source->subtree->priority_bound != root->priority_bound - 2)) {
      ad_verify_error(error, AD_VERIFY_CHILD, SIZE_MAX,
                      "invalid recursive child decomposition");
      child_error(error, index);
      goto failure;
    }
    ADChild child = {0};
    if (!pg_set_clone(&child.trap, &source->trap) ||
        !pg_set_clone(&child.attractor, &source->attractor)) {
      ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                      "failed to allocate restricted child sets");
      goto child_failure;
    }
    pg_set_intersect_into(&child.trap, subgame);
    pg_set_intersect_into(&child.attractor, subgame);
    child.subtree = restrict_node(subgame, source->subtree, error, collect);
    if (child.subtree == nullptr) {
      goto child_failure;
    }
    if (!ad_node_append_child(copy, &child)) {
      ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                      "failed to append restricted child");
      goto child_failure;
    }
    continue;
  child_failure:
    pg_set_destroy(&child.trap);
    pg_set_destroy(&child.attractor);
    ad_node_destroy(child.subtree);
    child_error(error, index);
    goto failure;
  }
  return copy;
failure:
  ad_node_destroy(copy);
  return nullptr;
}

static void restore_check_path(PGSet const *subgame, ADNode const *original,
                               ADVerifyError *error) {
  if (error == nullptr) {
    return;
  }
  for (size_t level = 0; level < error->path_length; level++) {
    size_t const selected = error->path[level];
    size_t index = 0;
    size_t kept = 0;
    while (index < original->child_count) {
      if (child_in_subgame(original->children + index, subgame)) {
        if (kept == selected) {
          break;
        }
        kept++;
      }
      index++;
    }
    error->path[level] = index;
    original = original->children[index].subtree;
  }
}

bool ad_tree_check(PGGame const *game, PGSet const *subgame,
                   PGPlayer const player, ADNode const *root,
                   ADVerifyError *error) {
  if (error != nullptr) {
    *error = (ADVerifyError){.vertex = SIZE_MAX};
  }
  if (game == nullptr || subgame == nullptr || player > PG_ODD ||
      subgame->bit_count != game->vertex_count) {
    ad_verify_error(error, AD_VERIFY_INPUT, SIZE_MAX,
                    "invalid decomposition check input");
    return false;
  }
  if (pg_set_empty(subgame)) {
    return true;
  }
  if (root == nullptr) {
    ad_verify_error(error, AD_VERIFY_INPUT, SIZE_MAX,
                    "nonempty subgame has no decomposition");
    return false;
  }
  if (root->player != player) {
    ad_verify_error(error, AD_VERIFY_NODE, SIZE_MAX,
                    "decomposition has the wrong player");
    return false;
  }
  if (!pg_subgame_is_total(game, subgame)) {
    ad_verify_error(error, AD_VERIFY_TOTALITY,
                    ad_verify_dead_end(game, subgame),
                    "verifier domain is not total");
    return false;
  }
  ADNode *restricted = restrict_node(subgame, root, error, false);
  if (restricted == nullptr) {
    return false;
  }
  bool const valid = ad_tree_verify(game, subgame, restricted, error);
  if (!valid) {
    restore_check_path(subgame, root, error);
  }
  ad_node_destroy(restricted);
  return valid;
}

bool ad_tree_check_all(PGGame const *game, PGSet const *subgame,
                       PGPlayer player, ADNode const *root,
                       ADVerifyErrors *errors) {
  ADVerifyError error = {.vertex = SIZE_MAX};
  if (pg_set_empty(subgame)) {
    return true;
  }
  if (root == nullptr || root->player != player ||
      !pg_subgame_is_total(game, subgame)) {
    bool const valid = ad_tree_check(game, subgame, player, root, &error);
    return valid || ad_verify_collect(errors, &error, SIZE_MAX, SIZE_MAX);
  }
  ADNode *restricted = restrict_node(subgame, root, &error, true);
  if (restricted == nullptr) {
    if (error.condition == AD_VERIFY_MEMORY) {
      return false;
    }
    return ad_verify_collect(errors, &error, SIZE_MAX, SIZE_MAX);
  }
  size_t start = errors->count;
  bool ok = verify_node(game, subgame, restricted, &error, errors);
  for (size_t i = start; i < errors->count; i++) {
    restore_check_path(subgame, root, errors->items + i);
  }
  ad_node_destroy(restricted);
  return ok;
}
