// Feed arbitrary bytes to the game parser, then solve small accepted games.
#include <cstddef>
#include <cstdint>
#include <string>

#include "oracle.hh"

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data,
                                      std::size_t size) {
  fuzz::current_game().assign(reinterpret_cast<char const *>(data), size);
  strahler_game *game = nullptr;
  char *error = nullptr;
  int const status = strahler_game_parse(reinterpret_cast<char const *>(data),
                                         size, &game, &error);
  strahler_free(error);
  if (status != 0) {
    return 0;
  }
  char *json = nullptr;
  error = nullptr;
  // Read the status first: argument evaluation order is unspecified.
  int const summarized = strahler_game_summary(game, &json, &error);
  std::string const summary = fuzz::finish(summarized, json, error, "summary ");
  fuzz::Json const doc = fuzz::JsonReader(summary.c_str()).value();
  if (doc["vertices"].items.size() <= 64) {
    fuzz::decompose(game, {"--algorithm=zielonka"});
  }
  strahler_game_free(game);
  return 0;
}
