#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_reach.h"
#include "ad_sym.h"
#include "pg_attractor.h"
#include "utils.h"

/* A position is a node and H/T/S (regular/minus/plus), even in the implicit
 * n-ary hosts. Node indices stay stable when a new child is needed. */
typedef struct {
  size_t node;
  ADPositionKind kind;
} Label;

typedef struct {
  size_t regular, minus, after;
} Ordinal;

typedef struct {
  PGGame const *game;
  ADLiftTree tree[2];
  size_t capacity[2];
  Label *labels[2];
  Ordinal *ordinals[2];
  size_t generation[2];
  bool memory;
  size_t arity;
  PGSet safe;
  PGSet lower;
  PGSet checked;
  ADSymStats *stats;
  ADTrace *trace;
  size_t (*trace_paths)[AD_PATH_CAPACITY];
} Sym;

static void rank_nodes(ADLiftTree const *tree, Ordinal *ordinals,
                       size_t const node, size_t *rank) {
  ordinals[node].regular = (*rank)++;
  ordinals[node].minus = (*rank)++;
  for (size_t child = tree->nodes[node].first_child; child != SIZE_MAX;
       child = tree->nodes[child].next_sibling) {
    rank_nodes(tree, ordinals, child, rank);
    (*rank)++;
  }
  ordinals[node].after = *rank;
}

/* Rebuild on every host growth. In particular, appending a sibling changes
 * ordinals in other subtrees even though structured node IDs stay stable. */
static ADReach *rank_view(Sym *state, PGSet const *domain,
                          PGPlayer const player) {
  if (state->trace != nullptr || ad_reach_scalar_requested() ||
      pg_set_count(domain) < 64 || !pg_subgame_is_total(state->game, domain)) {
    return nullptr;
  }
  ADLiftTree const *tree = state->tree + player;
  if (tree->node_count > SIZE_MAX / sizeof(Ordinal)) {
    return nullptr;
  }
  if (state->generation[player] != tree->node_count) {
    Ordinal *ordinals = malloc(tree->node_count * sizeof(*ordinals));
    if (ordinals == nullptr) {
      return nullptr;
    }
    size_t rank = 0;
    rank_nodes(tree, ordinals, 0, &rank);
    free(state->ordinals[player]);
    state->ordinals[player] = ordinals;
    state->generation[player] = tree->node_count;
  }
  char const *choice = getenv("STRAHLER_AD_BACKEND");
  /* A recursive domain is checked once: its packed adjacency cannot be
   * amortized across lifting snapshots. Keep SIMD selectable for comparison. */
  if (choice == nullptr || strcmp(choice, "auto") == 0) {
    choice = "fused";
  }
  ADReach *work = ad_reach_create_backend(state->game, domain, player, choice);
  if (work == nullptr) {
    return nullptr;
  }
  ADReachLabel *labels = ad_reach_labels(work);
  for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
       v = pg_set_next(domain, v + 1)) {
    Label const label = state->labels[player][v];
    Ordinal const ordinal = state->ordinals[player][label.node];
    labels[v] = (ADReachLabel){
        .rank = label.kind == AD_POS_REGULAR     ? ordinal.regular
                : label.kind == AD_POS_MINUS_INF ? ordinal.minus
                                                 : ordinal.after,
        .after = ordinal.after,
        .kind = label.kind == AD_POS_REGULAR ? AD_REACH_REGULAR
                : label.node == 0 && label.kind == AD_POS_PLUS ? AD_REACH_TOP
                                                               : AD_REACH_LAZY};
  }
  ad_reach_compute(work);
  return work;
}

static void sym_error(ADLiftError *error, char const *message) {
  if (error != nullptr && error->message[0] == '\0') {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

/* S at the root is TOP. For different nodes, compare their paths at the first
 * different sibling; sibling indices increase in creation order. */
[[nodiscard]] static int compare(ADLiftTree const *tree, Label const left,
                                 Label const right) {
  if (left.node == right.node) {
    return left.kind < right.kind ? -1 : left.kind > right.kind ? 1 : 0;
  }
  size_t first = left.node;
  size_t second = right.node;
  while (tree->nodes[first].level < tree->nodes[second].level) {
    first = tree->nodes[first].parent;
  }
  while (tree->nodes[second].level < tree->nodes[first].level) {
    second = tree->nodes[second].parent;
  }
  if (first == second) {
    if (first == left.node) {
      return left.kind == AD_POS_PLUS ? 1 : -1;
    }
    return right.kind == AD_POS_PLUS ? -1 : 1;
  }
  while (tree->nodes[first].parent != tree->nodes[second].parent) {
    first = tree->nodes[first].parent;
    second = tree->nodes[second].parent;
  }
  return first < second ? -1 : 1;
}

[[nodiscard]] static bool in_core(Sym const *state, PGPlayer const player,
                                  size_t const node, size_t const vertex) {
  Label const label = state->labels[player][vertex];
  return compare(state->tree + player, label, (Label){node, AD_POS_REGULAR}) >=
             0 &&
         compare(state->tree + player, label, (Label){node, AD_POS_PLUS}) < 0;
}

[[nodiscard]] static bool attract(Sym *state, PGSet const *domain,
                                  PGSet const *target, PGPlayer const player,
                                  PGSet *result, size_t const node,
                                  char const *kind) {
  state->stats->attractors++;
  bool const success =
      pg_attractor(state->game, domain, target, player, result);
  if (success && state->trace != nullptr) {
    size_t storage[AD_PATH_CAPACITY];
    ADTracePath const path =
        ad_trace_host_path(state->tree + player, node, storage);
    AD_TRACE(state->trace, .kind = AD_TRACE_ATTRACTOR, .player = player,
             .path = path, .name = kind, .target = target, .vertices = result,
             .subgame = domain);
  }
  return success && (state->trace == nullptr || !state->trace->failed);
}

/* Proposition 7.2.1, restricted to this call's subgame. Failure to satisfy a
 * condition is distinct from failure to allocate an attractor. */
[[nodiscard]] static bool is_ad_scalar(Sym *state, PGSet const *domain,
                                       PGPlayer const player, size_t const node,
                                       bool *valid) {
  *valid = false;
  pg_set_clear(&state->checked);
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (!in_core(state, player, node, vertex)) {
      return true;
    }
    if (pg_set_contains(&state->checked, vertex)) {
      continue;
    }
    Label const label = state->labels[player][vertex];
    if (label.kind == AD_POS_REGULAR) {
      bool const owned = state->game->vertices[vertex].owner == player;
      bool allowed = !owned;
      for (size_t edge = state->game->succ_offsets[vertex];
           edge < state->game->succ_offsets[vertex + 1]; edge++) {
        size_t const successor = state->game->successors[edge];
        if (!pg_set_contains(domain, successor)) {
          continue;
        }
        bool const inside =
            compare(state->tree + player, state->labels[player][successor],
                    (Label){label.node, AD_POS_PLUS}) < 0;
        if (inside == owned) {
          allowed = owned;
          break;
        }
      }
      if (!allowed) {
        return true;
      }
      continue;
    }
    pg_set_clear(&state->safe);
    pg_set_clear(&state->lower);
    for (size_t other = pg_set_next(domain, 0); other != SIZE_MAX;
         other = pg_set_next(domain, other + 1)) {
      int const order =
          compare(state->tree + player, state->labels[player][other], label);
      if (order <= 0) {
        pg_set_add(&state->safe, other);
      }
      if (order < 0) {
        pg_set_add(&state->lower, other);
      }
    }
    PGSet attracted = {0};
    state->stats->attractors++;
    if (!pg_attractor_through(state->game, domain, &state->safe, &state->lower,
                              player, &attracted)) {
      return false;
    }
    if (state->trace != nullptr) {
      size_t storage[AD_PATH_CAPACITY];
      ADTracePath const path =
          ad_trace_host_path(state->tree + player, label.node, storage);
      AD_TRACE(state->trace, .kind = AD_TRACE_ATTRACTOR, .player = player,
               .path = path, .name = "side", .target = &state->lower,
               .vertices = &attracted, .subgame = domain, .safe = &state->safe);
      if (state->trace->failed) {
        pg_set_destroy(&attracted);
        return false;
      }
    }
    bool reached = true;
    for (size_t other = vertex; other != SIZE_MAX;
         other = pg_set_next(domain, other + 1)) {
      Label const current = state->labels[player][other];
      if (current.node == label.node && current.kind == label.kind) {
        pg_set_add(&state->checked, other);
        if (!pg_set_contains(&attracted, other)) {
          reached = false;
        }
      }
    }
    pg_set_destroy(&attracted);
    if (!reached) {
      return true;
    }
  }
  *valid = true;
  return true;
}

[[nodiscard]] static bool is_ad_impl(Sym *state, PGSet const *domain,
                                     PGPlayer const player, size_t const node,
                                     bool *valid) {
  ADReach *work = rank_view(state, domain, player);
  if (work == nullptr) {
    return is_ad_scalar(state, domain, player, node, valid);
  }
  unsigned char const *good = ad_reach_good(work);
  *valid = false;
  pg_set_clear(&state->checked);
  /* Replay the original inspection order, including early failure and the
   * number of side-attractor tests. Computing later fibres cannot affect it. */
  for (size_t v = pg_set_next(domain, 0); v != SIZE_MAX;
       v = pg_set_next(domain, v + 1)) {
    if (!in_core(state, player, node, v)) {
      goto done;
    }
    if (pg_set_contains(&state->checked, v)) {
      continue;
    }
    Label const label = state->labels[player][v];
    if (label.kind == AD_POS_REGULAR) {
      if (!good[v]) {
        goto done;
      }
      continue;
    }
    state->stats->attractors++;
    bool reached = true;
    for (size_t other = v; other != SIZE_MAX;
         other = pg_set_next(domain, other + 1)) {
      Label const current = state->labels[player][other];
      if (current.node == label.node && current.kind == label.kind) {
        pg_set_add(&state->checked, other);
        reached &= good[other] != 0;
      }
    }
    if (!reached) {
      goto done;
    }
  }
  *valid = true;
done:
  if (getenv("STRAHLER_BACKEND_REPORT") != nullptr) {
    fprintf(stderr, "AD symmetric recursive rank check: %zu vertices\n",
            pg_set_count(domain));
  }
  ad_reach_destroy(work);
  return true;
}

[[nodiscard]] static bool is_ad(Sym *state, PGSet const *domain,
                                PGPlayer const player, size_t const node,
                                bool *valid) {
  if (!is_ad_impl(state, domain, player, node, valid)) {
    return false;
  }
  if (state->trace != nullptr) {
    size_t storage[AD_PATH_CAPACITY];
    ADTracePath const path =
        ad_trace_host_path(state->tree + player, node, storage);
    AD_TRACE(state->trace, .kind = AD_TRACE_CHECK, .player = player,
             .path = path, .subgame = domain, .valid = *valid,
             .name = *valid ? "valid restricted labelling"
                            : "restricted labelling condition fails");
  }
  return state->trace == nullptr || !state->trace->failed;
}

[[nodiscard]] static size_t add_node(Sym *state, PGPlayer const player,
                                     uint64_t const level,
                                     size_t const parent) {
  ADLiftTree *tree = state->tree + player;
  if (tree->node_count == state->capacity[player]) {
    ArrayGrowth const growth = grow_array(tree->nodes, state->capacity[player],
                                          sizeof(tree->nodes[0]));
    if (!growth.succeeded) {
      return SIZE_MAX;
    }
    tree->nodes = growth.data;
    state->capacity[player] = growth.capacity;
  }
  size_t const index = tree->node_count++;
  tree->nodes[index] = (ADLiftNode){.level = level,
                                    .parent = parent,
                                    .first_child = SIZE_MAX,
                                    .next_sibling = SIZE_MAX};
  if (parent != SIZE_MAX) {
    tree->nodes[parent].child_count++;
  }
  return index;
}

[[nodiscard]] static bool first_child(Sym *state, PGPlayer const player,
                                      size_t const node, size_t *child) {
  ADLiftTree *tree = state->tree + player;
  *child = tree->nodes[node].first_child;
  if (*child == SIZE_MAX && state->memory && tree->nodes[node].level >= 2) {
    *child = add_node(state, player, tree->nodes[node].level - 2, node);
    if (*child == SIZE_MAX) {
      return false;
    }
    tree->nodes[node].first_child = *child;
  }
  return true;
}

[[nodiscard]] static bool next_sibling(Sym *state, PGPlayer const player,
                                       size_t const node, size_t *sibling) {
  ADLiftTree *tree = state->tree + player;
  *sibling = tree->nodes[node].next_sibling;
  size_t const parent = tree->nodes[node].parent;
  if (*sibling == SIZE_MAX && state->memory && parent != SIZE_MAX &&
      tree->nodes[parent].child_count < state->arity) {
    *sibling = add_node(state, player, tree->nodes[node].level, parent);
    if (*sibling == SIZE_MAX) {
      return false;
    }
    tree->nodes[node].next_sibling = *sibling;
  }
  return true;
}

/* Proposition 7.2.3's ⊕: choose H, T or S by priority. The printed unions
 * with H in the definitions of T/S are read as unions with T/S respectively. */
[[nodiscard]] static Label assign(Sym const *state, PGPlayer const player,
                                  size_t const node, size_t const vertex) {
  uint64_t const priority = state->game->vertices[vertex].priority;
  uint64_t const level = state->tree[player].nodes[node].level;
  return (Label){node, priority == level  ? AD_POS_REGULAR
                       : priority < level ? AD_POS_MINUS_INF
                                          : AD_POS_PLUS};
}

/* smallMove on one T or S fibre, limited to this recursive subgame. */
[[nodiscard]] static bool move(Sym *state, PGSet const *domain,
                               PGPlayer const player, size_t const node,
                               ADPositionKind const kind,
                               PGSet const *attracted) {
  state->stats->moves++;
  size_t destination = SIZE_MAX;
  bool located = false;
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    Label *label = state->labels[player] + vertex;
    if (label->node != node || label->kind != kind ||
        pg_set_contains(attracted, vertex)) {
      continue;
    }
    if (!located) {
      bool const found = kind == AD_POS_MINUS_INF
                             ? first_child(state, player, node, &destination)
                             : next_sibling(state, player, node, &destination);
      if (!found) {
        return false;
      }
      located = true;
    }
    if (destination != SIZE_MAX) {
      *label = assign(state, player, destination, vertex);
    } else {
      size_t const side = kind == AD_POS_MINUS_INF
                              ? node
                              : state->tree[player].nodes[node].parent;
      if (side == SIZE_MAX) {
        return false;
      }
      *label = (Label){side, AD_POS_PLUS};
    }
    if (state->trace != nullptr) {
      ad_trace_label(state->trace, state->tree + player, vertex, label->node,
                     label->kind, AD_TRACE_LABELS, 0, 0);
      if (state->trace->failed) {
        return false;
      }
    }
  }
  return true;
}

/* Remark 5: pointwise max with η^S is the minimal inflationary Set. */
static void set_side(Sym *state, PGSet const *vertices, PGPlayer const player,
                     size_t const node) {
  state->stats->sets++;
  Label const side = {node, AD_POS_PLUS};
  for (size_t vertex = pg_set_next(vertices, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(vertices, vertex + 1)) {
    Label *label = state->labels[player] + vertex;
    if (compare(state->tree + player, *label, side) < 0) {
      *label = side;
      if (state->trace != nullptr) {
        ad_trace_label(state->trace, state->tree + player, vertex, label->node,
                       label->kind, AD_TRACE_LABELS, 0, 0);
        if (state->trace->failed) {
          return;
        }
      }
    }
  }
}

[[nodiscard]] static bool solve(Sym *state, PGSet *domain, PGPlayer player,
                                size_t node, size_t opponent_node);

/* Algorithms 8/9, with players exchanged on alternating levels. domain is
 * owned by the caller and becomes the returned separator for this player. */
[[nodiscard]] static bool solve_impl(Sym *state, PGSet *domain,
                                     PGPlayer const player, size_t const node,
                                     size_t const opponent_node) {
  state->stats->calls++;
  PGPlayer const opponent = (PGPlayer)(1 - player);
  assert(pg_subgame_is_total(state->game, domain));
  bool higher_side = false;
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    assert(in_core(state, player, node, vertex));
    Label const label = state->labels[opponent][vertex];
    int const order = compare(state->tree + opponent, label,
                              (Label){opponent_node, AD_POS_PLUS});
    assert(compare(state->tree + opponent, label,
                   (Label){opponent_node, AD_POS_REGULAR}) >= 0 &&
           order <= 0);
    higher_side |= order == 0;
  }
  state->stats->higher_side_calls += higher_side;
  bool valid = false;
  if (!is_ad(state, domain, player, node, &valid)) {
    return false;
  }
  if (valid) {
    set_side(state, domain, opponent, opponent_node);
    return true;
  }
  if (!is_ad(state, domain, opponent, opponent_node, &valid)) {
    return false;
  }
  if (valid) {
    set_side(state, domain, player, node);
    pg_set_clear(domain);
    return true;
  }
  size_t child = SIZE_MAX;
  if (!first_child(state, opponent, opponent_node, &child)) {
    return false;
  }
  PGSet target = {0};
  PGSet top = {0};
  PGSet subgame = {0};
  PGSet side = {0};
  if (!pg_set_init(&target, state->game->vertex_count) ||
      !pg_set_init(&subgame, state->game->vertex_count)) {
    goto failure;
  }
  while (child != SIZE_MAX && !pg_set_empty(domain)) {
    pg_set_clear(&target);
    for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(domain, vertex + 1)) {
      if (state->game->vertices[vertex].priority ==
          state->tree[player].nodes[node].level) {
        pg_set_add(&target, vertex);
      }
    }
    if (!attract(state, domain, &target, player, &top, node, "top") ||
        !move(state, domain, player, node, AD_POS_MINUS_INF, &top)) {
      goto failure;
    }
    pg_set_clear(&target);
    for (size_t vertex = pg_set_next(&top, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(&top, vertex + 1)) {
      if (in_core(state, opponent, child, vertex)) {
        pg_set_add(&target, vertex);
      }
    }
    set_side(state, &target, opponent, child);
    pg_set_clear(&subgame);
    for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(domain, vertex + 1)) {
      if (in_core(state, opponent, child, vertex)) {
        pg_set_add(&subgame, vertex);
      }
    }
    /* Close stale children to total traps; see docs/symmetric.md for the
     * corrected Lemma 8.2.1 entry invariant and the upper-bound argument. */
    pg_set_clear(&target);
    pg_set_union_into(&target, domain);
    pg_set_subtract_into(&target, &subgame);
    PGSet escaping = {0};
    if (!attract(state, domain, &target, player, &escaping, node, "side")) {
      goto failure;
    }
    pg_set_intersect_into(&escaping, &subgame);
    set_side(state, &escaping, opponent, child);
    pg_set_subtract_into(&subgame, &escaping);
    pg_set_destroy(&escaping);
    if ((!pg_set_empty(&subgame) &&
         !solve(state, &subgame, opponent, child, node)) ||
        !attract(state, domain, &subgame, opponent, &side, child, "side") ||
        !move(state, domain, opponent, child, AD_POS_PLUS, &side)) {
      goto failure;
    }
    pg_set_clear(&target);
    for (size_t vertex = pg_set_next(&side, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(&side, vertex + 1)) {
      if (in_core(state, player, node, vertex)) {
        pg_set_add(&target, vertex);
      }
    }
    set_side(state, &target, player, node);
    pg_set_subtract_into(domain, &side);
    /* Alg. 8 line 27 means G_{i+1}, as required by Lemma 8.2.1(c). */
    if (state->memory) {
      if (!is_ad(state, domain, player, node, &valid)) {
        goto failure;
      }
      if (valid) {
        set_side(state, domain, opponent, opponent_node);
        break;
      }
    }
    if (!next_sibling(state, opponent, child, &child)) {
      goto failure;
    }
  }
  pg_set_destroy(&target);
  pg_set_destroy(&top);
  pg_set_destroy(&subgame);
  pg_set_destroy(&side);
  return true;

failure:
  pg_set_destroy(&target);
  pg_set_destroy(&top);
  pg_set_destroy(&subgame);
  pg_set_destroy(&side);
  return false;
}

[[nodiscard]] static bool solve(Sym *state, PGSet *domain,
                                PGPlayer const player, size_t const node,
                                size_t const opponent_node) {
  ADTracePath paths[2] = {0};
  if (state->trace != nullptr) {
    paths[player] = ad_trace_host_path(state->tree + player, node,
                                       state->trace_paths[player]);
    paths[1 - player] =
        ad_trace_host_path(state->tree + (1 - player), opponent_node,
                           state->trace_paths[1 - player]);
    AD_TRACE(state->trace, .kind = AD_TRACE_CALL, .player = player,
             .h = state->tree[player].nodes[node].level, .path = paths[player],
             .paths = {paths[0], paths[1]}, .subgame = domain);
    if (state->trace->failed) {
      return false;
    }
  }
  bool const success = solve_impl(state, domain, player, node, opponent_node);
  if (success && state->trace != nullptr) {
    paths[player] = ad_trace_host_path(state->tree + player, node,
                                       state->trace_paths[player]);
    AD_TRACE(state->trace, .kind = AD_TRACE_RETURN, .player = player,
             .path = paths[player], .vertices = domain);
  }
  return success && (state->trace == nullptr || !state->trace->failed);
}

/* Flatten only the nodes actually visited by Algorithm 8, or the supplied
 * finite host, to reuse the labelling verifier and classic materializer. */
static void flatten(ADLiftTree *tree, size_t const index, size_t *position) {
  ADLiftNode *node = tree->nodes + index;
  node->regular = (*position)++;
  tree->positions[node->regular] =
      (ADPosition){AD_POS_REGULAR, true, node->level, index};
  node->minus_inf = (*position)++;
  tree->positions[node->minus_inf] =
      (ADPosition){AD_POS_MINUS_INF, node->level != 0,
                   node->level == 0 ? 0 : node->level - 1, index};
  for (size_t child = node->first_child; child != SIZE_MAX;
       child = tree->nodes[child].next_sibling) {
    flatten(tree, child, position);
    tree->positions[(*position)++] =
        (ADPosition){AD_POS_PLUS, true, node->level - 1, child};
  }
  node->after = *position;
}

[[nodiscard]] static bool
sym_solve_trace(PGGame const *game, PGSet const *domain,
                ADLiftTree const *hosts, bool const verify, ADResult *result,
                ADSymStats *stats, ADLiftError *error, ADTrace *trace) {
  if (error != nullptr) {
    *error = (ADLiftError){0};
  }
  if (result != nullptr) {
    *result = (ADResult){0};
  }
  ADSymStats local = {0};
  if (stats == nullptr) {
    stats = &local;
  }
  *stats = (ADSymStats){0};
  if (game == nullptr || domain == nullptr || result == nullptr ||
      domain->bit_count != game->vertex_count ||
      game->vertex_count > SIZE_MAX / sizeof(Label) ||
      !pg_subgame_is_total(game, domain)) {
    sym_error(error, "invalid symmetric solver input");
    return false;
  }
  size_t trace_paths[2][AD_PATH_CAPACITY];
  Sym state = {.game = game,
               .memory = hosts == nullptr,
               .arity = pg_set_count(domain),
               .stats = stats,
               .trace = trace,
               .trace_paths = trace_paths};
  PGSet separator = {0};
  uint64_t maximum = 0;
  for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(domain, vertex + 1)) {
    if (game->vertices[vertex].priority > maximum) {
      maximum = game->vertices[vertex].priority;
    }
  }
  if (maximum > 1024) {
    sym_error(error, "priority depth exceeds the safe recursion depth");
    return false;
  }
  if (!pg_set_clone(&separator, domain) ||
      !pg_set_init(&state.safe, game->vertex_count) ||
      !pg_set_init(&state.lower, game->vertex_count) ||
      !pg_set_init(&state.checked, game->vertex_count) ||
      !pg_set_clone(&result->unresolved, domain)) {
    goto failure;
  }
  for (size_t player = 0; player < 2; player++) {
    ADLiftTree *tree = state.tree + player;
    tree->player = (PGPlayer)player;
    if (hosts == nullptr) {
      uint64_t const level = maximum % 2 == player ? maximum : maximum + 1;
      if (add_node(&state, (PGPlayer)player, level, SIZE_MAX) == SIZE_MAX) {
        goto failure;
      }
    } else {
      ADLiftTree const *host = hosts + player;
      if (host->nodes == nullptr || host->node_count == 0 ||
          host->node_count > SIZE_MAX / (3 * sizeof(ADPosition)) ||
          host->player != player || host->nodes[0].level < maximum ||
          host->nodes[0].level > 1025) {
        sym_error(error, "invalid symmetric host");
        goto failure;
      }
      tree->node_count = host->node_count;
      tree->nodes = malloc(tree->node_count * sizeof(tree->nodes[0]));
      if (tree->nodes == nullptr) {
        goto failure;
      }
      memcpy(tree->nodes, host->nodes,
             tree->node_count * sizeof(tree->nodes[0]));
    }
    state.labels[player] =
        calloc(game->vertex_count == 0 ? 1 : game->vertex_count, sizeof(Label));
    if (state.labels[player] == nullptr ||
        !pg_set_init(result->region + player, game->vertex_count)) {
      goto failure;
    }
    AD_TRACE(trace, .kind = AD_TRACE_PHASE, .player = (PGPlayer)player,
             .name = "labels_reset");
    for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(domain, vertex + 1)) {
      state.labels[player][vertex] =
          assign(&state, (PGPlayer)player, 0, vertex);
      if (trace != nullptr) {
        Label const label = state.labels[player][vertex];
        ad_trace_label(trace, tree, vertex, label.node, label.kind,
                       AD_TRACE_LABELS, 0, 0);
        if (trace->failed) {
          goto failure;
        }
      }
    }
  }
  /* The upper root has no highest-priority vertices. Its empty top attractor
   * must be moved before visiting its children (Lemma 8.2.1's call invariant).
   */
  PGPlayer const upper = (PGPlayer)(1 - maximum % 2);
  if (!pg_set_empty(domain) &&
      !move(&state, domain, upper, 0, AD_POS_MINUS_INF, &state.lower)) {
    goto failure;
  }
  if (!pg_set_empty(domain) &&
      !solve(&state, &separator, (PGPlayer)(maximum % 2), 0, 0)) {
    goto failure;
  }
  stats->separator = maximum % 2 == PG_EVEN
                         ? pg_set_count(&separator)
                         : pg_set_count(domain) - pg_set_count(&separator);
  for (size_t player = 0; player < 2; player++) {
    ADLiftTree *tree = state.tree + player;
    if (tree->node_count > SIZE_MAX / (3 * sizeof(ADPosition))) {
      goto failure;
    }
    tree->position_count = 3 * tree->node_count;
    tree->positions = calloc(tree->position_count, sizeof(tree->positions[0]));
    ADLiftLabelling labelling = {.vertex_count = game->vertex_count};
    labelling.labels =
        malloc((game->vertex_count == 0 ? 1 : game->vertex_count) *
               sizeof(labelling.labels[0]));
    if (tree->positions == nullptr || labelling.labels == nullptr ||
        !pg_set_init(&labelling.dominion, game->vertex_count)) {
      ad_lift_labelling_destroy(&labelling);
      goto failure;
    }
    size_t position = 0;
    flatten(tree, 0, &position);
    tree->top = position;
    tree->positions[position] = (ADPosition){.kind = AD_POS_TOP};
    for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
      labelling.labels[vertex] = SIZE_MAX;
      if (!pg_set_contains(domain, vertex)) {
        continue;
      }
      Label const label = state.labels[player][vertex];
      ADLiftNode const *node = tree->nodes + label.node;
      labelling.labels[vertex] = label.kind == AD_POS_REGULAR ? node->regular
                                 : label.kind == AD_POS_MINUS_INF
                                     ? node->minus_inf
                                     : node->after;
      if (labelling.labels[vertex] < tree->top) {
        pg_set_add(&labelling.dominion, vertex);
      }
    }
#ifndef NDEBUG
    /* shortcut: bound the independent debug solve to small inputs; raise these
     * limits when checking larger raw labellings, not for production runs. */
    if (game->vertex_count <= 12 && tree->node_count <= 4096) {
      ADLiftLabelling least = {0};
      if (!ad_lift_run(game, domain, tree, AD_LIFT_SCHEDULE_ROUNDS, &least,
                       error)) {
        ad_lift_labelling_destroy(&labelling);
        goto failure;
      }
      for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
           vertex = pg_set_next(domain, vertex + 1)) {
        assert(labelling.labels[vertex] <= least.labels[vertex]);
      }
      ad_lift_labelling_destroy(&least);
    }
#endif
    /* Theorem F guarantees a separator, not certificates for arbitrary hosts.
     * Finish short lifts from D ⊑ A to obtain both least valid labellings. */
    if (!state.memory &&
        !ad_lift_continue_trace(game, domain, tree, &labelling, error, trace)) {
      ad_lift_labelling_destroy(&labelling);
      goto failure;
    }
    stats->certification_lifts += labelling.stats.lifts;
    stats->attractors += labelling.stats.attractors;
    ADVerifyError verify_error = {0};
    bool const success =
        (!verify ||
         ad_lift_verify(game, domain, tree, &labelling, &verify_error)) &&
        ad_lift_materialize(game, tree, &labelling,
                            result->decomposition + player, error);
    pg_set_move(result->region + player, &labelling.dominion);
    ad_lift_labelling_destroy(&labelling);
    if (!success) {
      sym_error(error, verify_error.message);
      goto failure;
    }
    AD_TRACE(trace, .kind = AD_TRACE_PHASE, .player = (PGPlayer)player,
             .name = "dominion_certified", .vertices = result->region + player);
    pg_set_subtract_into(&result->unresolved, result->region + player);
  }
  PGPlayer const first = (PGPlayer)(maximum % 2);
  pg_set_clear(&state.safe);
  pg_set_union_into(&state.safe, result->region + (1 - first));
  pg_set_intersect_into(&state.safe, &separator);
  if (!pg_set_subset(result->region + first, &separator) ||
      !pg_set_empty(&state.safe) ||
      (state.memory && !pg_set_empty(&result->unresolved))) {
    sym_error(error, "symmetric separator violates its certificate bounds");
    goto failure;
  }
  result->kind = pg_set_empty(&result->unresolved) ? AD_RESULT_COMPLETE
                                                   : AD_RESULT_PARTIAL;
  if (verify) {
    ADVerifyError verify_error = {0};
    if (!ad_result_verify_partial(game, domain, result, &verify_error)) {
      sym_error(error, verify_error.message);
      goto failure;
    }
  }
  for (size_t player = 0; player < 2; player++) {
    ad_lift_tree_destroy(state.tree + player);
    free(state.labels[player]);
    free(state.ordinals[player]);
  }
  pg_set_destroy(&separator);
  pg_set_destroy(&state.safe);
  pg_set_destroy(&state.lower);
  pg_set_destroy(&state.checked);
  return true;

failure:
  sym_error(error, "failed to construct symmetric decompositions");
  for (size_t player = 0; player < 2; player++) {
    ad_lift_tree_destroy(state.tree + player);
    free(state.labels[player]);
    free(state.ordinals[player]);
  }
  pg_set_destroy(&separator);
  pg_set_destroy(&state.safe);
  pg_set_destroy(&state.lower);
  pg_set_destroy(&state.checked);
  ad_result_destroy(result);
  return false;
}

bool ad_sym_solve(PGGame const *game, PGSet const *domain,
                  ADLiftTree const *hosts, bool const verify, ADResult *result,
                  ADSymStats *stats, ADLiftError *error) {
  return ad_sym_solve_trace(game, domain, hosts, verify, result, stats, error,
                            nullptr);
}

bool ad_sym_solve_trace(PGGame const *game, PGSet const *domain,
                        ADLiftTree const *hosts, bool const verify,
                        ADResult *result, ADSymStats *stats, ADLiftError *error,
                        ADTrace *trace) {
  if (trace != nullptr) {
    trace->nesting++;
  }
  bool success =
      sym_solve_trace(game, domain, hosts, verify, result, stats, error, trace);
  if (trace != nullptr) {
    if (--trace->nesting == 0 && success) {
      AD_TRACE(trace, .kind = AD_TRACE_RESULT, .result = result);
    }
    if (success && trace->failed) {
      ad_result_destroy(result);
      sym_error(error, "trace observer stopped the run");
      success = false;
    }
  }
  return success;
}
