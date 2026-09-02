#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_version.h"
#include "strahler_tree_version.h"

int cli_handle_version_argument(int argc, char const *program_path,
                                char const *argument) {
  if (argc != 2 || argument == nullptr || strcmp(argument, "--version") != 0) {
    return CLI_VERSION_NOT_REQUESTED;
  }

  char const *program = program_path == nullptr ? "utility" : program_path;
  char const *slash = strrchr(program, '/');
  if (slash != nullptr) {
    program = slash + 1;
  }
  return printf("%s %s\n", program, STRAHLER_TREE_VERSION) >= 0 ? EXIT_SUCCESS
                                                                : EXIT_FAILURE;
}
