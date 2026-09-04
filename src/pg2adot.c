#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_tree_dot.h"
#include "cli_version.h"
#include "zielonka.h"

enum {
  EXIT_USAGE = 2,
  EXIT_GAME = 3,
  EXIT_SOLVER = 4,
  EXIT_IO = 5,
};

typedef enum {
  PRIORITY_MODE_ORIGINAL,
  PRIORITY_MODE_COMPACT,
} PriorityMode;

static void usage(FILE *out, char *argv[static 1]) {
  char *program = strrchr(argv[0], '/');
  program = program == nullptr ? argv[0] : program + 1;
  fprintf(out,
          "Usage: %s [OPTIONS] [FILE]\n"
          "  -h, --help\n"
          "  --version                    print the program version\n"
          "  --player=both|even|odd       default: both\n"
          "  --view=classic|tree-relative default: classic\n"
          "  --labels=counts|sets|none    default: counts\n"
          "  --max-set-items=N            default: 32\n"
          "  --priority-mode=original|compact\n"
          "                               default: original\n"
          "  --no-verify\n",
          program);
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
  };
  static struct option const options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"player", required_argument, nullptr, OPTION_PLAYER},
      {"view", required_argument, nullptr, OPTION_VIEW},
      {"labels", required_argument, nullptr, OPTION_LABELS},
      {"max-set-items", required_argument, nullptr, OPTION_MAX_ITEMS},
      {"priority-mode", required_argument, nullptr, OPTION_PRIORITY_MODE},
      {"no-verify", no_argument, nullptr, OPTION_NO_VERIFY},
      {nullptr, 0, nullptr, 0},
  };

  ADDotPlayer player = AD_DOT_PLAYER_BOTH;
  ADDotView view = AD_DOT_VIEW_CLASSIC;
  ADDotLabels labels = AD_DOT_LABEL_COUNTS;
  size_t max_set_items = 32;
  PriorityMode priority_mode = PRIORITY_MODE_ORIGINAL;
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
    default:
      usage(stderr, argv);
      return EXIT_USAGE;
    }
  }
  if (argc - optind > 1) {
    usage(stderr, argv);
    return EXIT_USAGE;
  }

  FILE *input = stdin;
  bool close_input = false;
  if (optind < argc && strcmp(argv[optind], "-") != 0) {
    input = fopen(argv[optind], "rb");
    if (input == nullptr) {
      fprintf(stderr, "Cannot open %s: %s\n", argv[optind], strerror(errno));
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
    return EXIT_GAME;
  }
  if (close_input && fclose(input) != 0) {
    pg_game_destroy(&game);
    fputs("Failed to close input file\n", stderr);
    return EXIT_IO;
  }

  if (game.max_priority == UINT64_MAX) {
    pg_game_destroy(&game);
    fputs("Solver failed: the source maximum priority cannot be followed by "
          "an opposite-parity bound\n",
          stderr);
    return EXIT_SOLVER;
  }

  PGPriorityMap priority_map = {0};
  if (!pg_priority_map_build(&game, &priority_map) ||
      !pg_priority_map_apply(&priority_map, &game)) {
    pg_priority_map_destroy(&priority_map);
    pg_game_destroy(&game);
    fputs("Failed to compact the game priorities\n", stderr);
    return EXIT_SOLVER;
  }

  PGSet domain = {0};
  ZielonkaResult result = {0};
  ZielonkaError solver_error = {0};
  if (!pg_set_init(&domain, game.vertex_count)) {
    (void)pg_priority_map_restore(&priority_map, &game);
    pg_priority_map_destroy(&priority_map);
    pg_game_destroy(&game);
    fputs("Failed to allocate the game domain\n", stderr);
    return EXIT_SOLVER;
  }
  pg_set_fill(&domain);
  if (!zielonka_decompose(&game, &domain, game.max_priority, &result,
                          &solver_error)) {
    fprintf(stderr, "Solver failed: %s\n", solver_error.message);
    pg_set_destroy(&domain);
    (void)pg_priority_map_restore(&priority_map, &game);
    pg_priority_map_destroy(&priority_map);
    pg_game_destroy(&game);
    return EXIT_SOLVER;
  }

  if (verify) {
    ADVerifyError verify_error = {0};
    if (!zielonka_result_verify(&game, &domain, &result, &verify_error)) {
      fprintf(stderr, "Verification failed: %s\n", verify_error.message);
      zielonka_result_destroy(&result);
      pg_set_destroy(&domain);
      (void)pg_priority_map_restore(&priority_map, &game);
      pg_priority_map_destroy(&priority_map);
      pg_game_destroy(&game);
      return EXIT_SOLVER;
    }
    if (view == AD_DOT_VIEW_TREE_RELATIVE) {
      for (size_t candidate = 0; candidate < 2; candidate++) {
        if (result.decomposition[candidate] != nullptr &&
            !ad_tree_relative_verify(&game, &result.winning[candidate],
                                     result.decomposition[candidate],
                                     &verify_error)) {
          fprintf(stderr, "Tree-relative verification failed: %s\n",
                  verify_error.message);
          zielonka_result_destroy(&result);
          pg_set_destroy(&domain);
          (void)pg_priority_map_restore(&priority_map, &game);
          pg_priority_map_destroy(&priority_map);
          pg_game_destroy(&game);
          return EXIT_SOLVER;
        }
      }
    }
  }

  PGPriorityMap const *display_map =
      priority_mode == PRIORITY_MODE_ORIGINAL ? &priority_map : nullptr;
  bool const wrote = ad_tree_write_dot(stdout, &game, &result, player, view,
                                       labels, max_set_items, display_map);
  bool const restored = pg_priority_map_restore(&priority_map, &game);
  zielonka_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_priority_map_destroy(&priority_map);
  pg_game_destroy(&game);
  if (!restored) {
    fputs("Failed to restore the source priorities\n", stderr);
    return EXIT_SOLVER;
  }
  if (!wrote) {
    fputs("Failed to write DOT output\n", stderr);
    return EXIT_IO;
  }
  return EXIT_SUCCESS;
}
