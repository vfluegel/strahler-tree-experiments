#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_tree_dot.h"
#include "zielonka.h"

[[nodiscard]] static char *render(PGGame const *game,
                                  ZielonkaResult const *result,
                                  ADDotPlayer const player,
                                  ADDotLabels const labels,
                                  size_t const max_items) {
  char *buffer = nullptr;
  size_t length = 0;
  FILE *stream = open_memstream(&buffer, &length);
  assert(stream != nullptr);
  assert(ad_tree_write_dot(stream, game, result, player, labels, max_items));
  assert(fclose(stream) == 0);
  assert(buffer != nullptr && length == strlen(buffer));
  return buffer;
}

int main(void) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  char const *source = "10 0 0 10;\n1000 1 1 1000;\n";
  assert(fwrite(source, 1, strlen(source), stream) == strlen(source));
  assert(fseek(stream, 0, SEEK_SET) == 0);
  PGGame game = {0};
  PGParseError parse_error = {0};
  assert(pg_game_read(stream, &game, &parse_error));
  assert(fclose(stream) == 0);

  PGSet domain = {0};
  assert(pg_set_init(&domain, game.vertex_count));
  pg_set_fill(&domain);
  ZielonkaResult result = {0};
  ZielonkaError error = {0};
  assert(
      zielonka_decompose(&game, &domain, game.max_priority, &result, &error));

  char *first =
      render(&game, &result, AD_DOT_PLAYER_BOTH, AD_DOT_LABEL_SETS, 32);
  char *second =
      render(&game, &result, AD_DOT_PLAYER_BOTH, AD_DOT_LABEL_SETS, 32);
  assert(strcmp(first, second) == 0);
  assert(strstr(first, "W={10}") != nullptr);
  assert(strstr(first, "W={1000}") != nullptr);
  free(second);
  free(first);

  char *none = render(&game, &result, AD_DOT_PLAYER_EVEN, AD_DOT_LABEL_NONE, 0);
  assert(strstr(none, "even [label=\"Even d=2\"]") != nullptr);
  assert(strstr(none, "result (synthetic)") == nullptr);
  free(none);

  assert(!ad_tree_write_dot(nullptr, &game, &result, AD_DOT_PLAYER_BOTH,
                            AD_DOT_LABEL_COUNTS, 1));
  zielonka_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_game_destroy(&game);
  return 0;
}
