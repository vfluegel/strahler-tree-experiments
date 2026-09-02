#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_tree_dot.h"
#include "dot_utils.h"

[[nodiscard]] static char *finish_label(FILE *stream, char **buffer,
                                        size_t *length) {
  bool const flushed = fflush(stream) == 0;
  bool const closed = fclose(stream) == 0;
  if (!flushed || !closed) {
    free(*buffer);
    *buffer = nullptr;
    return nullptr;
  }
  (void)length;
  return *buffer;
}

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

[[nodiscard]] static char *node_label(PGGame const *game, PGSet const *domain,
                                      ADNode const *node,
                                      ADDotLabels const labels,
                                      size_t const max_items) {
  char *buffer = nullptr;
  size_t length = 0;
  FILE *stream = open_memstream(&buffer, &length);
  if (stream == nullptr) {
    return nullptr;
  }
  char const *name = node->player == PG_EVEN ? "Even" : "Odd";
  bool succeeded =
      fprintf(stream, "%s d=%" PRIu64, name, node->priority_bound) >= 0;
  if (succeeded && labels != AD_DOT_LABEL_NONE) {
    ADTreeMetrics const metrics = ad_tree_metrics(node);
    succeeded =
        fprintf(stream,
                "\n|W|=%zu |A|=%zu\nchildren=%zu\n"
                "nodes=%zu leaves=%zu height=%zu Strahler=%zu",
                pg_set_count(domain), pg_set_count(&node->top_attractor),
                node->child_count, metrics.nodes, metrics.leaves,
                metrics.height, metrics.strahler) >= 0;
  }
  if (succeeded && labels == AD_DOT_LABEL_SETS) {
    succeeded = fputs("\nW=", stream) >= 0 &&
                write_set(stream, game, domain, max_items) &&
                fputs("\nA=", stream) >= 0 &&
                write_set(stream, game, &node->top_attractor, max_items);
  }
  if (!succeeded) {
    (void)fclose(stream);
    free(buffer);
    return nullptr;
  }
  return finish_label(stream, &buffer, &length);
}

[[nodiscard]] static char *edge_label(PGGame const *game, ADChild const *child,
                                      size_t const child_index,
                                      ADDotLabels const labels,
                                      size_t const max_items) {
  char *buffer = nullptr;
  size_t length = 0;
  FILE *stream = open_memstream(&buffer, &length);
  if (stream == nullptr) {
    return nullptr;
  }
  bool succeeded = true;
  if (labels != AD_DOT_LABEL_NONE) {
    succeeded = fprintf(stream, "i=%zu |S_i|=%zu |A_i|=%zu", child_index + 1,
                        pg_set_count(&child->trap),
                        pg_set_count(&child->attractor)) >= 0;
  }
  if (succeeded && labels == AD_DOT_LABEL_SETS) {
    succeeded = fputs("\nS_i=", stream) >= 0 &&
                write_set(stream, game, &child->trap, max_items) &&
                fputs("\nA_i=", stream) >= 0 &&
                write_set(stream, game, &child->attractor, max_items);
  }
  if (!succeeded) {
    (void)fclose(stream);
    free(buffer);
    return nullptr;
  }
  return finish_label(stream, &buffer, &length);
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

[[nodiscard]] static bool write_node(FILE *out, PGGame const *game,
                                     PGSet const *domain, ADNode const *node,
                                     char const *identifier,
                                     ADDotLabels const labels,
                                     size_t const max_items) {
  char *label = node_label(game, domain, node, labels, max_items);
  if (label == nullptr || fprintf(out, "  %s [label=", identifier) < 0 ||
      !dot_write_quoted(out, label) || fputs("];\n", out) < 0) {
    free(label);
    return false;
  }
  free(label);

  for (size_t index = 0; index < node->child_count; index++) {
    ADChild const *child = node->children + index;
    char *child_id = child_identifier(identifier, index);
    char *label_text = edge_label(game, child, index, labels, max_items);
    if (child_id == nullptr || label_text == nullptr ||
        fprintf(out, "  %s -> %s", identifier, child_id) < 0 ||
        (labels != AD_DOT_LABEL_NONE &&
         (fputs(" [label=", out) < 0 || !dot_write_quoted(out, label_text) ||
          fputc(']', out) == EOF)) ||
        fputs(";\n", out) < 0 ||
        !write_node(out, game, &child->trap, child->subtree, child_id, labels,
                    max_items)) {
      free(child_id);
      free(label_text);
      return false;
    }
    free(child_id);
    free(label_text);
  }
  return true;
}

[[nodiscard]] static bool write_empty(FILE *out, PGPlayer const player,
                                      bool const attach) {
  char const *name = player == PG_EVEN ? "Even" : "Odd";
  char const *identifier = player == PG_EVEN ? "even_empty" : "odd_empty";
  if (fprintf(out, "  %s [label=\"%s: W=empty; no decomposition\"];\n",
              identifier, name) < 0) {
    return false;
  }
  return !attach || fprintf(out, "  result -> %s;\n", identifier) >= 0;
}

bool ad_tree_write_dot(FILE *out, PGGame const *game,
                       ZielonkaResult const *result, ADDotPlayer const player,
                       ADDotLabels const labels, size_t const max_set_items) {
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
                               labels, max_set_items);
      }
    }
  } else {
    PGPlayer const selected = player == AD_DOT_PLAYER_EVEN ? PG_EVEN : PG_ODD;
    char const *identifier = selected == PG_EVEN ? "even" : "odd";
    succeeded = result->decomposition[selected] == nullptr
                    ? write_empty(out, selected, false)
                    : write_node(out, game, &result->winning[selected],
                                 result->decomposition[selected], identifier,
                                 labels, max_set_items);
  }
  return succeeded && fputs("}\n", out) >= 0;
}
