#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ad_lift_solve.h"
#include "ad_tree_dot.h"
#include "ad_tree_json.h"
#include "cli_commands.h"
#include "cli_version.h"
#include "pg_preprocess.h"
#include "strahler_tree_version.h"
#include "zielonka.h"

enum {
  EXIT_GAME = 3,
  EXIT_SOLVER = 4,
  EXIT_IO = 5,
};

enum { DEFAULT_MAX_HOST_LEAVES = 1000000 };

typedef enum {
  PRIORITY_MODE_ORIGINAL,
  PRIORITY_MODE_COMPACT,
} PriorityMode;

typedef enum {
  ALGORITHM_LIFTING,
  ALGORITHM_ZIELONKA,
  ALGORITHM_ZIELONKA_SYM,
  ALGORITHM_LIFTING_SYM,
} Algorithm;

static void usage(FILE *out, char *argv[]) {
  cli_usage(out, argv[0], " [FILE]");
  fprintf(out,
          "  --format=dot|json                           default: dot\n"
          "  --player=both|even|odd                      default: both\n"
          "  --view=classic|tree-relative|jurdzinski     default: classic\n"
          "  --labels=counts|sets|none                   default: counts\n"
          "  --max-set-items=N                           default: 32\n"
          "  --priority-mode=original|compact            default: original\n"
          "  --algorithm=lifting|zielonka|zielonka-sym|lifting-sym\n"
          "                                              default: lifting\n"
          "  --initial-shave=loops,wcwc,single|all|none  default: none\n"
          "  --iter-shave                                Remove dominions "
          "between lifts (default: off)\n"
          "  --compress                                  Merge same-parity "
          "priorities (default: off)\n"
          "  --tree-k=K                                  Lift in U^K_{t,H}; "
          "may be partial\n"
          "  --adaptive-t                                Raise t until "
          "complete; --tree-k caps K\n"
          "  --adaptive-t=incremental                    Reuse certified "
          "opponent wins as TOP\n"
          "  --start-t=T                                 First adaptive t "
          "(default: 0)\n"
          "  --kary=A                                    Lift in a full A-ary "
          "tree; may be partial\n"
          "  --tree-file=FILE                            Leaf-stream host "
          "tree; may be partial\n"
          "  --max-host-leaves=N                         Host leaf limit "
          "(default: %d)\n"
          "  --trace=FILE                                Write algorithm "
          "events as JSON Lines\n"
          "  --trace-limit=N                             Stop after N events "
          "(default: 1000000)\n"
          "  --stats                                     Print statistics to "
          "stderr (default: off)\n"
          "  --no-verify                                 Skip verification "
          "(default: verify)\n"
          "Default host: a full Strahler tree. FILE defaults to stdin; - reads "
          "stdin.\n",
          DEFAULT_MAX_HOST_LEAVES);
}

[[nodiscard]] static bool same_file(struct stat const *left,
                                    struct stat const *right) {
  return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

/* Open before checking identity, and truncate only after protecting inputs and
 * output streams, including aliases through symlinks and /dev/stdout. */
[[nodiscard]] static FILE *open_trace(char const *path, char const *game_path,
                                      char const *tree_path) {
  int const descriptor = open(path, O_WRONLY | O_CREAT, 0666);
  if (descriptor < 0) {
    return nullptr;
  }
  struct stat destination = {0};
  struct stat source = {0};
  bool safe = fstat(descriptor, &destination) == 0;
  if ((fstat(STDOUT_FILENO, &source) == 0 &&
       same_file(&source, &destination)) ||
      (fstat(STDERR_FILENO, &source) == 0 &&
       same_file(&source, &destination)) ||
      (game_path == nullptr && fstat(STDIN_FILENO, &source) == 0 &&
       same_file(&source, &destination)) ||
      (game_path != nullptr && stat(game_path, &source) == 0 &&
       same_file(&source, &destination)) ||
      (tree_path != nullptr && stat(tree_path, &source) == 0 &&
       same_file(&source, &destination))) {
    errno = EINVAL;
    safe = false;
  }
  if (!safe ||
      (S_ISREG(destination.st_mode) && ftruncate(descriptor, 0) != 0)) {
    int const saved_errno = errno;
    (void)close(descriptor);
    errno = saved_errno;
    return nullptr;
  }
  FILE *stream = fdopen(descriptor, "w");
  if (stream == nullptr) {
    int const saved_errno = errno;
    (void)close(descriptor);
    errno = saved_errno;
  }
  return stream;
}

[[nodiscard]] static bool parse_size(char const *text, size_t *value) {
  if (text == nullptr || text[0] == '\0' ||
      strspn(text, "0123456789") != strlen(text)) {
    return false;
  }
  errno = 0;
  char *end = nullptr;
  uintmax_t const parsed = strtoumax(text, &end, 10);
  if (errno == ERANGE || *end != '\0' || parsed > SIZE_MAX) {
    return false;
  }
  *value = (size_t)parsed;
  return true;
}

/* Read a whole file into a null-terminated string. */
[[nodiscard]] static char *read_file(char const *path) {
  FILE *stream = fopen(path, "rb");
  if (stream == nullptr) {
    return nullptr;
  }
  char *text = nullptr;
  size_t length = 0;
  size_t capacity = 0;
  do {
    if (capacity - length < 2) {
      size_t const grown = capacity == 0 ? 4096 : capacity * 2;
      char *resized = grown < capacity ? nullptr : realloc(text, grown);
      if (resized == nullptr) {
        free(text);
        (void)fclose(stream);
        errno = ENOMEM;
        return nullptr;
      }
      text = resized;
      capacity = grown;
    }
    size_t const read = fread(text + length, 1, capacity - length - 1, stream);
    assert(read < capacity - length);
    length += read;
    if (read == 0) {
      break;
    }
  } while (!feof(stream) && !ferror(stream));
  bool const failed = ferror(stream) != 0;
  if (fclose(stream) != 0 || failed) {
    free(text);
    return nullptr;
  }
  text[length] = '\0';
  return text;
}

[[nodiscard]] static char const *result_kind(ADResult const *result) {
  return result->kind == AD_RESULT_COMPLETE ? "complete" : "partial";
}

static void write_tree_stats(CLIIO *io, ADResult const *result,
                             size_t const player, ADTreeMetrics const metrics) {
  fprintf(io->err,
          "%s=%zu tree_nodes=%zu tree_leaves=%zu tree_height=%zu "
          "strahler=%zu\n",
          result->kind == AD_RESULT_COMPLETE ? "region" : "dominion",
          pg_set_count(&result->region[player]), metrics.nodes, metrics.leaves,
          metrics.height, metrics.strahler);
}

static void write_zielonka_stats(CLIIO *io, ADResult const *result,
                                 size_t const vertices,
                                 ZielonkaStats const *stats) {
  fprintf(io->err, "zielonka: vertices=%zu result=%s\n", vertices,
          result_kind(result));
  fprintf(io->err, "zielonka: calls=%zu attractors=%zu\n", stats->calls,
          stats->attractors);
  for (size_t player = 0; player < 2; player++) {
    fprintf(io->err, "zielonka %s: ", player == PG_EVEN ? "even" : "odd");
    write_tree_stats(io, result, player,
                     ad_tree_metrics(result->decomposition[player]));
  }
}

static void write_sym_stats(CLIIO *io, char const *name,
                            ADSymStats const *stats, ADResult const *result,
                            size_t const vertices) {
  fprintf(io->err,
          "%s: vertices=%zu result=%s calls=%zu attractors=%zu "
          "moves=%zu sets=%zu certification_lifts=%zu separator=%zu\n",
          name, vertices, result_kind(result), stats->calls, stats->attractors,
          stats->moves, stats->sets, stats->certification_lifts,
          stats->separator);
  for (size_t player = 0; player < 2; player++) {
    fprintf(io->err, "%s %s: ", name, player == PG_EVEN ? "even" : "odd");
    write_tree_stats(io, result, player,
                     ad_tree_metrics(result->decomposition[player]));
  }
}

static void write_lifting_stats(CLIIO *io, ADLiftSolveStats const *stats,
                                ADLiftOptions const *options,
                                ADResult const *result) {
  char const *name = options->symmetric ? "lifting-sym" : "lifting";
  fprintf(io->err, "%s: vertices=%zu t_full=%zu result=%s\n", name,
          stats->vertices, stats->t_full, result_kind(result));
  if (options->iter_shave) {
    fprintf(io->err, "iter-shave: rounds=%zu\n", stats->iterations);
    for (size_t round = 0; round < stats->iterations; round++) {
      size_t even = pg_set_count(&result->region[0]);
      size_t odd = pg_set_count(&result->region[1]);
      size_t t = stats->player[0].t;
      if (round < result->shave_count) {
        ADShaveRound const *shave = result->shaves + round;
        even = pg_set_count(&shave->attracted[0]);
        odd = pg_set_count(&shave->attracted[1]);
        t = shave->t;
      }
      fprintf(io->err, "iter-shave: round=%zu", round + 1);
      if (options->mode == AD_LIFT_HOST_ADAPTIVE) {
        fprintf(io->err, " t=%zu", t);
      }
      fprintf(io->err, " shaved=%zu even=%zu odd=%zu\n", even + odd, even, odd);
    }
  }
  if (options->symmetric) {
    ADSymStats const *counts = &stats->symmetric;
    fprintf(io->err,
            "%s: calls=%zu attractors=%zu moves=%zu sets=%zu "
            "certification_lifts=%zu separator=%zu\n",
            name, counts->calls, counts->attractors, counts->moves,
            counts->sets, counts->certification_lifts, counts->separator);
  }
  if (options->mode == AD_LIFT_HOST_ADAPTIVE) {
    fputs("lifting: first_complete_t=", io->err);
    if (stats->adaptive_complete) {
      fprintf(io->err, "%zu even_first_full_t=%zu odd_first_full_t=%zu",
              stats->first_complete_t, stats->player[PG_EVEN].first_full_t,
              stats->player[PG_ODD].first_full_t);
    } else {
      fputs("none", io->err);
    }
    fprintf(io->err, " budget_exhausted=%s\n",
            stats->budget_exhausted ? "yes" : "no");
  }
  if (stats->vertices == 0) {
    return;
  }
  for (size_t player = 0; player < 2; player++) {
    ADLiftPlayerStats const *current = stats->player + player;
    fprintf(io->err, "%s %s: root_level=%" PRIu64 " H=%zu k_full=%zu ", name,
            player == PG_EVEN ? "even" : "odd", current->root_level,
            current->height, current->k_full);
    if (options->mode == AD_LIFT_HOST_TREE) {
      fputs("host=file", io->err);
    } else if (options->mode == AD_LIFT_HOST_KARY) {
      fprintf(io->err, "host=kary arity=%zu", current->arity);
    } else {
      fprintf(io->err, "host=strahler K=%zu t=%zu", current->k, current->t);
    }
    fprintf(io->err, " host_nodes=%zu host_leaves=%zu positions=%zu ",
            current->host_nodes, current->host_leaves, current->host_positions);
    if (!options->symmetric) {
      fprintf(io->err, "rounds=%zu lifts=%zu top=%zu ", current->lift.rounds,
              current->lift.lifts, current->lift.top_count);
    }
    if (options->mode == AD_LIFT_HOST_ADAPTIVE) {
      fprintf(io->err, "total_lifts=%zu ", current->total_lifts);
    }
    write_tree_stats(io, result, player, current->tree);
  }
}

#ifdef AD_TRACE_COUNT_ONLY
typedef struct {
  size_t count;
  ADTraceJSON *writer;
} TraceCount;

static bool count_trace_event(void *context, ADTraceEvent const *event) {
  TraceCount *count = context;
  ++count->count;
  assert(event->step == count->count);
  return event->kind != AD_TRACE_RESULT ||
         ad_trace_json_event(count->writer, event);
}
#endif

int pg2adot_command(int argc, char *argv[], CLIIO *io) {
  CLIParser parser = {.index = 1};

  enum {
    OPTION_FORMAT = 1,
    OPTION_PLAYER,
    OPTION_VIEW,
    OPTION_LABELS,
    OPTION_MAX_ITEMS,
    OPTION_PRIORITY_MODE,
    OPTION_NO_VERIFY,
    OPTION_ALGORITHM,
    OPTION_TREE_K,
    OPTION_ADAPTIVE_T,
    OPTION_START_T,
    OPTION_KARY,
    OPTION_TREE_FILE,
    OPTION_MAX_HOST_LEAVES,
    OPTION_STATS,
    OPTION_SHAVE,
    OPTION_COMPRESS,
    OPTION_ITER_SHAVE,
    OPTION_TRACE,
    OPTION_TRACE_LIMIT,
  };
  static struct option const options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"version", no_argument, nullptr, 'v'},
      {"format", required_argument, nullptr, OPTION_FORMAT},
      {"player", required_argument, nullptr, OPTION_PLAYER},
      {"view", required_argument, nullptr, OPTION_VIEW},
      {"labels", required_argument, nullptr, OPTION_LABELS},
      {"max-set-items", required_argument, nullptr, OPTION_MAX_ITEMS},
      {"priority-mode", required_argument, nullptr, OPTION_PRIORITY_MODE},
      {"no-verify", no_argument, nullptr, OPTION_NO_VERIFY},
      {"algorithm", required_argument, nullptr, OPTION_ALGORITHM},
      {"tree-k", required_argument, nullptr, OPTION_TREE_K},
      {"adaptive-t", optional_argument, nullptr, OPTION_ADAPTIVE_T},
      {"start-t", required_argument, nullptr, OPTION_START_T},
      {"kary", required_argument, nullptr, OPTION_KARY},
      {"tree-file", required_argument, nullptr, OPTION_TREE_FILE},
      {"max-host-leaves", required_argument, nullptr, OPTION_MAX_HOST_LEAVES},
      {"trace", required_argument, nullptr, OPTION_TRACE},
      {"trace-limit", required_argument, nullptr, OPTION_TRACE_LIMIT},
      {"stats", no_argument, nullptr, OPTION_STATS},
      {"initial-shave", required_argument, nullptr, OPTION_SHAVE},
      {"compress", no_argument, nullptr, OPTION_COMPRESS},
      {"iter-shave", no_argument, nullptr, OPTION_ITER_SHAVE},
      {nullptr, 0, nullptr, 0},
  };

  ADDotPlayer player = AD_DOT_PLAYER_BOTH;
  ADDotView view = AD_DOT_VIEW_CLASSIC;
  ADDotLabels labels = AD_DOT_LABEL_COUNTS;
  size_t max_set_items = 32;
  PriorityMode priority_mode = PRIORITY_MODE_ORIGINAL;
  Algorithm algorithm = ALGORITHM_LIFTING;
  ADLiftOptions lift_options = {
      .mode = AD_LIFT_HOST_FULL,
      .max_host_leaves = DEFAULT_MAX_HOST_LEAVES,
      .schedule = AD_LIFT_SCHEDULE_ROUNDS,
      .verify = true,
  };
  size_t tree_k = 0;
  bool adaptive_t = false;
  bool start_t_given = false;
  size_t start_t = 0;
  size_t arity = 0;
  char const *tree_path = nullptr;
  bool host_limit_given = false;
  bool print_stats = false;
  bool json = false;
  bool dot_options = false;
  char const *trace_path = nullptr;
  size_t trace_limit = 1000000;
  bool verify = true;
  PGPreprocessOptions preprocess_options = {0};
  int option = 0;
  while ((option = cli_next_option(argc, argv, &parser, options)) != -1) {
    switch (option) {
    case 'v':
      return cli_version_to(io->out, argv[0]);
    case 'h':
      usage(io->out, argv);
      return EXIT_SUCCESS;
    case OPTION_FORMAT:
      if (strcmp(parser.argument, "json") == 0) {
        json = true;
      } else if (strcmp(parser.argument, "dot") == 0) {
        json = false;
      } else {
        return cli_error_to(io->err, argv[0], "Invalid --format value");
      }
      break;
    case OPTION_PLAYER:
      if (strcmp(parser.argument, "both") == 0) {
        player = AD_DOT_PLAYER_BOTH;
      } else if (strcmp(parser.argument, "even") == 0) {
        player = AD_DOT_PLAYER_EVEN;
      } else if (strcmp(parser.argument, "odd") == 0) {
        player = AD_DOT_PLAYER_ODD;
      } else {
        return cli_error_to(io->err, argv[0], "Invalid --player value");
      }
      break;
    case OPTION_VIEW:
      dot_options = true;
      if (strcmp(parser.argument, "classic") == 0) {
        view = AD_DOT_VIEW_CLASSIC;
      } else if (strcmp(parser.argument, "tree-relative") == 0) {
        view = AD_DOT_VIEW_TREE_RELATIVE;
      } else if (strcmp(parser.argument, "jurdzinski") == 0) {
        view = AD_DOT_VIEW_JURDZINSKI;
      } else {
        return cli_error_to(io->err, argv[0], "Invalid --view value");
      }
      break;
    case OPTION_LABELS:
      dot_options = true;
      if (strcmp(parser.argument, "counts") == 0) {
        labels = AD_DOT_LABEL_COUNTS;
      } else if (strcmp(parser.argument, "sets") == 0) {
        labels = AD_DOT_LABEL_SETS;
      } else if (strcmp(parser.argument, "none") == 0) {
        labels = AD_DOT_LABEL_NONE;
      } else {
        return cli_error_to(io->err, argv[0], "Invalid --labels value");
      }
      break;
    case OPTION_MAX_ITEMS:
      dot_options = true;
      if (!parse_size(parser.argument, &max_set_items)) {
        return cli_error_to(io->err, argv[0], "Invalid --max-set-items value");
      }
      break;
    case OPTION_PRIORITY_MODE:
      if (strcmp(parser.argument, "original") == 0) {
        priority_mode = PRIORITY_MODE_ORIGINAL;
      } else if (strcmp(parser.argument, "compact") == 0) {
        priority_mode = PRIORITY_MODE_COMPACT;
      } else {
        return cli_error_to(io->err, argv[0], "Invalid --priority-mode value");
      }
      break;
    case OPTION_NO_VERIFY:
      verify = false;
      break;
    case OPTION_ALGORITHM:
      if (strcmp(parser.argument, "lifting") == 0) {
        algorithm = ALGORITHM_LIFTING;
      } else if (strcmp(parser.argument, "zielonka") == 0) {
        algorithm = ALGORITHM_ZIELONKA;
      } else if (strcmp(parser.argument, "zielonka-sym") == 0) {
        algorithm = ALGORITHM_ZIELONKA_SYM;
      } else if (strcmp(parser.argument, "lifting-sym") == 0) {
        algorithm = ALGORITHM_LIFTING_SYM;
      } else {
        return cli_error_to(io->err, argv[0], "Invalid --algorithm value");
      }
      break;
    case OPTION_TREE_K:
      if (!parse_size(parser.argument, &tree_k) || tree_k == 0) {
        return cli_error_to(io->err, argv[0], "Invalid --tree-k value");
      }
      break;
    case OPTION_ADAPTIVE_T:
      if (parser.argument != nullptr &&
          strcmp(parser.argument, "incremental") != 0) {
        return cli_error_to(io->err, argv[0], "Invalid --adaptive-t value");
      }
      adaptive_t = true;
      lift_options.incremental = parser.argument != nullptr;
      break;
    case OPTION_START_T:
      if (!parse_size(parser.argument, &start_t)) {
        return cli_error_to(io->err, argv[0], "Invalid --start-t value");
      }
      start_t_given = true;
      break;
    case OPTION_KARY:
      if (!parse_size(parser.argument, &arity) || arity == 0) {
        return cli_error_to(io->err, argv[0], "Invalid --kary value");
      }
      break;
    case OPTION_TREE_FILE:
      if (parser.argument[0] == '\0') {
        return cli_error_to(io->err, argv[0], "Invalid --tree-file value");
      }
      tree_path = parser.argument;
      break;
    case OPTION_MAX_HOST_LEAVES:
      if (!parse_size(parser.argument, &lift_options.max_host_leaves)) {
        return cli_error_to(io->err, argv[0],
                            "Invalid --max-host-leaves value");
      }
      host_limit_given = true;
      break;
    case OPTION_TRACE:
      if (parser.argument[0] == '\0') {
        return cli_error_to(io->err, argv[0], "Invalid --trace value");
      }
      trace_path = parser.argument;
      break;
    case OPTION_TRACE_LIMIT:
      if (!parse_size(parser.argument, &trace_limit)) {
        return cli_error_to(io->err, argv[0], "Invalid --trace-limit value");
      }
      break;
    case OPTION_STATS:
      print_stats = true;
      break;
    case OPTION_SHAVE:
    case OPTION_COMPRESS:
      if (!pg_preprocess_parse_option(&preprocess_options,
                                      option == OPTION_SHAVE ? "initial-shave"
                                                             : "compress",
                                      parser.argument)) {
        return cli_error_to(io->err, argv[0], "Invalid preprocessing option");
      }
      break;
    case OPTION_ITER_SHAVE:
      lift_options.iter_shave = true;
      break;
    default:
      return cli_error_to(
          io->err, argv[0],
          "Unknown option or invalid argument; use --name=value");
    }
  }
  if (argc - parser.index > 1) {
    return cli_error_to(io->err, argv[0],
                        "Unknown option or invalid argument; use --name=value");
  }
  if (json && dot_options) {
    return cli_error_to(
        io->err, argv[0],
        "--format=json conflicts with --view, --labels and --max-set-items");
  }
  static char const *const names[] = {"lifting", "zielonka", "zielonka-sym",
                                      "lifting-sym"};
  bool const strahler_host = tree_k != 0 || adaptive_t;
  if ((strahler_host ? 1 : 0) + (arity != 0 ? 1 : 0) +
          (tree_path != nullptr ? 1 : 0) >
      1) {
    return cli_error_to(
        io->err, argv[0],
        "Use at most one of --tree-k or --adaptive-t, --kary, and --tree-file");
  }
  if (start_t_given && !adaptive_t) {
    return cli_error_to(io->err, argv[0], "--start-t requires --adaptive-t");
  }
  if ((algorithm == ALGORITHM_ZIELONKA ||
       algorithm == ALGORITHM_ZIELONKA_SYM) &&
      (strahler_host || start_t_given || arity != 0 || tree_path != nullptr ||
       host_limit_given || lift_options.iter_shave)) {
    return cli_error_to(
        io->err, argv[0],
        algorithm == ALGORITHM_ZIELONKA
            ? "Lifting options cannot be used with --algorithm=zielonka"
            : "Lifting options cannot be used with --algorithm=zielonka-sym");
  }
  if (algorithm == ALGORITHM_LIFTING_SYM && adaptive_t) {
    return cli_error_to(
        io->err, argv[0],
        "--adaptive-t cannot be used with --algorithm=lifting-sym");
  }
  lift_options.symmetric = algorithm == ALGORITHM_LIFTING_SYM;
  if (adaptive_t) {
    lift_options.mode = AD_LIFT_HOST_ADAPTIVE;
    lift_options.t = start_t;
    lift_options.k = tree_k;
  } else if (tree_k != 0) {
    lift_options.mode = AD_LIFT_HOST_STRAHLER;
    lift_options.k = tree_k;
  } else if (arity != 0) {
    lift_options.mode = AD_LIFT_HOST_KARY;
    lift_options.arity = arity;
  } else if (tree_path != nullptr) {
    lift_options.mode = AD_LIFT_HOST_TREE;
  }
  lift_options.verify = verify;

  int status = EXIT_SUCCESS;
  char const *failure_reason = "solver or verification failed";
  PGGame game = {0};
  PGPreprocessResult preprocessed = {0};
  ADResult result = {0};
  OrderedTreeNode *host_tree = nullptr;
  ADTraceJSON trace_writer = {.game = &game,
                              .limit = trace_limit,
                              .callback = io->event_callback,
                              .context = io->context};
  ADTrace observer = {.callback = ad_trace_json_event,
                      .context = &trace_writer};
#ifdef AD_TRACE_COUNT_ONLY
  TraceCount trace_count = {.writer = &trace_writer};
  trace_writer.limit = SIZE_MAX;
  observer.callback = count_trace_event;
  observer.context = &trace_count;
#endif
  ADTrace *trace = trace_path == nullptr && io->event_callback == nullptr
                       ? nullptr
                       : &observer;
  if (trace != nullptr && io->event_callback == nullptr) {
    trace_writer.stream =
        open_trace(trace_path,
                   parser.index < argc && strcmp(argv[parser.index], "-") != 0
                       ? argv[parser.index]
                       : nullptr,
                   tree_path);
    if (trace_writer.stream == nullptr) {
      fprintf(io->err, "Cannot open trace %s: %s\n", trace_path,
              strerror(errno));
      return EXIT_FAILURE;
    }
  }
  if (trace != nullptr) {
    if (!ad_trace_json_header(
            &trace_writer, STRAHLER_TREE_VERSION " (git " STRAHLER_TREE_GIT ")",
            names[algorithm], argc, argv)) {
      observer.failed = true;
      status = EXIT_FAILURE;
      goto cleanup;
    }
  }
  lift_options.trace = trace;
  if (tree_path != nullptr) {
    char *text = read_file(tree_path);
    if (text == nullptr) {
      fprintf(io->err, "Cannot read %s: %s\n", tree_path, strerror(errno));
      status = EXIT_IO;
      failure_reason = "cannot read host input";
      goto cleanup;
    }
    OrderedTreeError tree_error = {0};
    bool const parsed =
        ordered_tree_parse_leaf_stream(text, &host_tree, &tree_error);
    free(text);
    if (!parsed) {
      char message[strlen(tree_path) + sizeof(tree_error.message) + 64];
      snprintf(message, sizeof(message), "%s:%zu:%zu: %s", tree_path,
               tree_error.line, tree_error.column, tree_error.message);
      status = cli_error_to(io->err, argv[0], message);
      failure_reason = "invalid host input";
      goto cleanup;
    }
    lift_options.tree = host_tree;
  }

  if (io->game == nullptr) {
    FILE *input = io->in;
    bool close_input = false;
    if (parser.index < argc && strcmp(argv[parser.index], "-") != 0) {
      input = fopen(argv[parser.index], "rb");
      if (input == nullptr) {
        fprintf(io->err, "Cannot open %s: %s\n", argv[parser.index],
                strerror(errno));
        status = EXIT_IO;
        failure_reason = "cannot open game input";
        goto cleanup;
      }
      close_input = true;
    }

    PGParseError parse_error = {0};
    if (!pg_game_read(input, &game, &parse_error)) {
      fprintf(io->err, "%zu:%zu: %s\n", parse_error.line, parse_error.column,
              parse_error.message);
      if (close_input) {
        (void)fclose(input);
      }
      status = EXIT_GAME;
      failure_reason = "invalid game input";
      goto cleanup;
    }
    if (close_input && fclose(input) != 0) {
      fputs("Failed to close input file\n", io->err);
      status = EXIT_IO;
      failure_reason = "cannot close game input";
      goto cleanup;
    }

  } else if (!cli_game_copy(io->game, &game)) {
    status = EXIT_SOLVER;
    goto cleanup;
  }

  if (game.max_priority == UINT64_MAX) {
    fputs("Solver failed: the source maximum priority cannot be followed by "
          "an opposite-parity bound\n",
          io->err);
    status = EXIT_SOLVER;
    failure_reason = "source maximum priority has no opposite-parity bound";
    goto cleanup;
  }

  errno = 0;
  if (!pg_preprocess(&game, &preprocess_options, &preprocessed)) {
    fputs("Failed to preprocess the game\n", io->err);
    status = EXIT_SOLVER;
    goto cleanup;
  }
  if (verify && !pg_preprocess_verify(&game, &preprocessed)) {
    fputs("Verification failed: invalid shaved regions\n", io->err);
    status = EXIT_SOLVER;
    goto cleanup;
  }
  trace_writer.initial_won = preprocessed.won;
  if (observer.failed) {
    status = EXIT_FAILURE;
    goto cleanup;
  }
  AD_TRACE(trace, .kind = AD_TRACE_PHASE, .name = "preprocessed",
           .vertices = &preprocessed.active);
  if (observer.failed) {
    status = EXIT_FAILURE;
    goto cleanup;
  }
  PGSet const *domain = &preprocessed.active;
  if (print_stats &&
      (preprocess_options.shave != 0 || preprocess_options.compress)) {
    fprintf(io->err,
            "preprocess: residual=%zu shaved_even=%zu shaved_odd=%zu "
            "single=%zu loops=%zu wcwc=%zu\n",
            pg_set_count(domain), pg_set_count(&preprocessed.won[PG_EVEN]),
            pg_set_count(&preprocessed.won[PG_ODD]),
            preprocessed.shaved[PG_SHAVE_SINGLE][0] +
                preprocessed.shaved[PG_SHAVE_SINGLE][1],
            preprocessed.shaved[PG_SHAVE_LOOPS][0] +
                preprocessed.shaved[PG_SHAVE_LOOPS][1],
            preprocessed.shaved[PG_SHAVE_WCWC][0] +
                preprocessed.shaved[PG_SHAVE_WCWC][1]);
  }

  ADLiftSolveStats lift_stats = {0};
  if (algorithm == ALGORITHM_ZIELONKA) {
    ZielonkaError solver_error = {0};
    ZielonkaStats stats = {0};
    uint64_t maximum = 0;
    for (size_t vertex = pg_set_next(domain, 0); vertex != SIZE_MAX;
         vertex = pg_set_next(domain, vertex + 1)) {
      if (game.vertices[vertex].priority > maximum) {
        maximum = game.vertices[vertex].priority;
      }
    }
    if (!zielonka_decompose_trace(&game, domain, maximum, &result, &stats,
                                  &solver_error, trace)) {
      fprintf(io->err, "Solver failed: %s\n", solver_error.message);
      status = EXIT_SOLVER;
      goto cleanup;
    }
    if (print_stats) {
      write_zielonka_stats(io, &result, pg_set_count(domain), &stats);
    }
  } else if (algorithm == ALGORITHM_ZIELONKA_SYM) {
    ADSymStats stats = {0};
    ADLiftError error = {0};
    if (!ad_sym_solve_trace(&game, domain, nullptr, verify, &result, &stats,
                            &error, trace)) {
      fprintf(io->err, "Solver failed: %s\n", error.message);
      status = EXIT_SOLVER;
      goto cleanup;
    }
    if (print_stats) {
      write_sym_stats(io, "zielonka-sym", &stats, &result,
                      pg_set_count(domain));
    }
  } else {
    ADLiftError lift_error = {0};
    if (!ad_lift_solve(&game, domain, &lift_options, &result, &lift_stats,
                       &lift_error)) {
      fprintf(io->err, "Solver failed: %s\n", lift_error.message);
      if (lift_stats.budget_exhausted) {
        fputs("Use --adaptive-t, --tree-k, --kary, a larger "
              "--max-host-leaves, or --algorithm=zielonka\n",
              io->err);
      }
      status = EXIT_SOLVER;
      goto cleanup;
    }
    if (lift_stats.budget_exhausted) {
      fprintf(io->err, "Warning: %s; the result is partial\n",
              lift_error.message);
    } else if (lift_stats.vertices != 0 &&
               lift_options.mode == AD_LIFT_HOST_ADAPTIVE &&
               lift_stats.adaptive_complete) {
      fprintf(io->err,
              "Adaptive t: complete at t=%zu (Even complete at t=%zu, Odd "
              "complete at t=%zu)\n",
              lift_stats.first_complete_t,
              lift_stats.player[PG_EVEN].first_full_t,
              lift_stats.player[PG_ODD].first_full_t);
    } else if (lift_stats.vertices != 0 &&
               lift_options.mode == AD_LIFT_HOST_ADAPTIVE) {
      fprintf(io->err,
              "Adaptive t: incomplete at t=%zu with K capped at %zu; the "
              "result is partial\n",
              lift_stats.t_full, lift_options.k);
    }
    if (print_stats) {
      write_lifting_stats(io, &lift_stats, &lift_options, &result);
    }
  }

  if (verify) {
    ADVerifyError verify_error = {0};
    bool const verified =
        result.kind == AD_RESULT_COMPLETE
            ? ad_result_verify_complete(&game, domain, &result, &verify_error)
            : ad_result_verify_partial(&game, domain, &result, &verify_error);
    if (!verified) {
      fprintf(io->err, "Verification failed: %s\n", verify_error.message);
      status = EXIT_SOLVER;
      goto cleanup;
    }
    if (view == AD_DOT_VIEW_TREE_RELATIVE || view == AD_DOT_VIEW_JURDZINSKI) {
      for (size_t candidate = 0; candidate < 2; candidate++) {
        if (result.decomposition[candidate] != nullptr &&
            !ad_tree_relative_verify(&game, &result.region[candidate],
                                     result.decomposition[candidate],
                                     &verify_error)) {
          fprintf(io->err, "Tree-relative verification failed: %s\n",
                  verify_error.message);
          status = EXIT_SOLVER;
          goto cleanup;
        }
      }
    }
  }

  if (print_stats) {
    char const *winner = "unresolved";
    for (size_t candidate = 0; candidate < 2; candidate++) {
      bool won =
          pg_set_contains(&preprocessed.won[candidate], game.start_vertex) ||
          pg_set_contains(&result.region[candidate], game.start_vertex);
      for (size_t round = 0; round < result.shave_count; round++) {
        won = won || pg_set_contains(&result.shaves[round].attracted[candidate],
                                     game.start_vertex);
      }
      if (won) {
        winner = candidate == PG_EVEN ? "even" : "odd";
      }
    }
    fprintf(io->err, "start: vertex=%" PRIu64 " winner=%s\n", game.start_id,
            winner);
  }

  if (observer.failed) {
    status = EXIT_FAILURE;
    goto cleanup;
  }

  PGPriorityMap const *display_map = priority_mode == PRIORITY_MODE_ORIGINAL
                                         ? &preprocessed.priority_map
                                         : nullptr;
  bool wrote = false;
  if (json) {
    fputc('{', io->out);
    json_header(io->out, "strahler-decomp/1",
                STRAHLER_TREE_VERSION " (git " STRAHLER_TREE_GIT ")",
                names[algorithm], argc, argv);
    fputs(",\"game\":", io->out);
    json_game(io->out, &game, &preprocessed.priority_map);
    fputs(",\"initial_won\":[", io->out);
    JSONWriter const writer = {io->out, &game};
    json_set(&writer, preprocessed.won);
    fputc(',', io->out);
    json_set(&writer, preprocessed.won + 1);
    fputs("],", io->out);
    wrote = ad_json_result_fields(&writer, &result, player);
    if (wrote) {
      fputs(",\"metrics\":", io->out);
      wrote = ad_json_metrics(io->out, &result, player);
      fputs(",\"parameters\":[", io->out);
      for (size_t candidate = 0; candidate < 2; candidate++) {
        fputs(candidate == 0 ? "{" : ",{", io->out);
        if (lift_stats.vertices != 0) {
          ADLiftPlayerStats const *current = lift_stats.player + candidate;
          fprintf(io->out, "\"H\":%zu", current->height);
          if (lift_options.mode == AD_LIFT_HOST_KARY) {
            fprintf(io->out, ",\"arity\":%zu", current->arity);
          } else if (lift_options.mode != AD_LIFT_HOST_TREE) {
            fprintf(io->out, ",\"K\":%zu,\"t\":%zu", current->k, current->t);
          }
        }
        fputc('}', io->out);
      }
      wrote = fprintf(io->out, "],\"budget_exhausted\":%s}\n",
                      lift_stats.budget_exhausted ? "true" : "false") >= 0 &&
              wrote;
    }
  } else {
    wrote = ad_tree_write_dot(io->out, &game, &result, player, view, labels,
                              max_set_items, display_map, preprocessed.won);
  }
  if (!wrote) {
    fputs(json ? "Failed to write JSON output\n"
               : "Failed to write DOT output\n",
          io->err);
    status = EXIT_IO;
    failure_reason =
        json ? "cannot write JSON output" : "cannot write DOT output";
  }
  if (fflush(io->out) != 0 || fflush(io->err) != 0 || ferror(io->out) ||
      ferror(io->err)) {
    status = EXIT_IO;
    failure_reason = "cannot flush output streams";
  }

cleanup:
  if (trace != nullptr) {
    if (observer.failed) {
      status = EXIT_FAILURE;
      failure_reason = "trace observer failed";
    }
    if (status != EXIT_SUCCESS && !trace_writer.truncated &&
        (trace_writer.stream == nullptr || !ferror(trace_writer.stream)) &&
        !ad_trace_json_incomplete(&trace_writer, observer.step + 1,
                                  failure_reason, status)) {
      observer.failed = true;
    }
    if (trace_writer.stream != nullptr && fclose(trace_writer.stream) != 0) {
      observer.failed = true;
    }
    if (observer.failed) {
      fprintf(io->err, "%s\n",
              trace_writer.truncated
                  ? "Trace event limit exceeded; trace ends with truncated"
                  : "Trace observer failed");
      status = EXIT_FAILURE;
    }
  }
  // Boolean solver/output APIs propagate allocation failure through errno.
  if (status != EXIT_SUCCESS && errno == ENOMEM) {
    fputs("Failed to allocate memory\n", io->err);
  }
  ad_result_destroy(&result);
  pg_preprocess_destroy(&preprocessed);
  if (io->game == nullptr) {
    pg_game_destroy(&game);
  } else {
    free(game.vertices);
  }
  ordered_tree_destroy(host_tree);
  return status;
}
