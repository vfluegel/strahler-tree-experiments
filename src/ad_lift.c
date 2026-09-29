#include <stdio.h>
#include <stdlib.h>

#include "ad_lift.h"
#include "pg_attractor.h"

/* Why short lifts compute the least valid labelling.
 *
 * Let ν be any valid labelling with μ <= ν pointwise, and let v be invalid
 * under μ. Put v at ν(v) and keep every other vertex at its μ label, which is
 * no larger than under ν. At a regular position, lowering successor labels
 * only helps the test label(u) < after(ν(v)). At a lazy position, it only
 * enlarges {label <= ν(v)} and {label < ν(v)}, so the reach-through-safe
 * strategy valid under ν remains available. Thus v is valid at ν(v), and
 * ν(v) != μ(v) because v is invalid at μ(v). As ν(v) is compatible with v, the
 * next compatible position after μ(v) is at most ν(v).
 *
 * This argument is independent for every invalid vertex, so lifting all of
 * them against the same μ keeps μ <= ν. The initial labelling, which takes the
 * least compatible position of every vertex, is below every valid labelling;
 * labels only increase and the host is finite, so the loop terminates with
 * every vertex valid. That labelling is below every valid labelling, so it is
 * the least one. The all-TOP labelling is valid, so a valid labelling exists.
 */

static void lift_error(ADLiftError *error, char const *message) {
  if (error != nullptr && error->message[0] == '\0') {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

static void verify_error(ADVerifyError *error, char const *message) {
  if (error != nullptr && error->message[0] == '\0') {
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
  }
}

typedef struct {
  size_t label;
  size_t vertex;
} LabelledVertex;

[[nodiscard]] static int compare_labelled(void const *left, void const *right) {
  LabelledVertex const *first = left;
  LabelledVertex const *second = right;
  if (first->label != second->label) {
    return first->label < second->label ? -1 : 1;
  }
  return first->vertex < second->vertex   ? -1
         : first->vertex > second->vertex ? 1
                                          : 0;
}

/* Active vertices with a label below limit, sorted by label and index. */
[[nodiscard]] static LabelledVertex *sort_by_label(PGSet const *active,
                                                   size_t const *labels,
                                                   size_t const limit,
                                                   size_t *count) {
  size_t const total = pg_set_count(active);
  if (total > SIZE_MAX / sizeof(LabelledVertex)) {
    return nullptr;
  }
  LabelledVertex *sorted = malloc((total == 0 ? 1 : total) * sizeof(*sorted));
  if (sorted == nullptr) {
    return nullptr;
  }
  size_t used = 0;
  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    if (labels[vertex] < limit) {
      sorted[used++] =
          (LabelledVertex){.label = labels[vertex], .vertex = vertex};
    }
  }
  if (used > 1) {
    qsort(sorted, used, sizeof(sorted[0]), compare_labelled);
  }
  *count = used;
  return sorted;
}

/* The one-step condition at REGULAR(η): an edge to u is allowed when u is
 * labelled below after(η). */
[[nodiscard]] static bool
regular_valid(PGGame const *game, PGSet const *active, size_t const *labels,
              PGPlayer const player, size_t const vertex, size_t const after) {
  bool const owned = game->vertices[vertex].owner == player;
  for (size_t edge = game->succ_offsets[vertex];
       edge < game->succ_offsets[vertex + 1]; edge++) {
    size_t const successor = game->successors[edge];
    if (!pg_set_contains(active, successor)) {
      continue;
    }
    bool const allowed = labels[successor] < after;
    if (owned && allowed) {
      return true;
    }
    if (!owned && !allowed) {
      return false;
    }
  }
  return !owned;
}

typedef struct {
  PGGame const *game;
  PGSet const *active;
  ADLiftTree const *host;
  size_t *labels;
  LabelledVertex *sorted;
  PGSet below;
  PGSet safe;
  PGSet invalid;
} LiftState;

/* Mark every invalid vertex of the current labelling. Positions are visited in
 * host order; below holds {μ < ℓ} when position ℓ is reached, so a lazy
 * position costs one reach-through-safe attractor computation. */
[[nodiscard]] static bool mark_invalid(LiftState *state) {
  ADLiftTree const *host = state->host;
  size_t count = 0;
  LabelledVertex *sorted =
      sort_by_label(state->active, state->labels, host->top, &count);
  if (sorted == nullptr) {
    return false;
  }
  free(state->sorted);
  state->sorted = sorted;
  pg_set_clear(&state->below);
  pg_set_clear(&state->invalid);

  size_t begin = 0;
  while (begin < count) {
    size_t const position = sorted[begin].label;
    size_t end = begin;
    while (end < count && sorted[end].label == position) {
      end++;
    }
    ADPosition const *current = host->positions + position;
    if (current->kind == AD_POS_REGULAR) {
      size_t const after = host->nodes[current->node].after;
      for (size_t index = begin; index < end; index++) {
        size_t const vertex = sorted[index].vertex;
        if (!regular_valid(state->game, state->active, state->labels,
                           host->player, vertex, after)) {
          pg_set_add(&state->invalid, vertex);
        }
      }
    } else {
      pg_set_clear(&state->safe);
      pg_set_union_into(&state->safe, &state->below);
      for (size_t index = begin; index < end; index++) {
        pg_set_add(&state->safe, sorted[index].vertex);
      }
      PGSet attracted = {0};
      if (!pg_attractor_through(state->game, state->active, &state->safe,
                                &state->below, host->player, &attracted)) {
        return false;
      }
      for (size_t index = begin; index < end; index++) {
        if (!pg_set_contains(&attracted, sorted[index].vertex)) {
          pg_set_add(&state->invalid, sorted[index].vertex);
        }
      }
      pg_set_destroy(&attracted);
    }
    for (size_t index = begin; index < end; index++) {
      pg_set_add(&state->below, sorted[index].vertex);
    }
    begin = end;
  }
  return true;
}

static void lift(LiftState *state, size_t const vertex) {
  /* TOP is always valid, so an invalid vertex lies strictly below it. */
  state->labels[vertex] =
      ad_lift_tree_first_compatible(state->host, state->labels[vertex] + 1,
                                    state->game->vertices[vertex].priority);
}

void ad_lift_labelling_destroy(ADLiftLabelling *labelling) {
  if (labelling == nullptr) {
    return;
  }
  free(labelling->labels);
  pg_set_destroy(&labelling->dominion);
  *labelling = (ADLiftLabelling){0};
}

[[nodiscard]] static bool valid_host(ADLiftTree const *host) {
  return host != nullptr && host->positions != nullptr &&
         host->nodes != nullptr && host->player <= PG_ODD &&
         host->top + 1 == host->position_count;
}

bool ad_lift_run(PGGame const *game, PGSet const *active,
                 ADLiftTree const *host, ADLiftSchedule const schedule,
                 ADLiftLabelling *result, ADLiftError *error) {
  if (error != nullptr) {
    *error = (ADLiftError){0};
  }
  if (result != nullptr) {
    *result = (ADLiftLabelling){0};
  }
  if (game == nullptr || active == nullptr || result == nullptr ||
      !valid_host(host) || schedule > AD_LIFT_SCHEDULE_SINGLE ||
      active->bit_count != game->vertex_count ||
      !pg_subgame_is_total(game, active)) {
    lift_error(error, "invalid lifting input");
    return false;
  }
  size_t const count = game->vertex_count;
  if (count > SIZE_MAX / sizeof(size_t)) {
    lift_error(error, "the game is too large");
    return false;
  }

  LiftState state = {.game = game, .active = active, .host = host};
  state.labels = malloc((count == 0 ? 1 : count) * sizeof(state.labels[0]));
  if (state.labels == nullptr || !pg_set_init(&state.below, count) ||
      !pg_set_init(&state.safe, count) || !pg_set_init(&state.invalid, count) ||
      !pg_set_init(&result->dominion, count)) {
    lift_error(error, "failed to allocate the lifting state");
    goto failure;
  }
  for (size_t vertex = 0; vertex < count; vertex++) {
    state.labels[vertex] = pg_set_contains(active, vertex)
                               ? ad_lift_tree_first_compatible(
                                     host, 0, game->vertices[vertex].priority)
                               : SIZE_MAX;
  }

  ADLiftStats stats = {.max_position = SIZE_MAX};
  while (true) {
    if (!mark_invalid(&state)) {
      lift_error(error, "failed to evaluate the labelling");
      goto failure;
    }
    size_t const first = pg_set_next(&state.invalid, 0);
    if (first == SIZE_MAX) {
      break;
    }
    stats.rounds++;
    if (schedule == AD_LIFT_SCHEDULE_SINGLE) {
      lift(&state, first);
      stats.lifts++;
      continue;
    }
    for (size_t vertex = first; vertex != SIZE_MAX;
         vertex = pg_set_next(&state.invalid, vertex + 1)) {
      lift(&state, vertex);
      stats.lifts++;
    }
  }

  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    size_t const label = state.labels[vertex];
    if (label == host->top) {
      stats.top_count++;
      continue;
    }
    pg_set_add(&result->dominion, vertex);
    if (stats.max_position == SIZE_MAX || label > stats.max_position) {
      stats.max_position = label;
    }
  }
  result->labels = state.labels;
  result->vertex_count = count;
  result->stats = stats;
  free(state.sorted);
  pg_set_destroy(&state.below);
  pg_set_destroy(&state.safe);
  pg_set_destroy(&state.invalid);
  return true;

failure:
  free(state.labels);
  free(state.sorted);
  pg_set_destroy(&state.below);
  pg_set_destroy(&state.safe);
  pg_set_destroy(&state.invalid);
  ad_lift_labelling_destroy(result);
  return false;
}

/* Recompute {μ <= position} and {μ < position} directly from the labels. */
static void split_at(PGSet const *active, size_t const *labels,
                     size_t const position, PGSet *safe, PGSet *lower) {
  pg_set_clear(safe);
  pg_set_clear(lower);
  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    if (labels[vertex] <= position) {
      pg_set_add(safe, vertex);
    }
    if (labels[vertex] < position) {
      pg_set_add(lower, vertex);
    }
  }
}

bool ad_lift_verify(PGGame const *game, PGSet const *active,
                    ADLiftTree const *host, ADLiftLabelling const *labelling,
                    ADVerifyError *error) {
  if (error != nullptr) {
    *error = (ADVerifyError){0};
  }
  if (game == nullptr || active == nullptr || labelling == nullptr ||
      labelling->labels == nullptr || !valid_host(host) ||
      labelling->vertex_count != game->vertex_count ||
      active->bit_count != game->vertex_count ||
      labelling->dominion.bit_count != game->vertex_count ||
      !pg_subgame_is_total(game, active)) {
    verify_error(error, "invalid labelling verifier input");
    return false;
  }
  size_t const *labels = labelling->labels;
  for (size_t vertex = 0; vertex < game->vertex_count; vertex++) {
    if (!pg_set_contains(active, vertex)) {
      if (labels[vertex] != SIZE_MAX ||
          pg_set_contains(&labelling->dominion, vertex)) {
        verify_error(error, "an inactive vertex is labelled");
        return false;
      }
      continue;
    }
    if (labels[vertex] > host->top) {
      verify_error(error, "a label is outside the host");
      return false;
    }
    if (!ad_position_accepts_priority(host, labels[vertex],
                                      game->vertices[vertex].priority)) {
      verify_error(error, "a label is not priority compatible");
      return false;
    }
    if ((labels[vertex] != host->top) !=
        pg_set_contains(&labelling->dominion, vertex)) {
      verify_error(error, "the dominion is not the non-TOP region");
      return false;
    }
  }

  PGSet safe = {0};
  PGSet lower = {0};
  PGSet checked = {0};
  if (!pg_set_init(&safe, game->vertex_count) ||
      !pg_set_init(&lower, game->vertex_count) ||
      !pg_set_init(&checked, game->vertex_count)) {
    verify_error(error, "failed to allocate labelling verifier sets");
    goto failure;
  }
  for (size_t vertex = pg_set_next(active, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(active, vertex + 1)) {
    size_t const position = labels[vertex];
    ADPosition const *current = host->positions + position;
    if (current->kind == AD_POS_TOP) {
      continue;
    }
    if (current->kind == AD_POS_REGULAR) {
      if (!regular_valid(game, active, labels, host->player, vertex,
                         host->nodes[current->node].after)) {
        verify_error(error, "a regular condition fails");
        goto failure;
      }
      continue;
    }
    if (pg_set_contains(&checked, vertex)) {
      continue;
    }
    PGSet attracted = {0};
    split_at(active, labels, position, &safe, &lower);
    if (!pg_attractor_through(game, active, &safe, &lower, host->player,
                              &attracted)) {
      verify_error(error, "failed to compute a lazy attractor");
      goto failure;
    }
    for (size_t other = vertex; other != SIZE_MAX;
         other = pg_set_next(active, other + 1)) {
      if (labels[other] != position) {
        continue;
      }
      pg_set_add(&checked, other);
      if (!pg_set_contains(&attracted, other)) {
        pg_set_destroy(&attracted);
        verify_error(error, "a lazy reach-through-safe condition fails");
        goto failure;
      }
    }
    pg_set_destroy(&attracted);
  }
  pg_set_destroy(&safe);
  pg_set_destroy(&lower);
  pg_set_destroy(&checked);
  return true;

failure:
  pg_set_destroy(&safe);
  pg_set_destroy(&lower);
  pg_set_destroy(&checked);
  return false;
}

typedef struct {
  PGGame const *game;
  ADLiftTree const *host;
  LabelledVertex const *sorted;
  size_t count;
  ADLiftError *error;
} Materializer;

/* The first sorted entry whose label is at least position. */
[[nodiscard]] static size_t lower_bound(Materializer const *materializer,
                                        size_t const position) {
  size_t low = 0;
  size_t high = materializer->count;
  while (low < high) {
    size_t const middle = low + (high - low) / 2;
    if (materializer->sorted[middle].label < position) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low;
}

static void add_range(Materializer const *materializer, PGSet *set,
                      size_t const begin, size_t const end) {
  for (size_t index = begin; index < end; index++) {
    pg_set_add(set, materializer->sorted[index].vertex);
  }
}

[[nodiscard]] static ADNode *materialize_node(Materializer const *materializer,
                                              size_t const index) {
  ADLiftTree const *host = materializer->host;
  ADLiftNode const *node = host->nodes + index;
  size_t const vertex_count = materializer->game->vertex_count;
  ADNode *result = ad_node_create(host->player, node->level, vertex_count);
  if (result == nullptr) {
    lift_error(materializer->error, "failed to allocate a decomposition node");
    return nullptr;
  }
  add_range(materializer, &result->top_attractor,
            lower_bound(materializer, node->regular),
            lower_bound(materializer, node->minus_inf + 1));

  /* Visit only the host children whose blocks, including their c+ position,
   * hold a vertex: every other child is pruned. */
  size_t next = lower_bound(materializer, node->minus_inf + 1);
  size_t const end = lower_bound(materializer, node->after);
  while (next < end) {
    size_t child = host->positions[materializer->sorted[next].label].node;
    while (host->nodes[child].parent != index) {
      child = host->nodes[child].parent;
    }
    ADLiftNode const *child_node = host->nodes + child;
    size_t const core_end = lower_bound(materializer, child_node->after);
    size_t const outer_end = lower_bound(materializer, child_node->after + 1);
    if (core_end == next) {
      lift_error(materializer->error,
                 "a nonempty c+ fibre has an empty child core");
      ad_node_destroy(result);
      return nullptr;
    }
    ADChild entry = {0};
    if (!pg_set_init(&entry.trap, vertex_count) ||
        !pg_set_init(&entry.attractor, vertex_count)) {
      pg_set_destroy(&entry.trap);
      lift_error(materializer->error, "failed to allocate a child");
      ad_node_destroy(result);
      return nullptr;
    }
    add_range(materializer, &entry.trap, next, core_end);
    add_range(materializer, &entry.attractor, next, outer_end);
    entry.subtree = materialize_node(materializer, child);
    if (entry.subtree == nullptr || !ad_node_append_child(result, &entry)) {
      pg_set_destroy(&entry.trap);
      pg_set_destroy(&entry.attractor);
      ad_node_destroy(entry.subtree);
      lift_error(materializer->error, "failed to append a child");
      ad_node_destroy(result);
      return nullptr;
    }
    next = outer_end;
  }
  return result;
}

bool ad_lift_materialize(PGGame const *game, ADLiftTree const *host,
                         ADLiftLabelling const *labelling, ADNode **root,
                         ADLiftError *error) {
  if (error != nullptr) {
    *error = (ADLiftError){0};
  }
  if (root != nullptr) {
    *root = nullptr;
  }
  if (game == nullptr || labelling == nullptr || root == nullptr ||
      labelling->labels == nullptr || !valid_host(host) ||
      labelling->vertex_count != game->vertex_count ||
      labelling->dominion.bit_count != game->vertex_count) {
    lift_error(error, "invalid materialization input");
    return false;
  }
  if (pg_set_empty(&labelling->dominion)) {
    return true;
  }
  size_t count = 0;
  LabelledVertex *sorted =
      sort_by_label(&labelling->dominion, labelling->labels, host->top, &count);
  if (sorted == nullptr) {
    lift_error(error, "failed to sort the labelling");
    return false;
  }
  Materializer const materializer = {
      .game = game,
      .host = host,
      .sorted = sorted,
      .count = count,
      .error = error,
  };
  *root = materialize_node(&materializer, 0);
  free(sorted);
  return *root != nullptr;
}
