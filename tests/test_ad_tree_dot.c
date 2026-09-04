#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad_tree_dot.h"
#include "zielonka.h"

[[nodiscard]] static char *
render(PGGame const *game, ZielonkaResult const *result,
       ADDotPlayer const player, ADDotView const view, ADDotLabels const labels,
       size_t const max_items, PGPriorityMap const *priority_map) {
  char *buffer = nullptr;
  size_t length = 0;
  FILE *stream = open_memstream(&buffer, &length);
  assert(stream != nullptr);
  assert(ad_tree_write_dot(stream, game, result, player, view, labels,
                           max_items, priority_map));
  assert(fclose(stream) == 0);
  assert(buffer != nullptr && length == strlen(buffer));
  return buffer;
}

int main(void) {
  FILE *stream = tmpfile();
  assert(stream != nullptr);
  char const *source = "10 9 1 10;\n1000 100 0 1000;\n";
  assert(fwrite(source, 1, strlen(source), stream) == strlen(source));
  assert(fseek(stream, 0, SEEK_SET) == 0);
  PGGame game = {0};
  PGParseError parse_error = {0};
  assert(pg_game_read(stream, &game, &parse_error));
  assert(fclose(stream) == 0);

  PGPriorityMap priority_map = {0};
  assert(pg_priority_map_build(&game, &priority_map));
  assert(pg_priority_map_apply(&priority_map, &game));

  PGSet domain = {0};
  assert(pg_set_init(&domain, game.vertex_count));
  pg_set_fill(&domain);
  ZielonkaResult result = {0};
  ZielonkaError error = {0};
  assert(
      zielonka_decompose(&game, &domain, game.max_priority, &result, &error));
  ADVerifyError verify_error = {0};
  assert(zielonka_result_verify(&game, &domain, &result, &verify_error));
  for (size_t player = 0; player < 2; player++) {
    if (result.decomposition[player] != nullptr) {
      assert(ad_tree_relative_verify(&game, &result.winning[player],
                                     result.decomposition[player],
                                     &verify_error));
    }
  }

  char *first = render(&game, &result, AD_DOT_PLAYER_BOTH, AD_DOT_VIEW_CLASSIC,
                       AD_DOT_LABEL_SETS, 32, &priority_map);
  char *second = render(&game, &result, AD_DOT_PLAYER_BOTH, AD_DOT_VIEW_CLASSIC,
                        AD_DOT_LABEL_SETS, 32, &priority_map);
  assert(strcmp(first, second) == 0);
  assert(strstr(first, "<I>W</I> = {10}") != nullptr);
  assert(strstr(first, "<I>W</I> = {1000}") != nullptr);
  assert(strstr(first, "<B>Even</B> <I>d</I> = 100") != nullptr);
  assert(strstr(first, "<B>Odd</B> <I>d</I> = 101") != nullptr);
  assert(strstr(first, "<B>Odd</B> <I>d</I> = 9") != nullptr);
  free(second);
  free(first);

  char *relative =
      render(&game, &result, AD_DOT_PLAYER_BOTH, AD_DOT_VIEW_TREE_RELATIVE,
             AD_DOT_LABEL_SETS, 1, &priority_map);
  char *relative_again =
      render(&game, &result, AD_DOT_PLAYER_BOTH, AD_DOT_VIEW_TREE_RELATIVE,
             AD_DOT_LABEL_SETS, 1, &priority_map);
  assert(strcmp(relative, relative_again) == 0);
  assert(strstr(relative, "<I>V</I> = {10}") != nullptr);
  assert(strstr(relative, "<I>H</I> = {10}") != nullptr);
  assert(strstr(relative, "<I>T</I> = {}") != nullptr);
  assert(strstr(relative, "<I>S</I> = {}") != nullptr);
  assert(strstr(relative, "<B>Even</B> <I>d</I> = 100") != nullptr);
  assert(strstr(relative, "<B>Odd</B> <I>d</I> = 101") != nullptr);
  free(relative_again);
  free(relative);

  char *none = render(&game, &result, AD_DOT_PLAYER_EVEN,
                      AD_DOT_VIEW_TREE_RELATIVE, AD_DOT_LABEL_NONE, 0, nullptr);
  assert(strstr(none, "<B>Even</B> <I>d</I> = 2") != nullptr);
  assert(strstr(none, "result (synthetic)") == nullptr);
  assert(strstr(none, "<I>V</I>") == nullptr);
  free(none);

  assert(!ad_tree_write_dot(nullptr, &game, &result, AD_DOT_PLAYER_BOTH,
                            AD_DOT_VIEW_CLASSIC, AD_DOT_LABEL_COUNTS, 1,
                            nullptr));
  assert(pg_priority_map_restore(&priority_map, &game));
  zielonka_result_destroy(&result);
  pg_set_destroy(&domain);
  pg_priority_map_destroy(&priority_map);
  pg_game_destroy(&game);
  return 0;
}
