#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "ad_tree_dot.h"

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

[[nodiscard]] static bool
write_node_label(FILE *out, PGGame const *game, PGSet const *domain,
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

[[nodiscard]] static bool write_edge_label(FILE *out, PGGame const *game,
                                           ADChild const *child,
                                           size_t const child_index,
                                           ADDotLabels const labels,
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

[[nodiscard]] static bool
write_node(FILE *out, PGGame const *game, PGSet const *domain,
           ADNode const *node, char const *identifier, ADDotLabels const labels,
           size_t const max_items, bool const show_tree_metrics,
           PGPriorityMap const *priority_map) {
  if (fprintf(out, "  %s [label=", identifier) < 0 ||
      !write_node_label(out, game, domain, node, labels, max_items,
                        show_tree_metrics, priority_map) ||
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
          !write_edge_label(out, game, child, index, labels, max_items) ||
          fputc(']', out) == EOF)) ||
        fputs(";\n", out) < 0 ||
        !write_node(out, game, &child->trap, child->subtree, child_id, labels,
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

bool ad_tree_write_dot(FILE *out, PGGame const *game,
                       ZielonkaResult const *result, ADDotPlayer const player,
                       ADDotLabels const labels, size_t const max_set_items,
                       PGPriorityMap const *priority_map) {
  if (out == nullptr || game == nullptr || result == nullptr ||
      player > AD_DOT_PLAYER_ODD || labels > AD_DOT_LABEL_NONE ||
      fputs("digraph attractor_decompositions {\n"
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
                               result->decomposition[candidate], identifier,
                               labels, max_set_items, true, priority_map);
      }
    }
  } else {
    PGPlayer const selected = player == AD_DOT_PLAYER_EVEN ? PG_EVEN : PG_ODD;
    char const *identifier = selected == PG_EVEN ? "even" : "odd";
    succeeded = result->decomposition[selected] == nullptr
                    ? write_empty(out, selected, false)
                    : write_node(out, game, &result->winning[selected],
                                 result->decomposition[selected], identifier,
                                 labels, max_set_items, true, priority_map);
  }
  return succeeded && fputs("}\n", out) >= 0;
}
