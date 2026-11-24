#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "ad_trace.h"
#include "ad_tree_json.h"
#include "json_utils.h"

void ad_trace_emit(ADTrace *trace, ADTraceEvent event) {
  if (trace->failed) {
    return;
  }
  event.step = ++trace->step;
  if (trace->callback == nullptr || !trace->callback(trace->context, &event)) {
    trace->failed = true;
  }
}

ADTracePath ad_trace_host_path(ADLiftTree const *host, size_t node,
                               size_t storage[AD_PATH_CAPACITY]) {
  size_t length = 0;
  while (node != 0 && node < host->node_count && length < AD_PATH_CAPACITY) {
    size_t const parent = host->nodes[node].parent;
    size_t index = 0;
    for (size_t sibling = host->nodes[parent].first_child; sibling != node;
         sibling = host->nodes[sibling].next_sibling) {
      index++;
    }
    storage[length++] = index;
    node = parent;
  }
  for (size_t index = 0; index < length / 2; index++) {
    size_t const value = storage[index];
    storage[index] = storage[length - index - 1];
    storage[length - index - 1] = value;
  }
  return (ADTracePath){storage, length};
}

void ad_trace_label(ADTrace *trace, ADLiftTree const *host, size_t const vertex,
                    size_t const node, ADPositionKind const kind,
                    ADTraceKind const event, size_t const from,
                    size_t const to) {
  size_t storage[AD_PATH_CAPACITY];
  ADTracePath const path = ad_trace_host_path(host, node, storage);
  ad_trace_emit(trace, (ADTraceEvent){.kind = event,
                                      .player = host->player,
                                      .path = path,
                                      .h = host->nodes[node].level,
                                      .part = kind == AD_POS_REGULAR     ? 'H'
                                              : kind == AD_POS_MINUS_INF ? 'T'
                                                                         : 'S',
                                      .vertex = vertex,
                                      .from = from,
                                      .to = to});
}

void ad_trace_tree(ADTrace *trace, PGGame const *game, PGSet const *outer,
                   PGSet const *core, ADNode const *root, ADTracePath path) {
  if (root == nullptr || trace->failed) {
    return;
  }
  ADTreeRelativeParts parts = {0};
  if (!ad_tree_relative_parts(game, outer, core, root, &parts)) {
    trace->failed = true;
    return;
  }
  /* An unfinished node has only the H vertices actually stored in its top. */
  pg_set_intersect_into(&parts.highest, &root->top_attractor);
  PGSet const *sets[] = {&parts.highest, &parts.top, &parts.side};
  for (size_t index = 0; index < 3; index++) {
    ad_trace_emit(trace, (ADTraceEvent){.kind = AD_TRACE_SET,
                                        .player = root->player,
                                        .path = path,
                                        .h = root->priority_bound,
                                        .part = "HTS"[index],
                                        .vertices = sets[index],
                                        .children = root->child_count});
  }
  ad_tree_relative_parts_destroy(&parts);
  size_t storage[AD_PATH_CAPACITY];
  if (path.length >= AD_PATH_CAPACITY) {
    trace->failed = true;
    return;
  }
  if (path.length != 0) {
    memcpy(storage, path.indices, path.length * sizeof(storage[0]));
  }
  for (size_t index = 0; index < root->child_count; index++) {
    ADChild const *child = root->children + index;
    storage[path.length] = index;
    ad_trace_tree(trace, game, &child->attractor, &child->trap, child->subtree,
                  (ADTracePath){storage, path.length + 1});
  }
}

static void path(FILE *stream, ADTracePath const value) {
  fputc('[', stream);
  for (size_t index = 0; index < value.length; index++) {
    fprintf(stream, "%s%zu", index == 0 ? "" : ",", value.indices[index]);
  }
  fputc(']', stream);
}

/* Walk native sibling links once: deriving each path independently by
 * searching siblings would be quadratic for a broad host. */
static void host_nodes(FILE *stream, ADLiftTree const *host, size_t index,
                        size_t storage[AD_PATH_CAPACITY], size_t depth,
                        bool implicit, bool *first) {
  ADLiftNode const *node = host->nodes + index;
  fprintf(stream, "%s{\"path\":", *first ? "" : ",");
  *first = false;
  path(stream, (ADTracePath){storage, depth});
  fprintf(stream, ",\"h\":%" PRIu64, node->level);
  if (!implicit) {
    fprintf(stream, ",\"positions\":[%zu,%zu,%zu]", node->regular,
            node->minus_inf, node->after);
  }
  fputc('}', stream);
  size_t child_index = 0;
  for (size_t child = node->first_child; child != SIZE_MAX;
       child = host->nodes[child].next_sibling) {
    storage[depth] = child_index++;
    host_nodes(stream, host, child, storage, depth + 1, implicit, first);
  }
}

/* Serialized only by a trace writer, once per host epoch. Native ordinals
 * are recorded rather than inferred from a pruned decomposition. */
static void host_description(FILE *stream, ADTraceEvent const *event) {
  ADLiftTree const *host = event->host;
  fprintf(stream, ",\"host\":{\"schema\":\"strahler-host/1\",\"complete\":%s,"
                  "\"parameters\":{", event->host_implicit ? "false" : "true");
  if (event->host_k != 0) {
    fprintf(stream, "\"k\":%zu,\"t\":%zu,\"h\":%zu", event->host_k,
            event->host_t, host->height + 1);
  } else if (event->host_arity != 0) {
    fprintf(stream, "\"arity\":%zu", event->host_arity);
  }
  fputs("},\"nodes\":[", stream);
  bool first = true;
  size_t storage[AD_PATH_CAPACITY];
  host_nodes(stream, host, 0, storage, 0, event->host_implicit, &first);
  fputs("],\"active\":", stream);
  json_set(&(JSONWriter){stream, event->priority_game}, event->subgame);
  fputs(",\"priorities\":[", stream);
  first = true;
  for (size_t vertex = pg_set_next(event->subgame, 0); vertex != SIZE_MAX;
       vertex = pg_set_next(event->subgame, vertex + 1)) {
    PGVertex const *v = event->priority_game->vertices + vertex;
    fprintf(stream, "%s[%" PRIu64 ",%" PRIu64 "]", first ? "" : ",",
            v->external_id, v->priority);
    first = false;
  }
  fputs("]}", stream);
}

static bool deliver_line(ADTraceJSON *writer, ADTraceJSON *copy,
                         char *const *line, size_t const *length,
                         bool written) {
  bool const closed = fclose(copy->stream) == 0;
  writer->truncated = copy->truncated;
  bool const delivered = closed && (written || copy->truncated) &&
                         writer->callback(writer->context, *line, *length);
  free(*line);
  return written && delivered;
}

bool ad_trace_json_header(ADTraceJSON *writer, char const *version,
                          char const *algorithm, int const argc,
                          char *const argv[]) {
  if (writer->callback != nullptr) {
    char *line = nullptr;
    size_t length = 0;
    ADTraceJSON copy = *writer;
    copy.callback = nullptr;
    copy.stream = open_memstream(&line, &length);
    if (copy.stream == nullptr) {
      return false;
    }
    bool const written =
        ad_trace_json_header(&copy, version, algorithm, argc, argv);
    // Flush before reading open_memstream's buffer and length.
    bool const flushed = fflush(copy.stream) == 0;
    return deliver_line(writer, &copy, &line, &length, written && flushed);
  }
  FILE *stream = writer->stream;
  fputs("{\"event\":\"header\",", stream);
  json_header(stream, "strahler-trace/1", version, algorithm, argc, argv);
  fputs("}\n", stream);
  return !ferror(stream);
}

bool ad_trace_json_incomplete(ADTraceJSON *writer, size_t const step,
                              char const *reason, int const status) {
  if (writer->callback != nullptr) {
    char *line = nullptr;
    size_t length = 0;
    ADTraceJSON copy = *writer;
    copy.callback = nullptr;
    copy.stream = open_memstream(&line, &length);
    if (copy.stream == nullptr) {
      return false;
    }
    bool const written = ad_trace_json_incomplete(&copy, step, reason, status);
    bool const flushed = fflush(copy.stream) == 0;
    return deliver_line(writer, &copy, &line, &length, written && flushed);
  }
  fprintf(writer->stream,
          "{\"event\":\"incomplete\",\"step\":%zu,\"reason\":", step);
  json_string(writer->stream, reason);
  fprintf(writer->stream, ",\"exit_status\":%d}\n", status);
  return !ferror(writer->stream);
}

bool ad_trace_json_event(void *context, ADTraceEvent const *event) {
  ADTraceJSON *writer = context;
  if (writer->callback != nullptr) {
    char *line = nullptr;
    size_t length = 0;
    ADTraceJSON copy = *writer;
    copy.callback = nullptr;
    copy.stream = open_memstream(&line, &length);
    if (copy.stream == nullptr) {
      return false;
    }
    bool const written = ad_trace_json_event(&copy, event);
    bool const flushed = fflush(copy.stream) == 0;
    return deliver_line(writer, &copy, &line, &length, written && flushed);
  }
  FILE *stream = writer->stream;
  if (event->step > writer->limit) {
    fprintf(stream,
            "{\"event\":\"truncated\",\"step\":%zu,\"limit\":%zu,\"player\":%d,"
            "\"path\":",
            event->step, writer->limit, (int)event->player);
    path(stream, event->path);
    fputs("}\n", stream);
    writer->truncated = true;
    return false;
  }
  static char const *const names[] = {
      "call",   "return", "attractor", "set",   "move",  "check",
      "labels", "lift",   "round_end", "phase", "result"};
  fprintf(stream, "{\"event\":\"%s\",\"step\":%zu,\"player\":%d,\"path\":",
          names[event->kind], event->step, (int)event->player);
  path(stream, event->path);
  switch (event->kind) {
  case AD_TRACE_CALL:
    fprintf(stream, ",\"h\":%" PRIu64 ",\"paths\":[", event->h);
    path(stream, event->paths[0]);
    fputc(',', stream);
    path(stream, event->paths[1]);
    fputs("],\"subgame\":", stream);
    json_set(&(JSONWriter){stream, writer->game}, event->subgame);
    break;
  case AD_TRACE_RETURN:
    fputs(",\"vertices\":", stream);
    json_set(&(JSONWriter){stream, writer->game}, event->vertices);
    break;
  case AD_TRACE_ATTRACTOR:
    fputs(",\"kind\":", stream);
    json_string(stream, event->name);
    fputs(",\"target\":", stream);
    json_set(&(JSONWriter){stream, writer->game}, event->target);
    fputs(",\"vertices\":", stream);
    json_set(&(JSONWriter){stream, writer->game}, event->vertices);
    if (event->subgame != nullptr) {
      fputs(",\"subgame\":", stream);
      json_set(&(JSONWriter){stream, writer->game}, event->subgame);
    }
    if (event->safe != nullptr) {
      fputs(",\"safe\":", stream);
      json_set(&(JSONWriter){stream, writer->game}, event->safe);
    }
    break;
  case AD_TRACE_SET:
  case AD_TRACE_MOVE:
    fprintf(stream,
            ",\"part\":\"%c\",\"h\":%" PRIu64 ",\"children\":%zu,\"vertices\":",
            event->part, event->h, event->children);
    json_set(&(JSONWriter){stream, writer->game}, event->vertices);
    if (event->kind == AD_TRACE_MOVE) {
      fputs(",\"to_path\":", stream);
      path(stream, event->other_path);
      fprintf(stream, ",\"to_part\":\"%c\"", event->other_part);
    }
    break;
  case AD_TRACE_CHECK:
    fprintf(stream, ",\"is_attractor_decomposition\":%s,\"subgame\":",
            event->valid ? "true" : "false");
    json_set(&(JSONWriter){stream, writer->game}, event->subgame);
    fputs(",\"reason\":", stream);
    json_string(stream, event->name);
    break;
  case AD_TRACE_LABELS:
  case AD_TRACE_LIFT:
    fprintf(stream, ",\"vertex\":%" PRIu64 ",\"part\":\"%c\",\"h\":%" PRIu64,
            writer->game->vertices[event->vertex].external_id, event->part,
            event->h);
    if (event->kind == AD_TRACE_LIFT) {
      fprintf(stream, ",\"from\":%zu,\"to\":%zu", event->from, event->to);
    }
    break;
  case AD_TRACE_PHASE:
    fputs(",\"phase\":", stream);
    json_string(stream, event->name);
    fprintf(stream, ",\"value\":%zu,\"vertices\":", event->to);
    json_set(&(JSONWriter){stream, writer->game}, event->vertices);
    if (event->host != nullptr) {
      host_description(stream, event);
    }
    if (event->name != nullptr && strcmp(event->name, "host_node") == 0) {
      fprintf(stream, ",\"h\":%" PRIu64, event->h);
    }
    if (event->result != nullptr) {
      fputs(",\"result\":", stream);
      if (!ad_json_result(&(JSONWriter){stream, writer->game}, event->result,
                          AD_DOT_PLAYER_BOTH)) {
        return false;
      }
    }
    break;
  case AD_TRACE_RESULT:
    fputs(",\"initial_won\":[", stream);
    json_set(&(JSONWriter){stream, writer->game}, writer->initial_won);
    fputc(',', stream);
    json_set(&(JSONWriter){stream, writer->game},
             writer->initial_won == nullptr ? nullptr
                                            : writer->initial_won + 1);
    fputs("],\"result\":", stream);
    if (!ad_json_result(&(JSONWriter){stream, writer->game}, event->result,
                        AD_DOT_PLAYER_BOTH)) {
      return false;
    }
    break;
  case AD_TRACE_ROUND_END:
    break;
  }
  fputs("}\n", stream);
  return !ferror(stream);
}
