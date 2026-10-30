#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ad_lift.h"
#include "ad_tree_json.h"
#include "cli_commands.h"
#include "json_input.h"
#include "pg_preprocess.h"
#include "strahler.h"
#include "strahler_tree_version.h"

struct strahler_game {
  PGGame value;
};

void strahler_free(void *value) { free(value); }

char const *strahler_version(void) { return STRAHLER_TREE_VERSION; }

void strahler_game_free(strahler_game *game) {
  if (game != nullptr) {
    pg_game_destroy(&game->value);
    free(game);
  }
}

static int failure(char **error, int status, char const *message) {
  if (errno == ENOMEM) {
    status = 6;
    message = "Failed to allocate memory";
  }
  if (error != nullptr) {
    size_t const length = strlen(message) + 1;
    *error = malloc(length);
    if (*error == nullptr) {
      return 6;
    }
    memcpy(*error, message, length);
  }
  return status;
}

static bool outputs(char **json, char **error) {
  errno = 0;
  if (error != nullptr) {
    *error = nullptr;
  }
  if (json != nullptr) {
    *json = nullptr;
  }
  return json != nullptr;
}

static bool valid_text(char const *text, size_t length) {
  return text != nullptr && length > 0 && length < PTRDIFF_MAX &&
         memchr(text, '\0', length) == nullptr;
}

int strahler_game_parse(char const *text, size_t length, strahler_game **game,
                        char **error) {
  errno = 0;
  if (error != nullptr) {
    *error = nullptr;
  }
  if (game != nullptr) {
    *game = nullptr;
  }
  if (game == nullptr || !valid_text(text, length)) {
    return failure(error, 2, "Expected nonempty game text without NUL bytes");
  }
  strahler_game *parsed = calloc(1, sizeof(*parsed));
  if (parsed == nullptr) {
    return failure(error, 6, "Failed to allocate game");
  }
  FILE *stream = fmemopen((void *)text, length, "r");
  if (stream == nullptr) {
    strahler_game_free(parsed);
    return failure(error, 5, "Cannot open game text");
  }
  PGParseError parse = {0};
  bool const read = pg_game_read(stream, &parsed->value, &parse);
  bool const closed = fclose(stream) == 0;
  if (!read || !closed) {
    strahler_game_free(parsed);
    char message[320];
    snprintf(message, sizeof(message), "%zu:%zu: %s", parse.line, parse.column,
             read ? "Cannot close game text" : parse.message);
    return failure(error, read ? 5 : 3, message);
  }
  *game = parsed;
  return 0;
}

static int finish(FILE *stream, char **json, char **error, bool written) {
  bool const closed = fclose(stream) == 0;
  if (!written || !closed) {
    free(*json);
    *json = nullptr;
    return failure(error, 5, "Failed to write JSON");
  }
  return 0;
}

int strahler_game_summary(strahler_game const *handle, char **json,
                          char **error) {
  if (!outputs(json, error) || handle == nullptr) {
    return failure(error, 2, "Expected game and JSON output");
  }
  size_t length = 0;
  FILE *stream = open_memstream(json, &length);
  if (stream == nullptr) {
    return failure(error, 6, "Failed to allocate JSON stream");
  }
  PGGame const *game = &handle->value;
  fprintf(stream, "{\"schema\":\"strahler-game/1\",\"vertices\":[");
  for (size_t v = 0; v < game->vertex_count; v++) {
    PGVertex const *vertex = game->vertices + v;
    fprintf(stream,
            "%s{\"id\":%" PRIu64 ",\"priority\":%" PRIu64
            ",\"owner\":%d,\"name\":",
            v == 0 ? "" : ",", vertex->external_id, vertex->priority,
            (int)vertex->owner);
    if (vertex->name == nullptr) {
      fputs("null", stream);
    } else {
      json_string(stream, vertex->name);
    }
    fputs(",\"successors\":[", stream);
    for (size_t e = game->succ_offsets[v]; e < game->succ_offsets[v + 1]; e++) {
      fprintf(stream, "%s%" PRIu64, e == game->succ_offsets[v] ? "" : ",",
              game->vertices[game->successors[e]].external_id);
    }
    fputs("]}", stream);
  }
  fprintf(stream, "],\"edges\":%zu,\"start\":%" PRIu64 "}\n", game->edge_count,
          game->start_id);
  return finish(stream, json, error, !ferror(stream));
}

typedef int (*Command)(int argc, char *argv[], CLIIO *io);
typedef struct {
  strahler_event_cb callback;
  void *context;
} Observer;

static bool event_line(void *context, char const *line, size_t length) {
  Observer *observer = context;
  return observer->callback(observer->context, line, length) != 0;
}

static int command(Command run, char const *name, strahler_game const *game,
                   char const *text, size_t length, char const *const args[],
                   int count, strahler_event_cb callback, void *context,
                   char **json, char **error) {
  if (!outputs(json, error) || count < 0 || count > INT_MAX - 3 ||
      (count != 0 && args == nullptr)) {
    return failure(error, 2, "Invalid option array or JSON output");
  }
  for (int i = 0; i < count; i++) {
    if (args[i] == nullptr || strncmp(args[i], "--", 2) != 0 ||
        args[i][2] == '\0' || strncmp(args[i], "--format", 8) == 0 ||
        strncmp(args[i], "--trace=", 8) == 0 ||
        strcmp(args[i], "--help") == 0 || strcmp(args[i], "--version") == 0 ||
        strcmp(args[i], "--json") == 0) {
      return failure(error, 2,
                     "Expected solver options; format, help, version and trace "
                     "files are managed by the API");
    }
  }
  char **argv = (char **)calloc((size_t)count + 3, sizeof(*argv));
  if (argv == nullptr) {
    return failure(error, 6, "Failed to allocate options");
  }
  argv[0] = (char *)name;
  argv[1] = strcmp(name, "genstree") == 0 ? "--json" : "--format=json";
  for (int i = 0; i < count; i++) {
    argv[i + 2] = (char *)args[i];
  }
  size_t json_length = 0;
  size_t diagnostic_length = 0;
  char *diagnostic = nullptr;
  Observer observer = {callback, context};
  CLIIO io = {.game = game == nullptr ? nullptr : &game->value,
              .event_callback = callback == nullptr ? nullptr : event_line,
              .context = &observer};
  io.out = open_memstream(json, &json_length);
  io.err = open_memstream(&diagnostic, &diagnostic_length);
  if (text != nullptr) {
    io.in = fmemopen((void *)text, length, "r");
  }
  int status = 6;
  if (io.out != nullptr && io.err != nullptr &&
      (text == nullptr || io.in != nullptr)) {
    status = run(count + 2, argv, &io);
  }
  if (io.in != nullptr && fclose(io.in) != 0) {
    status = 5;
  }
  if (io.out != nullptr && fclose(io.out) != 0) {
    status = 5;
  }
  if (io.err != nullptr && fclose(io.err) != 0) {
    status = 5;
  }
  free((void *)argv);
  if (status != 0) {
    free(*json);
    *json = nullptr;
    status = failure(error, status,
                     diagnostic != nullptr && diagnostic[0] != '\0'
                         ? diagnostic
                         : "Command failed");
  }
  free(diagnostic);
  return status;
}

int strahler_decompose(strahler_game const *game, char const *const argv[],
                       int argc, strahler_event_cb callback, void *context,
                       char **json, char **error) {
  if (game == nullptr) {
    outputs(json, error);
    return failure(error, 2, "Expected a game");
  }
  return command(pg2adot_command, "pg2adot", game, nullptr, 0, argv, argc,
                 callback, context, json, error);
}

int strahler_progress_measures(strahler_game const *game,
                               char const *const argv[], int argc, char **json,
                               char **error) {
  if (game == nullptr) {
    outputs(json, error);
    return failure(error, 2, "Expected a game");
  }
  return command(pg2pmdot_command, "pg2pmdot", game, nullptr, 0, argv, argc,
                 nullptr, nullptr, json, error);
}

int strahler_genstree(int k, int h, int t, char **json, char **error) {
  char k_arg[48], h_arg[48], t_arg[48];
  snprintf(k_arg, sizeof(k_arg), "--strahler=%d", k);
  snprintf(h_arg, sizeof(h_arg), "--height=%d", h);
  snprintf(t_arg, sizeof(t_arg), "--log-leaves=%d", t);
  char const *args[] = {k_arg, h_arg, t_arg};
  return command(genstree_command, "genstree", nullptr, nullptr, 0, args, 3,
                 nullptr, nullptr, json, error);
}

static bool valid_leaf_depth(char const *text, size_t length) {
  size_t depth = 1;
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '|') {
      depth = 1;
    } else if (text[i] == ',' && i + 1 < length && text[i + 1] != '|' &&
               ++depth >= AD_PATH_CAPACITY) {
      return false;
    }
  }
  return true;
}

int strahler_pms_tree(char const *text, size_t length, char const *const argv[],
                      int argc, char **json, char **error) {
  if (!valid_text(text, length)) {
    outputs(json, error);
    return failure(error, 2, "Expected nonempty leaf stream without NUL bytes");
  }
  // Bound paths before parsing, writing or destroying recursive native trees.
  if (!valid_leaf_depth(text, length)) {
    outputs(json, error);
    return failure(error, 2, "Leaf stream path depth exceeds 1025");
  }
  return command(pms2dot_command, "pms2dot", nullptr, text, length, argv, argc,
                 nullptr, nullptr, json, error);
}

static size_t vertex_index(PGGame const *game, uint64_t id) {
  size_t lo = 0, hi = game->vertex_count;
  while (lo < hi) {
    size_t const mid = lo + (hi - lo) / 2;
    if (game->vertices[mid].external_id < id) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo < game->vertex_count && game->vertices[lo].external_id == id
             ? lo
             : SIZE_MAX;
}

static bool read_set(PGGame const *game, JSONValue const *value, PGSet *set) {
  if (value == nullptr || value->kind != J_ARRAY ||
      !pg_set_init(set, game->vertex_count)) {
    return false;
  }
  for (value = value->child; value != nullptr; value = value->next) {
    size_t const index =
        value->kind == J_NUMBER ? vertex_index(game, value->number) : SIZE_MAX;
    if (index == SIZE_MAX || pg_set_contains(set, index)) {
      return false;
    }
    pg_set_add(set, index);
  }
  return true;
}

static bool read_node(PGGame const *game, JSONValue const *value,
                      PGPlayer player, ADNode **root, PGSet *core, PGSet *outer,
                      size_t depth) {
  if (value == nullptr || depth >= AD_PATH_CAPACITY ||
      !pg_set_init(core, game->vertex_count) ||
      !pg_set_init(outer, game->vertex_count)) {
    return false;
  }
  if (value->kind == J_NULL) {
    return true;
  }
  JSONValue const *bound = json_member(value, "h");
  JSONValue const *children = json_member(value, "children");
  if (bound == nullptr || bound->kind != J_NUMBER || children == nullptr ||
      children->kind != J_ARRAY) {
    return false;
  }
  *root = ad_node_create(player, bound->number, game->vertex_count);
  if (*root == nullptr) {
    return false;
  }
  PGSet highest = {0}, top = {0}, side = {0};
  bool valid = read_set(game, json_member(value, "H"), &highest) &&
               read_set(game, json_member(value, "T"), &top) &&
               read_set(game, json_member(value, "S"), &side);
  if (valid) {
    pg_set_union_into(&(*root)->top_attractor, &highest);
    pg_set_union_into(&(*root)->top_attractor, &top);
    pg_set_union_into(core, &(*root)->top_attractor);
    pg_set_union_into(outer, &side);
  }
  pg_set_destroy(&highest);
  pg_set_destroy(&top);
  pg_set_destroy(&side);
  if (!valid) {
    return false;
  }
  for (JSONValue const *child = children->child; child != nullptr;
       child = child->next) {
    ADChild next = {0};
    valid = read_node(game, child, player, &next.subtree, &next.trap,
                      &next.attractor, depth + 1);
    if (valid) {
      pg_set_union_into(core, &next.attractor);
      valid = ad_node_append_child(*root, &next);
    }
    if (!valid) {
      pg_set_destroy(&next.trap);
      pg_set_destroy(&next.attractor);
      ad_node_destroy(next.subtree);
      return false;
    }
  }
  pg_set_union_into(outer, core);
  return true;
}

static bool relative_check(PGGame const *game, PGSet const *domain,
                           JSONValue const *value, ADNode const *node,
                           ADVerifyError *error, ADVerifyErrors *errors) {
  if (node == nullptr || pg_set_empty(domain)) {
    return true;
  }
  uint64_t const bound = node->priority_bound;
  char const *parts[] = {"H", "T", "S"};
  PGSet seen = {0};
  if (!pg_set_init(&seen, game->vertex_count)) {
    ad_verify_error(error, AD_VERIFY_MEMORY, SIZE_MAX,
                    "failed to allocate relative checker set");
    return false;
  }
  bool valid = true;
  for (size_t part = 0; part < 3 && valid; part++) {
    JSONValue const *items = json_member(value, parts[part]);
    for (JSONValue const *item = items->child; item != nullptr;
         item = item->next) {
      size_t const vertex = vertex_index(game, item->number);
      if (!pg_set_contains(domain, vertex)) {
        continue;
      }
      ADVerifyCondition condition = AD_VERIFY_OK;
      if (pg_set_contains(&seen, vertex)) {
        condition = AD_VERIFY_PARTITION;
      } else if (part == 0 && game->vertices[vertex].priority != bound) {
        condition = AD_VERIFY_PRIORITY;
      } else if (part == 1 && game->vertices[vertex].priority == bound) {
        condition = AD_VERIFY_TOP;
      }
      if (condition != AD_VERIFY_OK) {
        ad_verify_error(error, condition, vertex,
                        "tree-relative H/T/S parts disagree with the node "
                        "bound or partition");
        valid = ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX);
        if (!valid) {
          break;
        }
      }
      pg_set_add(&seen, vertex);
    }
  }
  // S is outside the entire core, including every child's side attractor.
  JSONValue const *side = json_member(value, "S");
  for (JSONValue const *item = side->child; item != nullptr && valid;
       item = item->next) {
    size_t const vertex = vertex_index(game, item->number);
    if (!pg_set_contains(domain, vertex)) {
      continue;
    }
    for (size_t i = 0; i < node->child_count; i++) {
      if (pg_set_contains(&node->children[i].attractor, vertex)) {
        ad_verify_error(error, AD_VERIFY_PARTITION, vertex,
                        "tree-relative S overlaps the node core");
        valid = ad_verify_collect(errors, error, SIZE_MAX, SIZE_MAX);
        break;
      }
    }
  }
  pg_set_destroy(&seen);
  JSONValue const *child = json_member(value, "children")->child;
  for (size_t i = 0; i < node->child_count && valid; i++, child = child->next) {
    if (errors != nullptr) {
      errors->path[errors->path_length++] = i;
    }
    valid = relative_check(game, domain, child, node->children[i].subtree,
                           error, errors);
    if (errors != nullptr) {
      errors->path_length--;
    }
    if (!valid && error->path_length < AD_PATH_CAPACITY) {
      memmove(error->path + 1, error->path,
              error->path_length * sizeof(*error->path));
      error->path[0] = i;
      error->path_length++;
    }
  }
  return valid;
}

static void check_json(FILE *out, PGGame const *game, bool ok,
                       ADVerifyError const *result,
                       ADVerifyErrors const *violations) {
  fprintf(out, "{\"ok\":%s,\"condition\":%d,\"path\":[", ok ? "true" : "false",
          (int)result->condition);
  for (size_t i = 0; i < result->path_length; i++) {
    fprintf(out, "%s%zu", i == 0 ? "" : ",", result->path[i]);
  }
  fputs("],\"vertex\":", out);
  if (result->vertex == SIZE_MAX) {
    fputs("null", out);
  } else {
    fprintf(out, "%" PRIu64, game->vertices[result->vertex].external_id);
  }
  fputs(",\"message\":", out);
  json_string(out, result->message);
  if (violations != nullptr) {
    fputs(",\"violations\":[", out);
    for (size_t i = 0; i < violations->count; i++) {
      ADVerifyError const *item = violations->items + i;
      fprintf(out, "%s{\"condition\":%d,\"path\":[", i == 0 ? "" : ",",
              (int)item->condition);
      for (size_t j = 0; j < item->path_length; j++) {
        fprintf(out, "%s%zu", j == 0 ? "" : ",", item->path[j]);
      }
      fputs("],\"vertex\":", out);
      if (item->vertex == SIZE_MAX) {
        fputs("null", out);
      } else {
        fprintf(out, "%" PRIu64, game->vertices[item->vertex].external_id);
      }
      fputs(",\"edge\":", out);
      if (violations->edges[i][0] == SIZE_MAX) {
        fputs("null", out);
      } else {
        fprintf(out, "[%" PRIu64 ",%" PRIu64 "]",
                game->vertices[violations->edges[i][0]].external_id,
                game->vertices[violations->edges[i][1]].external_id);
      }
      fputc('}', out);
    }
    fputc(']', out);
  }
  fputc('}', out);
}

static bool normalize(strahler_game const *handle, int compress, PGGame *game,
                      PGPreprocessResult *pre) {
  return cli_game_copy(&handle->value, game) &&
         pg_preprocess_priority_map(game, compress != 0, &pre->priority_map) &&
         pg_priority_map_apply(&pre->priority_map, game);
}

int strahler_check(strahler_game const *handle, char const *tree_json,
                   int player, char const *subgame_json, int compress,
                   char **json, char **error) {
  if (!outputs(json, error) || handle == nullptr || tree_json == nullptr ||
      player < 0 || player > 1 || compress < 0 || compress > 1) {
    return failure(error, 2, "Expected game, tree JSON and player 0 or 1");
  }
  PGGame storage = {0};
  PGPreprocessResult pre = {0};
  bool const normalized = normalize(handle, compress, &storage, &pre);
  PGGame const *game = &storage;
  JSONValue *tree = json_parse_input(tree_json);
  JSONValue *subgame =
      subgame_json == nullptr ? nullptr : json_parse_input(subgame_json);
  ADNode *root = nullptr;
  PGSet core = {0}, outer = {0}, domain = {0};
  bool parsed =
      normalized && tree != nullptr &&
      read_node(game, tree, (PGPlayer)player, &root, &core, &outer, 0);
  if (parsed) {
    parsed = subgame_json == nullptr ? pg_set_init(&domain, game->vertex_count)
                                     : read_set(game, subgame, &domain);
    if (subgame_json == nullptr && parsed) {
      pg_set_fill(&domain);
    }
  }
  int status = 2;
  if (parsed) {
    ADVerifyError result = {.vertex = SIZE_MAX};
    bool const ok =
        ad_tree_check(game, &domain, (PGPlayer)player, root, &result) &&
        relative_check(game, &domain, tree, root, &result, nullptr);
    ADVerifyErrors violations = {0};
    ADVerifyError relative = {.vertex = SIZE_MAX};
    bool const collected =
        ok ||
        (ad_tree_check_all(game, &domain, (PGPlayer)player, root,
                           &violations) &&
         relative_check(game, &domain, tree, root, &relative, &violations));
    if (!collected || result.condition == AD_VERIFY_MEMORY) {
      errno = ENOMEM;
      status = failure(error, 6, "Failed to allocate checker state");
    } else {
      size_t length = 0;
      FILE *out = open_memstream(json, &length);
      if (out == nullptr) {
        status = failure(error, 6, "Failed to allocate JSON stream");
      } else {
        check_json(out, game, ok, &result, &violations);
        fputc('\n', out);
        status = finish(out, json, error, !ferror(out));
      }
    }
    free(violations.items);
    free(violations.edges);
  } else {
    status = failure(error, 2,
                     "Invalid tree or subgame JSON (expected h,H,T,S,children "
                     "and known vertex IDs)");
  }
  ad_node_destroy(root);
  pg_set_destroy(&core);
  pg_set_destroy(&outer);
  pg_set_destroy(&domain);
  json_input_destroy(tree);
  json_input_destroy(subgame);
  pg_preprocess_destroy(&pre);
  free(storage.vertices);
  return status;
}

static bool label_position(PGGame const *game, ADLiftTree const *host,
                           JSONValue const *label, size_t *vertex,
                           size_t *position) {
  JSONValue const *id = json_member(label, "vertex");
  JSONValue const *path = json_member(label, "path");
  JSONValue const *part = json_member(label, "part");
  if (id == nullptr || id->kind != J_NUMBER || path == nullptr ||
      path->kind != J_ARRAY || part == nullptr || part->kind != J_STRING ||
      strlen(part->string) != 1) {
    return false;
  }
  *vertex = vertex_index(game, id->number);
  if (*vertex == SIZE_MAX) {
    return false;
  }
  size_t node = 0;
  for (path = path->child; path != nullptr; path = path->next) {
    if (path->kind != J_NUMBER ||
        path->number >= host->nodes[node].child_count) {
      return false;
    }
    node = host->nodes[node].first_child;
    for (uint64_t i = 0; i < path->number; i++) {
      node = host->nodes[node].next_sibling;
    }
  }
  switch (part->string[0]) {
  case 'H':
    *position = host->nodes[node].regular;
    break;
  case 'T':
    *position = host->nodes[node].minus_inf;
    break;
  case 'S':
    *position = host->nodes[node].after;
    break;
  default:
    return false;
  }
  return true;
}

int strahler_materialize_partial(strahler_game const *handle,
                                 char const *labels_json, int compress,
                                 char **json, char **error) {
  if (!outputs(json, error) || handle == nullptr || labels_json == nullptr ||
      compress < 0 || compress > 1) {
    return failure(error, 2, "Expected game and labels JSON");
  }
  PGGame storage = {0};
  PGPreprocessResult pre = {0};
  bool const normalized = normalize(handle, compress, &storage, &pre);
  PGGame const *game = &storage;
  JSONValue *input = json_parse_input(labels_json);
  JSONValue const *player = json_member(input, "player");
  JSONValue const *h = json_member(input, "h");
  JSONValue const *tree = json_member(input, "tree");
  JSONValue const *labels = json_member(input, "labels");
  OrderedTreeNode *base = nullptr;
  ADLiftTree host = {0};
  ADLiftLabelling labelling = {.vertex_count = game->vertex_count};
  ADNode *root = nullptr;
  ADVerifyError *problems = nullptr;
  size_t count = 0;
  ADLiftError lift_error = {0};
  OrderedTreeError parse = {0};
  int status = 2;
  if (!normalized || player == nullptr || player->kind != J_NUMBER ||
      player->number > 1 || h == nullptr || h->kind != J_NUMBER ||
      tree == nullptr || tree->kind != J_STRING || labels == nullptr ||
      labels->kind != J_ARRAY ||
      !valid_leaf_depth(tree->string, strlen(tree->string)) ||
      !ordered_tree_parse_leaf_stream(tree->string, &base, &parse) ||
      !ad_lift_tree_build(base, (PGPlayer)player->number, h->number, &host,
                          &lift_error)) {
    goto invalid;
  }
  labelling.labels = malloc(game->vertex_count * sizeof(*labelling.labels));
  if (labelling.labels == nullptr ||
      !pg_set_init(&labelling.dominion, game->vertex_count)) {
    goto invalid;
  }
  for (size_t v = 0; v < game->vertex_count; v++) {
    labelling.labels[v] = SIZE_MAX;
  }
  for (JSONValue const *label = labels->child; label != nullptr;
       label = label->next) {
    size_t vertex = 0, position = 0;
    if (!label_position(game, &host, label, &vertex, &position) ||
        labelling.labels[vertex] != SIZE_MAX) {
      goto invalid;
    }
    labelling.labels[vertex] = position;
    if (position < host.top) {
      pg_set_add(&labelling.dominion, vertex);
    }
  }
  if (!ad_lift_materialize_partial(game, &host, &labelling, &root, &problems,
                                   &count, &lift_error)) {
    goto invalid;
  }
  size_t length = 0;
  FILE *out = open_memstream(json, &length);
  if (out == nullptr) {
    goto invalid;
  }
  fputs("{\"tree\":", out);
  bool const wrote = ad_json_tree(&(JSONWriter){out, game}, &labelling.dominion,
                                  &labelling.dominion, root);
  fputs(",\"problems\":[", out);
  for (size_t i = 0; i < count; i++) {
    if (i != 0) {
      fputc(',', out);
    }
    check_json(out, game, false, problems + i, nullptr);
  }
  fputs("]}\n", out);
  status = finish(out, json, error, wrote && !ferror(out));
  goto cleanup;
invalid:
  status = failure(error, 2,
                   lift_error.message[0] == '\0'
                       ? "Invalid labels JSON (expected player,h,tree,labels "
                         "and compatible positions)"
                       : lift_error.message);
cleanup:
  free(problems);
  ad_node_destroy(root);
  ad_lift_labelling_destroy(&labelling);
  ad_lift_tree_destroy(&host);
  ordered_tree_destroy(base);
  json_input_destroy(input);
  pg_preprocess_destroy(&pre);
  free(storage.vertices);
  return status;
}
