#ifndef AD_TRACE_H
#define AD_TRACE_H 1

#include <stdio.h>

#include "ad_lift_tree.h"
#include "ad_tree.h"

typedef struct {
  size_t const *indices;
  size_t length;
} ADTracePath;

typedef enum {
  AD_TRACE_CALL,
  AD_TRACE_RETURN,
  AD_TRACE_ATTRACTOR,
  AD_TRACE_SET,
  AD_TRACE_MOVE,
  AD_TRACE_CHECK,
  AD_TRACE_LABELS,
  AD_TRACE_LIFT,
  AD_TRACE_ROUND_END,
  AD_TRACE_PHASE,
  AD_TRACE_RESULT,
} ADTraceKind;

/* All pointers are borrowed until the callback returns. Vertex numbers in the
 * C observer are dense indices; the JSON writer translates them to game IDs.
 * H/T/S positions use host child indices, including empty host branches. */
typedef struct {
  ADTraceKind kind;
  size_t step;
  PGPlayer player;
  ADTracePath path;
  ADTracePath other_path;
  ADTracePath paths[2];
  uint64_t h;
  char part;
  char other_part;
  char const *name;
  PGSet const *vertices;
  PGSet const *target;
  PGSet const *subgame;
  PGSet const *safe;
  size_t vertex;
  size_t from;
  size_t to;
  size_t children;
  bool valid;
  ADResult const *result;
  /* Optional, versioned metadata on an existing phase. Borrowed like sets.
   * Null in untraced runs and on ordinary label/lift events. */
  ADLiftTree const *host;
  PGGame const *priority_game;
  size_t host_k;
  size_t host_t;
  size_t host_arity;
  bool host_implicit;
} ADTraceEvent;

typedef struct {
  bool (*callback)(void *context, ADTraceEvent const *event);
  void *context;
  size_t step;
  bool failed;
  size_t nesting; /* Nested solver calls share one final result event. */
} ADTrace;

/* Keep event construction, allocation and set derivation behind this branch. */
#define AD_TRACE(observer, ...)                                                \
  do {                                                                         \
    if ((observer) != nullptr) {                                               \
      ad_trace_emit((observer), (ADTraceEvent){__VA_ARGS__});                  \
    }                                                                          \
  } while (false)

void ad_trace_emit(ADTrace *trace, ADTraceEvent event);
ADTracePath ad_trace_host_path(ADLiftTree const *host, size_t node,
                               size_t storage[AD_PATH_CAPACITY]);
void ad_trace_label(ADTrace *trace, ADLiftTree const *host, size_t vertex,
                    size_t node, ADPositionKind kind, ADTraceKind event,
                    size_t from, size_t to);
void ad_trace_tree(ADTrace *trace, PGGame const *game, PGSet const *outer,
                   PGSet const *core, ADNode const *root, ADTracePath path);

typedef struct {
  FILE *stream;
  PGGame const *game;
  size_t limit;
  bool truncated;
  PGSet const *initial_won;
  bool (*callback)(void *context, char const *line, size_t length);
  void *context;
} ADTraceJSON;

[[nodiscard]] bool ad_trace_json_header(ADTraceJSON *writer,
                                        char const *version,
                                        char const *algorithm, int argc,
                                        char *const argv[]);
[[nodiscard]] bool ad_trace_json_event(void *context,
                                       ADTraceEvent const *event);
[[nodiscard]] bool ad_trace_json_incomplete(ADTraceJSON *writer, size_t step,
                                            char const *reason, int status);

#endif
