#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_tree_dot.h"
#include "utils.h"

[[nodiscard]] static bool write_set(FILE *stream, PGGame const *game,
                                    PGSet const *set, size_t const max_items) {
  if (fputc('{', stream) == EOF) {
    return false;
  }
  size_t shown = 0;
  size_t const total = pg_set_count(set);
  for (size_t vertex = pg_set_next(set, 0);
       vertex != SIZE_MAX && shown < max_items;
       vertex = pg_set_next(set, vertex + 1)) {
    if ((shown != 0 && fputc(',', stream) == EOF) ||
        fprintf(stream, "%" PRIu64, game->vertices[vertex].external_id) < 0) {
      return false;
    }
    shown++;
  }
  if (shown < total && fprintf(stream, "%s..., +%zu more",
                               shown == 0 ? "" : ",", total - shown) < 0) {
    return false;
  }
  return fputc('}', stream) != EOF;
}

[[nodiscard]] static bool begin_html_table(FILE *out) {
  return fputs("<<TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"4\" "
               "CELLPADDING=\"1\">",
               out) >= 0;
}

[[nodiscard]] static bool end_html_table(FILE *out) {
  return fputs("</TABLE>>", out) >= 0;
}

typedef struct {
  bool below_zero;
  uint64_t level;
  char **identifiers;
  size_t identifier_count;
  size_t identifier_capacity;
} ADDotRank;

typedef struct {
  ADDotRank *ranks;
  size_t count;
  size_t capacity;
} ADDotRanks;

static void ranks_destroy(ADDotRanks *ranks) {
  if (ranks == nullptr) {
    return;
  }
  for (size_t rank = 0; rank < ranks->count; rank++) {
    for (size_t index = 0; index < ranks->ranks[rank].identifier_count;
         index++) {
      free(ranks->ranks[rank].identifiers[index]);
    }
    free((void *)ranks->ranks[rank].identifiers);
  }
  free(ranks->ranks);
  *ranks = (ADDotRanks){0};
}

[[nodiscard]] static bool ranks_add(ADDotRanks *ranks, bool const below_zero,
                                    uint64_t const level,
                                    char const *identifier) {
  size_t rank = 0;
  while (rank < ranks->count && (ranks->ranks[rank].below_zero != below_zero ||
                                 ranks->ranks[rank].level != level)) {
    rank++;
  }
  if (rank == ranks->count) {
    if (ranks->count == ranks->capacity) {
      ArrayGrowth const growth =
          grow_array(ranks->ranks, ranks->capacity, sizeof(ranks->ranks[0]));
      if (!growth.succeeded) {
        return false;
      }
      ranks->ranks = growth.data;
      ranks->capacity = growth.capacity;
    }
    ranks->ranks[rank] = (ADDotRank){.below_zero = below_zero, .level = level};
    ranks->count++;
  }

  ADDotRank *target = ranks->ranks + rank;
  if (target->identifier_count == target->identifier_capacity) {
    ArrayGrowth const growth =
        grow_array((void *)target->identifiers, target->identifier_capacity,
                   sizeof(target->identifiers[0]));
    if (!growth.succeeded) {
      return false;
    }
    target->identifiers = (char **)growth.data;
    target->identifier_capacity = growth.capacity;
  }
  size_t const length = strlen(identifier);
  char *copy = malloc(length + 1);
  if (copy == nullptr) {
    return false;
  }
  memcpy(copy, identifier, length + 1);
  target->identifiers[target->identifier_count++] = copy;
  return true;
}

[[nodiscard]] static int compare_ranks(void const *left, void const *right) {
  ADDotRank const *first = left;
  ADDotRank const *second = right;
  if (first->below_zero != second->below_zero) {
    return first->below_zero ? 1 : -1;
  }
  return first->level > second->level   ? -1
         : first->level < second->level ? 1
                                        : 0;
}

[[nodiscard]] static bool write_level(FILE *out, bool const below_zero,
                                      uint64_t const compact_level,
                                      PGPriorityMap const *priority_map) {
  if (below_zero) {
    return fputs("-1", out) >= 0;
  }
  if (fprintf(out, "%" PRIu64, compact_level) < 0) {
    return false;
  }
  if (priority_map == nullptr) {
    return true;
  }
  uint64_t source_bound = 0;
  if (!pg_priority_map_original_bound(priority_map, compact_level,
                                      &source_bound)) {
    return false;
  }
  return source_bound == compact_level ||
         fprintf(out,
                 "<BR/><FONT POINT-SIZE=\"9\">source bound = %" PRIu64
                 "</FONT>",
                 source_bound) >= 0;
}

[[nodiscard]] static bool write_rank_identifier(FILE *out,
                                                ADDotRank const *rank) {
  return rank->below_zero
             ? fputs("jurdzinski_level_m1", out) >= 0
             : fprintf(out, "jurdzinski_level_%" PRIu64, rank->level) >= 0;
}

[[nodiscard]] static bool
write_jurdzinski_ranks(FILE *out, ADDotRanks *ranks,
                       PGPriorityMap const *priority_map) {
  if (ranks->count > 1) {
    qsort(ranks->ranks, ranks->count, sizeof(ranks->ranks[0]), compare_ranks);
  }
  for (size_t rank = 0; rank < ranks->count; rank++) {
    ADDotRank const *current = ranks->ranks + rank;
    if (fputs("  ", out) < 0 || !write_rank_identifier(out, current) ||
        fputs(" [shape=plaintext, label=<<I>level</I> = ", out) < 0 ||
        !write_level(out, current->below_zero, current->level, priority_map) ||
        fputs(">];\n  { rank=same; ", out) < 0 ||
        !write_rank_identifier(out, current)) {
      return false;
    }
    for (size_t index = 0; index < current->identifier_count; index++) {
      if (fprintf(out, "; %s", current->identifiers[index]) < 0) {
        return false;
      }
    }
    if (fputs("; }\n", out) < 0) {
      return false;
    }
  }
  for (size_t rank = 1; rank < ranks->count; rank++) {
    if (fputs("  ", out) < 0 ||
        !write_rank_identifier(out, ranks->ranks + rank - 1) ||
        fputs(" -> ", out) < 0 ||
        !write_rank_identifier(out, ranks->ranks + rank) ||
        fputs(" [style=invis, weight=100];\n", out) < 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] static bool
write_classic_node_label(FILE *out, PGGame const *game, PGSet const *domain,
                         ADNode const *node, ADDotLabels const labels,
                         size_t const max_items, bool const show_tree_metrics,
                         PGPriorityMap const *priority_map) {
  char const *name = node->player == PG_EVEN ? "Even" : "Odd";
  uint64_t displayed_bound = node->priority_bound;
  if (priority_map != nullptr &&
      !pg_priority_map_original_bound(priority_map, node->priority_bound,
                                      &displayed_bound)) {
    return false;
  }
  bool succeeded = begin_html_table(out) &&
                   fprintf(out,
                           "<TR><TD COLSPAN=\"2\"><B>%s</B> <I>d</I> = %" PRIu64
                           "</TD></TR>",
                           name, displayed_bound) >= 0;
  if (succeeded && labels != AD_DOT_LABEL_NONE) {
    succeeded =
        fprintf(out,
                "<TR><TD>|<I>W</I>| = %zu</TD>"
                "<TD>|<I>A</I>| = %zu</TD></TR>",
                pg_set_count(domain), pg_set_count(&node->top_attractor)) >= 0;
    if (succeeded && show_tree_metrics) {
      ADTreeMetrics const metrics = ad_tree_metrics(node);
      succeeded =
          fprintf(out,
                  "<TR><TD>nodes = %zu</TD><TD>leaves = %zu</TD></TR>"
                  "<TR><TD>height = %zu</TD><TD>Strahler = %zu</TD></TR>",
                  metrics.nodes, metrics.leaves, metrics.height,
                  metrics.strahler) >= 0;
    }
  }
  if (succeeded && labels == AD_DOT_LABEL_SETS) {
    succeeded =
        fputs("<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>W</I> = ", out) >= 0 &&
        write_set(out, game, domain, max_items) &&
        fputs("</TD></TR>"
              "<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>A</I> = ",
              out) >= 0 &&
        write_set(out, game, &node->top_attractor, max_items) &&
        fputs("</TD></TR>", out) >= 0;
  }
  return succeeded && end_html_table(out);
}

[[nodiscard]] static bool write_tree_relative_node_label(
    FILE *out, PGGame const *game, PGSet const *outer, PGSet const *core,
    ADNode const *node, ADDotLabels const labels, size_t const max_items,
    bool const show_tree_metrics, PGPriorityMap const *priority_map) {
  char const *name = node->player == PG_EVEN ? "Even" : "Odd";
  uint64_t displayed_bound = node->priority_bound;
  if (priority_map != nullptr &&
      !pg_priority_map_original_bound(priority_map, node->priority_bound,
                                      &displayed_bound)) {
    return false;
  }
  bool succeeded = begin_html_table(out) &&
                   fprintf(out,
                           "<TR><TD COLSPAN=\"2\"><B>%s</B> <I>d</I> = %" PRIu64
                           "</TD></TR>",
                           name, displayed_bound) >= 0;
  ADTreeRelativeParts parts = {0};
  if (succeeded && labels != AD_DOT_LABEL_NONE) {
    if (!ad_tree_relative_parts(game, outer, core, node, &parts)) {
      return false;
    }
    succeeded =
        fprintf(out,
                "<TR><TD>|<I>V</I>| = %zu</TD>"
                "<TD>|<I>H</I>| = %zu</TD></TR>"
                "<TR><TD>|<I>T</I>| = %zu</TD>"
                "<TD>|<I>S</I>| = %zu</TD></TR>",
                pg_set_count(outer), pg_set_count(&parts.highest),
                pg_set_count(&parts.top), pg_set_count(&parts.side)) >= 0;
    if (succeeded && show_tree_metrics) {
      ADTreeMetrics const metrics = ad_tree_metrics(node);
      succeeded =
          fprintf(out,
                  "<TR><TD>nodes = %zu</TD><TD>leaves = %zu</TD></TR>"
                  "<TR><TD>height = %zu</TD><TD>Strahler = %zu</TD></TR>",
                  metrics.nodes, metrics.leaves, metrics.height,
                  metrics.strahler) >= 0;
    }
  }
  if (succeeded && labels == AD_DOT_LABEL_SETS) {
    succeeded =
        fputs("<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>V</I> = ", out) >= 0 &&
        write_set(out, game, outer, max_items) &&
        fputs("</TD></TR>"
              "<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>H</I> = ",
              out) >= 0 &&
        write_set(out, game, &parts.highest, max_items) &&
        fputs("</TD></TR>"
              "<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>T</I> = ",
              out) >= 0 &&
        write_set(out, game, &parts.top, max_items) &&
        fputs("</TD></TR>"
              "<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>S</I> = ",
              out) >= 0 &&
        write_set(out, game, &parts.side, max_items) &&
        fputs("</TD></TR>", out) >= 0;
  }
  ad_tree_relative_parts_destroy(&parts);
  return succeeded && end_html_table(out);
}

[[nodiscard]] static bool
write_classic_edge_label(FILE *out, PGGame const *game, ADChild const *child,
                         size_t const child_index, ADDotLabels const labels,
                         size_t const max_items) {
  bool succeeded = begin_html_table(out) &&
                   fprintf(out,
                           "<TR><TD COLSPAN=\"2\"><I>i</I> = %zu</TD></TR>"
                           "<TR><TD>|<I>S</I><SUB><I>i</I></SUB>| = %zu</TD>"
                           "<TD>|<I>A</I><SUB><I>i</I></SUB>| = %zu</TD></TR>",
                           child_index + 1, pg_set_count(&child->trap),
                           pg_set_count(&child->attractor)) >= 0;
  if (succeeded && labels == AD_DOT_LABEL_SETS) {
    succeeded =
        fputs("<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>S</I><SUB><I>i</I>"
              "</SUB> = ",
              out) >= 0 &&
        write_set(out, game, &child->trap, max_items) &&
        fputs("</TD></TR>"
              "<TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><I>A</I><SUB><I>i</I>"
              "</SUB> = ",
              out) >= 0 &&
        write_set(out, game, &child->attractor, max_items) &&
        fputs("</TD></TR>", out) >= 0;
  }
  return succeeded && end_html_table(out);
}

[[nodiscard]] static bool
write_tree_relative_edge_label(FILE *out, PGGame const *game,
                               ADChild const *child, size_t const child_index,
                               ADDotLabels const labels,
                               size_t const max_items) {
  bool succeeded =
      begin_html_table(out) &&
      fprintf(out,
              "<TR><TD><I>i</I> = %zu</TD></TR>"
              "<TR><TD>|<I>R</I><SUB><I>i</I></SUB>| = %zu"
              "</TD></TR>",
              child_index + 1, pg_set_count(&child->attractor)) >= 0;
  if (succeeded && labels == AD_DOT_LABEL_SETS) {
    succeeded = fputs("<TR><TD ALIGN=\"LEFT\"><I>R</I><SUB><I>i</I></SUB> = ",
                      out) >= 0 &&
                write_set(out, game, &child->attractor, max_items) &&
                fputs("</TD></TR>", out) >= 0;
  }
  return succeeded && end_html_table(out);
}

[[nodiscard]] static char *child_identifier(char const *parent,
                                            size_t const index) {
  int const needed = snprintf(nullptr, 0, "%s_%zu", parent, index);
  if (needed < 0 || (size_t)needed == SIZE_MAX) {
    return nullptr;
  }
  char *identifier = malloc((size_t)needed + 1);
  if (identifier != nullptr) {
    (void)snprintf(identifier, (size_t)needed + 1, "%s_%zu", parent, index);
  }
  return identifier;
}

typedef enum {
  JURDZINSKI_PART_HIGHEST,
  JURDZINSKI_PART_TOP,
  JURDZINSKI_PART_SIDE,
} JurdzinskiPart;

[[nodiscard]] static bool write_jurdzinski_part_name(FILE *out,
                                                     JurdzinskiPart const part,
                                                     size_t const child_index) {
  if (part == JURDZINSKI_PART_HIGHEST) {
    return fputs("<I>H</I>", out) >= 0;
  }
  if (part == JURDZINSKI_PART_TOP) {
    return fputs("<I>T</I>", out) >= 0;
  }
  return fprintf(out, "<I>S</I><SUB>%zu</SUB>", child_index + 1) >= 0;
}

[[nodiscard]] static bool
write_jurdzinski_part_label(FILE *out, PGGame const *game, PGSet const *set,
                            JurdzinskiPart const part, size_t const child_index,
                            ADDotLabels const labels, size_t const max_items,
                            ADNode const *metrics_root, PGPlayer const player) {
  if (fputc('<', out) == EOF) {
    return false;
  }
  if (metrics_root != nullptr) {
    char const *name = player == PG_EVEN ? "Even" : "Odd";
    if (fprintf(out, "<B>%s</B><BR/>", name) < 0) {
      return false;
    }
  }
  if (!write_jurdzinski_part_name(out, part, child_index)) {
    return false;
  }
  if (labels == AD_DOT_LABEL_COUNTS &&
      fprintf(out, ": %zu %s", pg_set_count(set),
              pg_set_count(set) == 1 ? "vertex" : "vertices") < 0) {
    return false;
  }
  if (labels == AD_DOT_LABEL_SETS &&
      (fputs(" = ", out) < 0 || !write_set(out, game, set, max_items))) {
    return false;
  }
  if (metrics_root != nullptr && labels != AD_DOT_LABEL_NONE) {
    ADTreeMetrics const metrics = ad_tree_metrics(metrics_root);
    if (fprintf(out,
                "<BR/><FONT POINT-SIZE=\"10\">base tree: %zu %s; "
                "%zu %s; height %zu; Strahler %zu</FONT>",
                metrics.nodes, metrics.nodes == 1 ? "node" : "nodes",
                metrics.leaves, metrics.leaves == 1 ? "leaf" : "leaves",
                metrics.height, metrics.strahler) < 0) {
      return false;
    }
  }
  return fputc('>', out) != EOF;
}

[[nodiscard]] static char *top_leaf_identifier(char const *parent) {
  int const needed = snprintf(nullptr, 0, "%s_mininf", parent);
  if (needed < 0 || (size_t)needed == SIZE_MAX) {
    return nullptr;
  }
  char *identifier = malloc((size_t)needed + 1);
  if (identifier != nullptr) {
    (void)snprintf(identifier, (size_t)needed + 1, "%s_mininf", parent);
  }
  return identifier;
}

[[nodiscard]] static char *side_leaf_identifier(char const *parent,
                                                size_t const index) {
  int const needed = snprintf(nullptr, 0, "%s_%zu_plus", parent, index);
  if (needed < 0 || (size_t)needed == SIZE_MAX) {
    return nullptr;
  }
  char *identifier = malloc((size_t)needed + 1);
  if (identifier != nullptr) {
    (void)snprintf(identifier, (size_t)needed + 1, "%s_%zu_plus", parent,
                   index);
  }
  return identifier;
}

[[nodiscard]] static bool write_jurdzinski_node(
    FILE *out, PGGame const *game, PGSet const *outer, PGSet const *core,
    ADNode const *node, char const *identifier, ADDotLabels const labels,
    size_t const max_items, bool const show_tree_metrics, ADDotRanks *ranks) {
  ADTreeRelativeParts parts = {0};
  if (!ad_tree_relative_parts(game, outer, core, node, &parts) ||
      fprintf(out, "  %s [label=", identifier) < 0 ||
      !write_jurdzinski_part_label(
          out, game, &parts.highest, JURDZINSKI_PART_HIGHEST, 0, labels,
          max_items, show_tree_metrics ? node : nullptr, node->player) ||
      fputs("];\n", out) < 0 ||
      !ranks_add(ranks, false, node->priority_bound, identifier)) {
    ad_tree_relative_parts_destroy(&parts);
    return false;
  }

  bool const leaf_below_zero = node->priority_bound == 0;
  uint64_t const leaf_level = leaf_below_zero ? 0 : node->priority_bound - 1;
  if (!pg_set_empty(&parts.top)) {
    char *top_id = top_leaf_identifier(identifier);
    bool const succeeded =
        top_id != nullptr &&
        (labels == AD_DOT_LABEL_NONE
             ? fprintf(out, "  %s -> %s;\n", identifier, top_id) >= 0
             : fprintf(out, "  %s -> %s [label=<&minus;&infin;>];\n",
                       identifier, top_id) >= 0) &&
        fprintf(out, "  %s [shape=ellipse, label=", top_id) >= 0 &&
        write_jurdzinski_part_label(out, game, &parts.top, JURDZINSKI_PART_TOP,
                                    0, labels, max_items, nullptr,
                                    node->player) &&
        fputs("];\n", out) >= 0 &&
        ranks_add(ranks, leaf_below_zero, leaf_level, top_id);
    free(top_id);
    if (!succeeded) {
      ad_tree_relative_parts_destroy(&parts);
      return false;
    }
  }

  for (size_t index = 0; index < node->child_count; index++) {
    ADChild const *child = node->children + index;
    char *child_id = child_identifier(identifier, index);
    char *side_id = nullptr;
    ADTreeRelativeParts child_parts = {0};
    bool succeeded =
        child_id != nullptr &&
        ad_tree_relative_parts(game, &child->attractor, &child->trap,
                               child->subtree, &child_parts) &&
        (labels == AD_DOT_LABEL_NONE
             ? fprintf(out, "  %s -> %s [minlen=2];\n", identifier, child_id) >=
                   0
             : fprintf(out, "  %s -> %s [label=<%zu>, minlen=2];\n", identifier,
                       child_id, index + 1) >= 0);
    if (succeeded && !pg_set_empty(&child_parts.side)) {
      side_id = side_leaf_identifier(identifier, index);
      succeeded = side_id != nullptr &&
                  (labels == AD_DOT_LABEL_NONE
                       ? fprintf(out, "  %s -> %s;\n", identifier, side_id) >= 0
                       : fprintf(out, "  %s -> %s [label=<%zu<SUP>+</SUP>>];\n",
                                 identifier, side_id, index + 1) >= 0) &&
                  fprintf(out, "  %s [shape=ellipse, label=", side_id) >= 0 &&
                  write_jurdzinski_part_label(
                      out, game, &child_parts.side, JURDZINSKI_PART_SIDE, index,
                      labels, max_items, nullptr, node->player) &&
                  fputs("];\n", out) >= 0 &&
                  ranks_add(ranks, leaf_below_zero, leaf_level, side_id);
    }
    ad_tree_relative_parts_destroy(&child_parts);
    free(child_id);
    free(side_id);
    if (!succeeded) {
      ad_tree_relative_parts_destroy(&parts);
      return false;
    }
  }

  for (size_t index = 0; index < node->child_count; index++) {
    ADChild const *child = node->children + index;
    char *child_id = child_identifier(identifier, index);
    bool const succeeded =
        child_id != nullptr &&
        write_jurdzinski_node(out, game, &child->attractor, &child->trap,
                              child->subtree, child_id, labels, max_items,
                              false, ranks);
    free(child_id);
    if (!succeeded) {
      ad_tree_relative_parts_destroy(&parts);
      return false;
    }
  }

  ad_tree_relative_parts_destroy(&parts);
  return true;
}

[[nodiscard]] static bool
write_node(FILE *out, PGGame const *game, PGSet const *domain,
           PGSet const *core, ADNode const *node, char const *identifier,
           ADDotView const view, ADDotLabels const labels,
           size_t const max_items, bool const show_tree_metrics,
           PGPriorityMap const *priority_map) {
  if (fprintf(out, "  %s [label=", identifier) < 0 ||
      !(view == AD_DOT_VIEW_CLASSIC
            ? write_classic_node_label(out, game, core, node, labels, max_items,
                                       show_tree_metrics, priority_map)
            : write_tree_relative_node_label(
                  out, game, domain, core, node, labels, max_items,
                  show_tree_metrics, priority_map)) ||
      fputs("];\n", out) < 0) {
    return false;
  }

  for (size_t index = 0; index < node->child_count; index++) {
    ADChild const *child = node->children + index;
    char *child_id = child_identifier(identifier, index);
    if (child_id == nullptr ||
        fprintf(out, "  %s -> %s", identifier, child_id) < 0 ||
        (labels != AD_DOT_LABEL_NONE &&
         (fputs(" [label=", out) < 0 ||
          !(view == AD_DOT_VIEW_CLASSIC
                ? write_classic_edge_label(out, game, child, index, labels,
                                           max_items)
                : write_tree_relative_edge_label(out, game, child, index,
                                                 labels, max_items)) ||
          fputc(']', out) == EOF)) ||
        fputs(";\n", out) < 0 ||
        !write_node(out, game,
                    view == AD_DOT_VIEW_CLASSIC ? &child->trap
                                                : &child->attractor,
                    &child->trap, child->subtree, child_id, view, labels,
                    max_items, false, priority_map)) {
      free(child_id);
      return false;
    }
    free(child_id);
  }
  return true;
}

[[nodiscard]] static bool write_empty(FILE *out, PGPlayer const player,
                                      bool const attach) {
  char const *name = player == PG_EVEN ? "Even" : "Odd";
  char const *identifier = player == PG_EVEN ? "even_empty" : "odd_empty";
  if (fprintf(out,
              "  %s [label=<<B>%s</B>: <I>W</I> = empty; no "
              "decomposition>];\n",
              identifier, name) < 0) {
    return false;
  }
  return !attach || fprintf(out, "  result -> %s;\n", identifier) >= 0;
}

[[nodiscard]] static bool
write_jurdzinski_dot(FILE *out, PGGame const *game,
                     ZielonkaResult const *result, ADDotPlayer const player,
                     ADDotLabels const labels, size_t const max_set_items,
                     PGPriorityMap const *priority_map) {
  if (fputs("digraph attractor_decompositions {\n"
            "  graph [rankdir=TB, ordering=out, newrank=true];\n"
            "  node [shape=box];\n",
            out) < 0) {
    return false;
  }

  ADDotRanks ranks = {0};
  bool succeeded = true;
  if (player == AD_DOT_PLAYER_BOTH) {
    succeeded = fputs("  result [label=\"result (synthetic)\"];\n", out) >= 0;
    for (size_t candidate = 0; succeeded && candidate < 2; candidate++) {
      char const *identifier = candidate == PG_EVEN ? "even" : "odd";
      if (result->decomposition[candidate] == nullptr) {
        succeeded = write_empty(out, (PGPlayer)candidate, true);
      } else {
        succeeded =
            fprintf(out, "  result -> %s;\n", identifier) >= 0 &&
            write_jurdzinski_node(out, game, &result->winning[candidate],
                                  &result->winning[candidate],
                                  result->decomposition[candidate], identifier,
                                  labels, max_set_items, true, &ranks);
      }
    }
  } else {
    PGPlayer const selected = player == AD_DOT_PLAYER_EVEN ? PG_EVEN : PG_ODD;
    char const *identifier = selected == PG_EVEN ? "even" : "odd";
    succeeded =
        result->decomposition[selected] == nullptr
            ? write_empty(out, selected, false)
            : write_jurdzinski_node(out, game, &result->winning[selected],
                                    &result->winning[selected],
                                    result->decomposition[selected], identifier,
                                    labels, max_set_items, true, &ranks);
  }
  if (succeeded) {
    succeeded = write_jurdzinski_ranks(out, &ranks, priority_map);
  }
  ranks_destroy(&ranks);
  return succeeded && fputs("}\n", out) >= 0;
}

bool ad_tree_write_dot(FILE *out, PGGame const *game,
                       ZielonkaResult const *result, ADDotPlayer const player,
                       ADDotView const view, ADDotLabels const labels,
                       size_t const max_set_items,
                       PGPriorityMap const *priority_map) {
  if (out == nullptr || game == nullptr || result == nullptr ||
      player > AD_DOT_PLAYER_ODD || view > AD_DOT_VIEW_JURDZINSKI ||
      labels > AD_DOT_LABEL_NONE) {
    return false;
  }
  if (view == AD_DOT_VIEW_JURDZINSKI) {
    return write_jurdzinski_dot(out, game, result, player, labels,
                                max_set_items, priority_map);
  }
  if (fputs("digraph attractor_decompositions {\n"
            "  graph [rankdir=TB, ordering=out];\n"
            "  node [shape=box];\n",
            out) < 0) {
    return false;
  }

  bool succeeded = true;
  if (player == AD_DOT_PLAYER_BOTH) {
    succeeded = fputs("  result [label=\"result (synthetic)\"];\n", out) >= 0;
    for (size_t candidate = 0; succeeded && candidate < 2; candidate++) {
      char const *identifier = candidate == PG_EVEN ? "even" : "odd";
      if (result->decomposition[candidate] == nullptr) {
        succeeded = write_empty(out, (PGPlayer)candidate, true);
      } else {
        succeeded = fprintf(out, "  result -> %s;\n", identifier) >= 0 &&
                    write_node(out, game, &result->winning[candidate],
                               &result->winning[candidate],
                               result->decomposition[candidate], identifier,
                               view, labels, max_set_items, true, priority_map);
      }
    }
  } else {
    PGPlayer const selected = player == AD_DOT_PLAYER_EVEN ? PG_EVEN : PG_ODD;
    char const *identifier = selected == PG_EVEN ? "even" : "odd";
    succeeded =
        result->decomposition[selected] == nullptr
            ? write_empty(out, selected, false)
            : write_node(out, game, &result->winning[selected],
                         &result->winning[selected],
                         result->decomposition[selected], identifier, view,
                         labels, max_set_items, true, priority_map);
  }
  return succeeded && fputs("}\n", out) >= 0;
}
