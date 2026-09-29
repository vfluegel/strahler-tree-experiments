#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_lift_solve.h"
#include "ad_tree_dot.h"
#include "cli_version.h"
#include "zielonka.h"

enum {
  EXIT_USAGE = 2,
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
} Algorithm;

static void usage(FILE *out, char *argv[static 1]) {
  char *program = strrchr(argv[0], '/');
  program = program == nullptr ? argv[0] : program + 1;
  fprintf(
      out,
      "Usage: %s [OPTIONS] [FILE]\n"
      "  -h, --help\n"
      "  --version                    print the program version\n"
      "  --player=both|even|odd       default: both\n"
      "  --view=classic|tree-relative|jurdzinski\n"
      "                               default: classic\n"
      "  --labels=counts|sets|none    default: counts\n"
      "  --max-set-items=N            default: 32\n"
      "  --priority-mode=original|compact\n"
      "                               default: original\n"
      "  --algorithm=lifting|zielonka default: lifting\n"
      "  --tree-k=K                   lift in U^K_{t,H}; may be partial\n"
      "  --adaptive-t                 raise t until the result is complete;\n"
      "                               --tree-k=K caps K\n"
      "  --start-t=T                  first t for --adaptive-t; default: 0\n"
      "  --kary=A                     lift in the full A-ary tree of height "
      "H;\n"
      "                               may be partial\n"
      "  --tree-file=FILE             lift in a leaf-stream host tree; may "
      "be partial\n"
      "  --max-host-leaves=N          default: %d\n"
      "  --stats                      print statistics to stderr\n"
      "  --no-verify\n",
      program, DEFAULT_MAX_HOST_LEAVES);
}

[[nodiscard]] static bool parse_size(char const *text, size_t *value) {
  if (text == nullptr || text[0] == '-' || text[0] == '\0') {
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
  while (true) {
    if (capacity - length < 2) {
      size_t const grown = capacity == 0 ? 4096 : capacity * 2;
      char *resized = grown < capacity ? nullptr : realloc(text, grown);
      if (resized == nullptr) {
        free(text);
        (void)fclose(stream);
        return nullptr;
      }
      text = resized;
      capacity = grown;
    }
    size_t const read = fread(text + length, 1, capacity - length - 1, stream);
    length += read;
    if (read == 0) {
      break;
    }
  }
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

static void write_tree_stats(ADResult const *result, size_t const player,
                             ADTreeMetrics const metrics) {
  fprintf(stderr,
          "%s=%zu tree_nodes=%zu tree_leaves=%zu tree_height=%zu "
          "strahler=%zu\n",
          result->kind == AD_RESULT_COMPLETE ? "region" : "dominion",
          pg_set_count(&result->region[player]), metrics.nodes, metrics.leaves,
          metrics.height, metrics.strahler);
}

static void write_zielonka_stats(ADResult const *result,
                                 size_t const vertices) {
  fprintf(stderr, "zielonka: vertices=%zu result=%s\n", vertices,
          result_kind(result));
  for (size_t player = 0; player < 2; player++) {
    fprintf(stderr, "zielonka %s: ", player == PG_EVEN ? "even" : "odd");
    write_tree_stats(result, player,
                     ad_tree_metrics(result->decomposition[player]));
  }
}

static void write_lifting_stats(ADLiftSolveStats const *stats,
                                ADLiftOptions const *options,
                                ADResult const *result) {
  fprintf(stderr, "lifting: vertices=%zu t_full=%zu result=%s\n",
          stats->vertices, stats->t_full, result_kind(result));
  if (options->mode == AD_LIFT_HOST_ADAPTIVE) {
    fputs("lifting: first_complete_t=", stderr);
    if (stats->adaptive_complete) {
      fprintf(stderr, "%zu even_first_full_t=%zu odd_first_full_t=%zu",
              stats->first_complete_t, stats->player[PG_EVEN].first_full_t,
              stats->player[PG_ODD].first_full_t);
    } else {
      fputs("none", stderr);
    }
    fprintf(stderr, " budget_exhausted=%s\n",
            stats->budget_exhausted ? "yes" : "no");
  }
  if (stats->vertices == 0) {
    return;
  }
  for (size_t player = 0; player < 2; player++) {
    ADLiftPlayerStats const *current = stats->player + player;
    fprintf(stderr, "lifting %s: root_level=%" PRIu64 " H=%zu k_full=%zu ",
            player == PG_EVEN ? "even" : "odd", current->root_level,
            current->height, current->k_full);
    if (options->mode == AD_LIFT_HOST_TREE) {
      fputs("host=file", stderr);
    } else if (options->mode == AD_LIFT_HOST_KARY) {
      fprintf(stderr, "host=kary arity=%zu", current->arity);
    } else {
      fprintf(stderr, "host=strahler K=%zu t=%zu", current->k, current->t);
    }
    fprintf(stderr,
            " host_nodes=%zu host_leaves=%zu positions=%zu rounds=%zu "
            "lifts=%zu top=%zu ",
            current->host_nodes, current->host_leaves, current->host_positions,
            current->lift.rounds, current->lift.lifts, current->lift.top_count);
    write_tree_stats(result, player, current->tree);
  }
}

int main(int argc, char *argv[argc + 1]) {
  int const version_status =
      cli_handle_version_argument(argc, argv[0], argc > 1 ? argv[1] : nullptr);
  if (version_status != CLI_VERSION_NOT_REQUESTED) {
    return version_status;
  }

  enum {
    OPTION_PLAYER = 1,
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
  };
  static struct option const options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"player", required_argument, nullptr, OPTION_PLAYER},
      {"view", required_argument, nullptr, OPTION_VIEW},
      {"labels", required_argument, nullptr, OPTION_LABELS},
      {"max-set-items", required_argument, nullptr, OPTION_MAX_ITEMS},
      {"priority-mode", required_argument, nullptr, OPTION_PRIORITY_MODE},
      {"no-verify", no_argument, nullptr, OPTION_NO_VERIFY},
      {"algorithm", required_argument, nullptr, OPTION_ALGORITHM},
      {"tree-k", required_argument, nullptr, OPTION_TREE_K},
      {"adaptive-t", no_argument, nullptr, OPTION_ADAPTIVE_T},
      {"start-t", required_argument, nullptr, OPTION_START_T},
      {"kary", required_argument, nullptr, OPTION_KARY},
      {"tree-file", required_argument, nullptr, OPTION_TREE_FILE},
      {"max-host-leaves", required_argument, nullptr, OPTION_MAX_HOST_LEAVES},
      {"stats", no_argument, nullptr, OPTION_STATS},
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
  bool verify = true;
  opterr = 0;
  int option = 0;
  while ((option = getopt_long(argc, argv, "h", options, nullptr)) != -1) {
    switch (option) {
    case 'h':
      usage(stdout, argv);
      return EXIT_SUCCESS;
    case OPTION_PLAYER:
      if (strcmp(optarg, "both") == 0) {
        player = AD_DOT_PLAYER_BOTH;
      } else if (strcmp(optarg, "even") == 0) {
        player = AD_DOT_PLAYER_EVEN;
      } else if (strcmp(optarg, "odd") == 0) {
        player = AD_DOT_PLAYER_ODD;
      } else {
        fputs("Invalid --player value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_VIEW:
      if (strcmp(optarg, "classic") == 0) {
        view = AD_DOT_VIEW_CLASSIC;
      } else if (strcmp(optarg, "tree-relative") == 0) {
        view = AD_DOT_VIEW_TREE_RELATIVE;
      } else if (strcmp(optarg, "jurdzinski") == 0) {
        view = AD_DOT_VIEW_JURDZINSKI;
      } else {
        fputs("Invalid --view value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_LABELS:
      if (strcmp(optarg, "counts") == 0) {
        labels = AD_DOT_LABEL_COUNTS;
      } else if (strcmp(optarg, "sets") == 0) {
        labels = AD_DOT_LABEL_SETS;
      } else if (strcmp(optarg, "none") == 0) {
        labels = AD_DOT_LABEL_NONE;
      } else {
        fputs("Invalid --labels value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_MAX_ITEMS:
      if (!parse_size(optarg, &max_set_items)) {
        fputs("Invalid --max-set-items value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_PRIORITY_MODE:
      if (strcmp(optarg, "original") == 0) {
        priority_mode = PRIORITY_MODE_ORIGINAL;
      } else if (strcmp(optarg, "compact") == 0) {
        priority_mode = PRIORITY_MODE_COMPACT;
      } else {
        fputs("Invalid --priority-mode value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_NO_VERIFY:
      verify = false;
      break;
    case OPTION_ALGORITHM:
      if (strcmp(optarg, "lifting") == 0) {
        algorithm = ALGORITHM_LIFTING;
      } else if (strcmp(optarg, "zielonka") == 0) {
        algorithm = ALGORITHM_ZIELONKA;
      } else {
        fputs("Invalid --algorithm value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_TREE_K:
      if (!parse_size(optarg, &tree_k) || tree_k == 0) {
        fputs("Invalid --tree-k value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_ADAPTIVE_T:
      adaptive_t = true;
      break;
    case OPTION_START_T:
      if (!parse_size(optarg, &start_t)) {
        fputs("Invalid --start-t value\n", stderr);
        return EXIT_USAGE;
      }
      start_t_given = true;
      break;
    case OPTION_KARY:
      if (!parse_size(optarg, &arity) || arity == 0) {
        fputs("Invalid --kary value\n", stderr);
        return EXIT_USAGE;
      }
      break;
    case OPTION_TREE_FILE:
      tree_path = optarg;
      break;
    case OPTION_MAX_HOST_LEAVES:
      if (!parse_size(optarg, &lift_options.max_host_leaves)) {
        fputs("Invalid --max-host-leaves value\n", stderr);
        return EXIT_USAGE;
      }
      host_limit_given = true;
      break;
    case OPTION_STATS:
      print_stats = true;
      break;
    default:
      usage(stderr, argv);
      return EXIT_USAGE;
    }
  }
  if (argc - optind > 1) {
    usage(stderr, argv);
    return EXIT_USAGE;
  }
  bool const strahler_host = tree_k != 0 || adaptive_t;
  if ((strahler_host ? 1 : 0) + (arity != 0 ? 1 : 0) +
          (tree_path != nullptr ? 1 : 0) >
      1) {
    fputs("Use at most one of --tree-k or --adaptive-t, --kary, and "
          "--tree-file\n",
          stderr);
    return EXIT_USAGE;
  }
  if (start_t_given && !adaptive_t) {
    fputs("--start-t requires --adaptive-t\n", stderr);
    return EXIT_USAGE;
  }
  if (algorithm == ALGORITHM_ZIELONKA &&
      (strahler_host || start_t_given || arity != 0 || tree_path != nullptr ||
       host_limit_given)) {
    fputs("Lifting options cannot be used with --algorithm=zielonka\n", stderr);
    return EXIT_USAGE;
  }
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

  OrderedTreeNode *host_tree = nullptr;
  if (tree_path != nullptr) {
    char *text = read_file(tree_path);
    if (text == nullptr) {
      fprintf(stderr, "Cannot read %s\n", tree_path);
      return EXIT_IO;
    }
    OrderedTreeError tree_error = {0};
    bool const parsed =
        ordered_tree_parse_leaf_stream(text, &host_tree, &tree_error);
    free(text);
    if (!parsed) {
      fprintf(stderr, "%s:%zu:%zu: %s\n", tree_path, tree_error.line,
              tree_error.column, tree_error.message);
      return EXIT_USAGE;
    }
    lift_options.tree = host_tree;
  }

  int status = EXIT_SUCCESS;
  FILE *input = stdin;
  bool close_input = false;
  if (optind < argc && strcmp(argv[optind], "-") != 0) {
    input = fopen(argv[optind], "rb");
    if (input == nullptr) {
      fprintf(stderr, "Cannot open %s: %s\n", argv[optind], strerror(errno));
      ordered_tree_destroy(host_tree);
      return EXIT_IO;
    }
    close_input = true;
  }

  PGGame game = {0};
  PGParseError parse_error = {0};
  if (!pg_game_read(input, &game, &parse_error)) {
    fprintf(stderr, "%zu:%zu: %s\n", parse_error.line, parse_error.column,
            parse_error.message);
    if (close_input) {
      (void)fclose(input);
    }
    ordered_tree_destroy(host_tree);
    return EXIT_GAME;
  }
  if (close_input && fclose(input) != 0) {
    pg_game_destroy(&game);
    ordered_tree_destroy(host_tree);
    fputs("Failed to close input file\n", stderr);
    return EXIT_IO;
  }

  if (game.max_priority == UINT64_MAX) {
    pg_game_destroy(&game);
    ordered_tree_destroy(host_tree);
    fputs("Solver failed: the source maximum priority cannot be followed by "
          "an opposite-parity bound\n",
          stderr);
    return EXIT_SOLVER;
  }

  PGPriorityMap priority_map = {0};
  PGSet domain = {0};
  ADResult result = {0};
  if (!pg_priority_map_build(&game, &priority_map) ||
      !pg_priority_map_apply(&priority_map, &game)) {
    fputs("Failed to compact the game priorities\n", stderr);
    status = EXIT_SOLVER;
    goto cleanup;
  }
  if (!pg_set_init(&domain, game.vertex_count)) {
    fputs("Failed to allocate the game domain\n", stderr);
    status = EXIT_SOLVER;
    goto cleanup;
  }
  pg_set_fill(&domain);

  if (algorithm == ALGORITHM_ZIELONKA) {
    ZielonkaError solver_error = {0};
    if (!zielonka_decompose(&game, &domain, game.max_priority, &result,
                            &solver_error)) {
      fprintf(stderr, "Solver failed: %s\n", solver_error.message);
      status = EXIT_SOLVER;
      goto cleanup;
    }
    if (print_stats) {
      write_zielonka_stats(&result, game.vertex_count);
    }
  } else {
    ADLiftSolveStats stats = {0};
    ADLiftError lift_error = {0};
    if (!ad_lift_solve(&game, &domain, &lift_options, &result, &stats,
                       &lift_error)) {
      fprintf(stderr, "Solver failed: %s\n", lift_error.message);
      if (stats.budget_exhausted) {
        fputs("Use --adaptive-t, --tree-k, --kary, a larger "
              "--max-host-leaves, or --algorithm=zielonka\n",
              stderr);
      }
      status = EXIT_SOLVER;
      goto cleanup;
    }
    if (stats.budget_exhausted) {
      fprintf(stderr, "Warning: %s; the result is partial\n",
              lift_error.message);
    } else if (lift_options.mode == AD_LIFT_HOST_ADAPTIVE &&
               stats.adaptive_complete) {
      fprintf(stderr,
              "Adaptive t: complete at t=%zu (Even complete at t=%zu, Odd "
              "complete at t=%zu)\n",
              stats.first_complete_t, stats.player[PG_EVEN].first_full_t,
              stats.player[PG_ODD].first_full_t);
    } else if (lift_options.mode == AD_LIFT_HOST_ADAPTIVE) {
      fprintf(stderr,
              "Adaptive t: incomplete at t=%zu with K capped at %zu; the "
              "result is partial\n",
              stats.t_full, lift_options.k);
    }
    if (print_stats) {
      write_lifting_stats(&stats, &lift_options, &result);
    }
  }

  if (verify) {
    ADVerifyError verify_error = {0};
    bool const verified =
        result.kind == AD_RESULT_COMPLETE
            ? ad_result_verify_complete(&game, &domain, &result, &verify_error)
            : ad_result_verify_partial(&game, &domain, &result, &verify_error);
    if (!verified) {
      fprintf(stderr, "Verification failed: %s\n", verify_error.message);
      status = EXIT_SOLVER;
      goto cleanup;
    }
    if (view == AD_DOT_VIEW_TREE_RELATIVE || view == AD_DOT_VIEW_JURDZINSKI) {
      for (size_t candidate = 0; candidate < 2; candidate++) {
        if (result.decomposition[candidate] != nullptr &&
            !ad_tree_relative_verify(&game, &result.region[candidate],
                                     result.decomposition[candidate],
                                     &verify_error)) {
          fprintf(stderr, "Tree-relative verification failed: %s\n",
                  verify_error.message);
          status = EXIT_SOLVER;
          goto cleanup;
        }
      }
    }
  }

  PGPriorityMap const *display_map =
      priority_mode == PRIORITY_MODE_ORIGINAL ? &priority_map : nullptr;
  if (!ad_tree_write_dot(stdout, &game, &result, player, view, labels,
                         max_set_items, display_map)) {
    fputs("Failed to write DOT output\n", stderr);
    status = EXIT_IO;
  }

cleanup:
  if (priority_map.entries != nullptr &&
      !pg_priority_map_restore(&priority_map, &game) &&
      status == EXIT_SUCCESS) {
    fputs("Failed to restore the source priorities\n", stderr);
    status = EXIT_SOLVER;
  }
  ad_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_priority_map_destroy(&priority_map);
  pg_game_destroy(&game);
  ordered_tree_destroy(host_tree);
  return status;
}
