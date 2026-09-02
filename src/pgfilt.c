#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_version.h"
#include "pg_game.h"

enum { USAGE_ERROR = 2 };

static void print_usage(FILE *out, char *argv[static 1]) {
  char *program = strrchr(argv[0], '/');
  program = program == nullptr ? argv[0] : program + 1;
  fprintf(out, "Usage: %s [--normalize]\n", program);
  fputs("Read one PGSolver game from standard input.\n", out);
  fputs("  -h, --help       Print this message\n", out);
  fputs("  -n, --normalize  Write canonical PGSolver format\n", out);
  fputs("  --version         Print the program version\n", out);
}

int main(int argc, char *argv[argc + 1]) {
  int const version_status =
      cli_handle_version_argument(argc, argv[0], argc > 1 ? argv[1] : nullptr);
  if (version_status != CLI_VERSION_NOT_REQUESTED) {
    return version_status;
  }

  static struct option const options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"normalize", no_argument, nullptr, 'n'},
      {nullptr, 0, nullptr, 0},
  };

  bool normalize = false;
  opterr = 0;
  int option = 0;
  while ((option = getopt_long(argc, argv, "hn", options, nullptr)) != -1) {
    switch (option) {
    case 'h':
      print_usage(stdout, argv);
      return EXIT_SUCCESS;
    case 'n':
      normalize = true;
      break;
    default:
      print_usage(stderr, argv);
      return USAGE_ERROR;
    }
  }
  if (optind != argc) {
    print_usage(stderr, argv);
    return USAGE_ERROR;
  }

  PGGame game = {0};
  PGParseError error = {0};
  if (!pg_game_read(stdin, &game, &error)) {
    fprintf(stderr, "%zu:%zu: %s\n", error.line, error.column, error.message);
    return EXIT_FAILURE;
  }

  bool succeeded = true;
  if (normalize) {
    succeeded = pg_game_write_pgsolver(stdout, &game, true);
  } else {
    size_t owners[2] = {0};
    for (size_t index = 0; index < game.vertex_count; index++) {
      owners[game.vertices[index].owner]++;
    }
    uint64_t const minimum_id = game.vertices[0].external_id;
    uint64_t const maximum_id =
        game.vertices[game.vertex_count - 1].external_id;
    succeeded =
        printf("vertices: %zu\n", game.vertex_count) >= 0 &&
        printf("edges: %zu\n", game.edge_count) >= 0 &&
        printf("max-priority: %" PRIu64 "\n", game.max_priority) >= 0 &&
        printf("owners: %zu %zu\n", owners[PG_EVEN], owners[PG_ODD]) >= 0 &&
        printf("external-id-range: %" PRIu64 "..%" PRIu64 "\n", minimum_id,
               maximum_id) >= 0;
  }
  pg_game_destroy(&game);
  if (!succeeded) {
    fputs("Failed to write output\n", stderr);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
