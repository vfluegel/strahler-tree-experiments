#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

#include "dot_utils.h"
#include "json_utils.h"
#include "ordered_tree.h"
#include "pg_dot.h"
#include "pm_tree_dot.h"

typedef struct {
  PMMeasure const *measure;
  size_t vertex;
  size_t order;
} Member;

static int compare_members(void const *left, void const *right) {
  Member const *a = left;
  Member const *b = right;
  int const comparison = pm_compare(a->measure, b->measure, INT_MAX);
  return comparison != 0 ? comparison
                         : (a->vertex > b->vertex) - (a->vertex < b->vertex);
}

static int compare_order(void const *left, void const *right) {
  Member const *a = left;
  Member const *b = right;
  return (a->order > b->order) - (a->order < b->order);
}

static Member *members_for(PMResult const *result, size_t *member_count) {
  size_t const count = pg_set_count(&result->region);
  *member_count = count;
  if (count > SIZE_MAX / sizeof(Member)) {
    return nullptr;
  }
  Member *members = malloc((count == 0 ? 1 : count) * sizeof(members[0]));
  if (members == nullptr || count == 0) {
    return members;
  }
  size_t index = 0;
  for (size_t order = 0; order < result->vertex_count; order++) {
    size_t const vertex = result->order[order];
    if (pg_set_contains(&result->region, vertex)) {
      assert(index < count);
      members[index++] = (Member){&result->measure[vertex], vertex, order};
    }
  }
  qsort(members, count, sizeof(members[0]), compare_members);
  return members;
}

static bool write_branch(FILE *out, PMMeasure const *measure, int const depth) {
  int position = 0;
  for (int level = 0; level < depth; level++) {
    if (level != 0 && fputc(',', out) == EOF) {
      return false;
    }
    int const start = position;
    while (position < measure->length && measure->levels[position] == level) {
      if (fputc('0' + measure->bits[position++], out) == EOF) {
        return false;
      }
    }
    if (position == start && fputc('e', out) == EOF) {
      return false;
    }
  }
  return fputc('|', out) != EOF;
}

bool pm_write_leaves(FILE *out, PMResult const *result) {
  size_t count = 0;
  Member *members = members_for(result, &count);
  if (members == nullptr) {
    return false;
  }
  size_t distinct = 0;
  for (size_t index = 0; index < count;) {
    Member first = members[index++];
    while (index < count &&
           pm_compare(first.measure, members[index].measure, INT_MAX) == 0) {
      if (members[index].order < first.order) {
        first = members[index];
      }
      index++;
    }
    members[distinct++] = first;
  }
  qsort(members, distinct, sizeof(members[0]), compare_order);
  bool wrote = true;
  for (size_t index = 0; index < distinct && wrote; index++) {
    wrote = write_branch(out, members[index].measure, result->depth);
  }
  free(members);
  return wrote && fputc('\n', out) != EOF;
}

typedef struct {
  FILE *out;
  PGGame const *game;
  PMResult const *result;
  Member const *members;
  size_t count;
  size_t position;
  size_t next_id;
  size_t root_id;
  bool partial;
  bool json;
  ADDotLabels labels;
  size_t max_items;
  PGPriorityMap const *priority_map;
  PGSet held;
} TreeWriter;

static bool write_region(TreeWriter const *writer, PGSet const *region,
                         char const *name) {
  if (writer->labels == AD_DOT_LABEL_NONE) {
    return true;
  }
  if (writer->labels == AD_DOT_LABEL_COUNTS) {
    return fprintf(writer->out, "\\n|%s|=%zu", name, pg_set_count(region)) >= 0;
  }
  return fprintf(writer->out, "\\n%s=", name) >= 0 &&
         pg_dot_write_set(writer->out, writer->game, region, writer->max_items);
}

static bool write_parameters(FILE *out, PMResult const *result) {
  if (result->algorithm == PM_STRAHLER) {
    return fprintf(out, "strahler k=%zu t=%zu depth=%d", result->parameters.k,
                   result->parameters.t, result->depth) >= 0;
  }
  return fprintf(out, "succinct bits=%zu depth=%d", result->parameters.bits,
                 result->depth) >= 0;
}

static bool node_priority(TreeWriter const *writer, int const level,
                          uint64_t *priority) {
  *priority = (uint64_t)(2 * (writer->result->depth - level - 1)) +
              (1 - writer->result->player);
  return writer->priority_map == nullptr ||
         pg_priority_map_original_bound(writer->priority_map, *priority,
                                        priority);
}

static void leaf_vertices(TreeWriter *writer, OrderedTreeNode const *node) {
  pg_set_clear(&writer->held);
  if (node != nullptr) {
    assert(writer->position < writer->count);
    PMMeasure const *measure = writer->members[writer->position].measure;
    do {
      pg_set_add(&writer->held, writer->members[writer->position++].vertex);
    } while (writer->position < writer->count &&
             pm_compare(measure, writer->members[writer->position].measure,
                        INT_MAX) == 0);
  }
}

static bool write_json_node(TreeWriter *writer, OrderedTreeNode const *node,
                            int const level) {
  FILE *out = writer->out;
  if (node == nullptr) {
    return fputs("null", out) >= 0;
  }
  fputs("{\"priority\":", out);
  if (node->is_leaf) {
    fputs("null,\"vertices\":", out);
    leaf_vertices(writer, node);
    json_set(&(JSONWriter){out, writer->game}, &writer->held);
  } else {
    uint64_t priority = 0;
    if (!node_priority(writer, level, &priority)) {
      return false;
    }
    fprintf(out, "%" PRIu64 ",\"vertices\":null", priority);
  }
  fputs(",\"children\":[", out);
  for (size_t index = 0; index < node->child_count; index++) {
    if (index != 0) {
      fputc(',', out);
    }
    fputs("{\"label\":", out);
    json_string(out, node->children[index].label);
    fputs(",\"node\":", out);
    if (!write_json_node(writer, node->children[index].child, level + 1) ||
        fputc('}', out) == EOF) {
      return false;
    }
  }
  return fputs("]}", out) >= 0 && !ferror(out);
}

static bool write_node(TreeWriter *writer, OrderedTreeNode const *node,
                       size_t const id, int const level) {
  FILE *out = writer->out;
  if (fprintf(out, "  n%zu [label=\"", id) < 0) {
    return false;
  }
  if (id == writer->root_id) {
    if (fprintf(out, "%s (%s)\\n",
                writer->result->player == PG_EVEN ? "Even" : "Odd",
                writer->partial ? "partial" : "complete") < 0 ||
        !write_parameters(out, writer->result)) {
      return false;
    }
    if (node == nullptr && fputs("\\nno non-top measures", out) < 0) {
      return false;
    }
  }
  if (node != nullptr && !node->is_leaf &&
      writer->labels != AD_DOT_LABEL_NONE) {
    uint64_t priority = 0;
    if (!node_priority(writer, level, &priority)) {
      return false;
    }
    if (fprintf(out, "%sp=%" PRIu64, id == writer->root_id ? "\\n" : "",
                priority) < 0) {
      return false;
    }
  }
  if (node == nullptr || node->is_leaf) {
    leaf_vertices(writer, node);
    if (!write_region(writer, &writer->held, "V")) {
      return false;
    }
  }
  if (fputs("\"];\n", out) < 0) {
    return false;
  }
  if (node == nullptr) {
    return true;
  }
  for (size_t index = 0; index < node->child_count; index++) {
    size_t const child = ++writer->next_id;
    if (fprintf(out, "  n%zu -> n%zu [label=", id, child) < 0 ||
        !dot_write_quoted(out, node->children[index].label) ||
        fputs("];\n", out) < 0 ||
        !write_node(writer, node->children[index].child, child, level + 1)) {
      return false;
    }
  }
  return true;
}

static bool write_player(TreeWriter *writer, PGSet const *shaved) {
  Member *members = members_for(writer->result, &writer->count);
  if (members == nullptr) {
    return false;
  }
  writer->members = members;
  char *stream = nullptr;
  size_t length = 0;
  FILE *memory = open_memstream(&stream, &length);
  OrderedTreeNode *tree = nullptr;
  bool wrote = false;
  if (memory == nullptr) {
    goto cleanup;
  }
  bool valid = true;
  for (size_t index = 0; index < writer->count && valid; index++) {
    if (index == 0 || pm_compare(members[index - 1].measure,
                                 members[index].measure, INT_MAX) != 0) {
      valid =
          write_branch(memory, members[index].measure, writer->result->depth);
    }
  }
  if (fclose(memory) != 0) {
    valid = false;
  }
  OrderedTreeError error = {0};
  if (!valid || (writer->count != 0 &&
                 !ordered_tree_parse_leaf_stream_ordered(
                     stream, ORDERED_TREE_CHECK_VECTOR_ORDER, &tree, &error))) {
    goto cleanup;
  }
  if (writer->json) {
    PMResult const *result = writer->result;
    fprintf(writer->out,
            "{\"parameters\":{\"k\":%zu,\"t\":%zu,\"bits\":%zu,\"depth\":%d},"
            "\"tree\":",
            result->parameters.k, result->parameters.t, result->parameters.bits,
            result->depth);
    if (!write_json_node(writer, tree, 0)) {
      goto cleanup;
    }
    JSONWriter const output = {writer->out, writer->game};
    fputs(",\"shaved\":", writer->out);
    json_set(&output, shaved);
    fputs(",\"attracted\":", writer->out);
    json_set(&output, &result->attracted);
    wrote = fputc('}', writer->out) != EOF && !ferror(writer->out);
    goto cleanup;
  }
  if (!write_node(writer, tree, writer->root_id, 0)) {
    goto cleanup;
  }
  PGSet const *const extra[] = {shaved, &writer->result->attracted};
  char const *const names[] = {"shaved", "attracted"};
  for (size_t kind = 0; kind < 2; kind++) {
    if (!pg_set_empty(extra[kind])) {
      size_t const id = ++writer->next_id;
      if (fprintf(writer->out,
                  "  n%zu [style=filled, fillcolor=lightgrey, label=\"%s (%s)",
                  id, names[kind],
                  writer->result->player == PG_EVEN ? "Even" : "Odd") < 0 ||
          !write_region(writer, extra[kind], "W") ||
          fprintf(writer->out, "\"];\n  n%zu -> n%zu [style=dashed];\n",
                  writer->root_id, id) < 0) {
        goto cleanup;
      }
    }
  }
  wrote = true;
cleanup:
  free(members);
  free(stream);
  ordered_tree_destroy(tree);
  return wrote;
}

bool pm_write_dot(FILE *out, PGGame const *game,
                  PMResult const result[static 2], PGSet const *unresolved,
                  size_t const round, bool const incremental,
                  ADDotPlayer const player, ADDotLabels const labels,
                  size_t const max_set_items, PGPriorityMap const *priority_map,
                  PGSet const shaved[static 2]) {
  bool const partial = !pg_set_empty(unresolved);
  if (fputs(
          "digraph progress_measures {\n  ordering=out;\n  node [shape=box];\n",
          out) < 0) {
    return false;
  }
  if (incremental && fprintf(out, "  label=\"round %zu\";\n", round) < 0) {
    return false;
  }
  if (player == AD_DOT_PLAYER_BOTH &&
      fprintf(out, "  result [label=\"result (synthetic%s)\"];\n",
              partial ? "; partial" : "") < 0) {
    return false;
  }
  TreeWriter writer = {.out = out,
                       .game = game,
                       .partial = partial,
                       .labels = labels,
                       .max_items = max_set_items,
                       .priority_map = priority_map};
  if (!pg_set_init(&writer.held, game->vertex_count)) {
    return false;
  }
  bool wrote = false;
  for (size_t candidate = 0; candidate < 2; candidate++) {
    if ((player == AD_DOT_PLAYER_EVEN && candidate != PG_EVEN) ||
        (player == AD_DOT_PLAYER_ODD && candidate != PG_ODD)) {
      continue;
    }
    writer.result = &result[candidate];
    writer.root_id = ++writer.next_id;
    writer.position = 0;
    if ((player == AD_DOT_PLAYER_BOTH &&
         fprintf(out, "  result -> n%zu;\n", writer.root_id) < 0) ||
        !write_player(&writer, &shaved[candidate])) {
      goto cleanup;
    }
  }
  if (partial && player == AD_DOT_PLAYER_BOTH &&
      (fputs("  unresolved [style=dashed, label=\"Unresolved", out) < 0 ||
       !write_region(&writer, unresolved, "U") ||
       fputs("\"];\n  result -> unresolved [style=dashed];\n", out) < 0)) {
    goto cleanup;
  }
  wrote = fputs("}\n", out) >= 0;
cleanup:
  pg_set_destroy(&writer.held);
  return wrote;
}

bool pm_write_json(FILE *out, PGGame const *game,
                   PMResult const result[static 2], PGSet const *unresolved,
                   size_t const round, ADDotPlayer const player,
                   PGPriorityMap const *priority_map,
                   PGSet const shaved[static 2]) {
  fprintf(out, "{\"round\":%zu,\"complete\":%s,\"unresolved\":", round,
          pg_set_empty(unresolved) ? "true" : "false");
  json_set(&(JSONWriter){out, game}, unresolved);
  fputs(",\"trees\":[", out);
  TreeWriter writer = {
      .out = out, .game = game, .json = true, .priority_map = priority_map};
  if (!pg_set_init(&writer.held, game->vertex_count)) {
    return false;
  }
  bool wrote = false;
  for (size_t candidate = 0; candidate < 2; candidate++) {
    if (candidate != 0) {
      fputc(',', out);
    }
    if ((player == AD_DOT_PLAYER_EVEN && candidate != PG_EVEN) ||
        (player == AD_DOT_PLAYER_ODD && candidate != PG_ODD)) {
      fputs("null", out);
      continue;
    }
    writer.result = &result[candidate];
    writer.position = 0;
    if (!write_player(&writer, &shaved[candidate])) {
      goto cleanup;
    }
  }
  wrote = fputs("]}", out) >= 0 && !ferror(out);
cleanup:
  pg_set_destroy(&writer.held);
  return wrote;
}
