// Decode bytes into a parity game and cross-check every solver and backend.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "oracle.hh"

namespace {

using fuzz::Args;
using fuzz::Outcome;

// Input bytes come first, then a stream derived from them, so short inputs
// still describe whole games.
class Stream {
public:
  Stream(std::uint8_t const *data, std::size_t size)
      : data_(data), size_(size) {
    for (std::size_t i = 0; i < size; i++) {
      state_ = (state_ ^ data[i]) * 16777619U;
    }
  }
  unsigned next() {
    if (at_ < size_) {
      return data_[at_++];
    }
    state_ = state_ * 1664525U + 1013904223U;
    return state_ >> 24U;
  }

private:
  std::uint8_t const *data_;
  std::size_t size_;
  std::size_t at_ = 0;
  std::uint32_t state_ = 2166136261U;
};

struct Decoded {
  std::string text;
  bool small = true;
  unsigned k = 1;
  unsigned t = 0;
};

Decoded decode(std::uint8_t const *data, std::size_t size) {
  Stream bytes(data, size);
  Decoded game;
  unsigned const shape = bytes.next();
  game.small = shape < 192U;
  unsigned const n = game.small ? 1 + shape % 8 : 9 + shape % 32;
  unsigned const priorities = 2 + bytes.next() % 7;
  unsigned const degree = 1 + bytes.next() % 6;
  unsigned const host = bytes.next();
  game.k = 1 + host % 3;
  game.t = (host >> 2U) % 3;
  game.text = "parity " + std::to_string(n - 1) + ";\n";
  for (unsigned v = 0; v < n; v++) {
    game.text += std::to_string(v) + ' ' +
                 std::to_string(bytes.next() % priorities) + ' ' +
                 std::to_string(bytes.next() & 1U) + ' ';
    unsigned const edges = 1 + bytes.next() % degree;
    for (unsigned e = 0; e < edges; e++) {
      game.text += (e == 0 ? "" : ",") + std::to_string(bytes.next() % n);
    }
    game.text += ";\n";
  }
  return game;
}

std::vector<std::string> const ad_backends = {
    "fused", "packed", "dense", "queue", "queue1", "dirty", "dirty1", "byte"};
std::vector<std::string> const pm_backends = {"packed", "lookup", "compare",
                                              "both"};

class Check {
public:
  Check(strahler_game const *game, Outcome truth)
      : game_(game), truth_(std::move(truth)) {}

  // Every backend must return the scalar document, byte for byte.
  Outcome lifting(Args const &args) const {
    std::string reference;
    {
      fuzz::Backend const scalar("STRAHLER_AD_BACKEND", "scalar");
      reference = fuzz::decompose(game_, args);
    }
    for (auto const &name : ad_backends) {
      fuzz::Backend const backend("STRAHLER_AD_BACKEND", name);
      if (fuzz::decompose(game_, args) != reference) {
        fuzz::fail("AD backend " + name + " differs for " + fuzz::join(args));
      }
    }
    Outcome out = fuzz::decomposition_outcome(reference);
    sound(out, fuzz::join(args));
    return out;
  }

  Outcome progress(Args const &args) const {
    std::string reference;
    {
      fuzz::Backend const scalar("STRAHLER_PM_BACKEND", "scalar");
      reference = fuzz::measures(game_, args);
    }
    for (auto const &name : pm_backends) {
      fuzz::Backend const backend("STRAHLER_PM_BACKEND", name);
      if (fuzz::measures(game_, args) != reference) {
        fuzz::fail("PM backend " + name + " differs for " + fuzz::join(args));
      }
    }
    Outcome out = fuzz::measure_outcome(reference);
    sound(out, "PM " + fuzz::join(args));
    return out;
  }

  // Universal hosts are too slow to repeat per backend.
  [[nodiscard]] Outcome universal(Args const &args) const {
    return fuzz::decomposition_outcome(fuzz::decompose(game_, args));
  }

  void solved(Outcome const &out, std::string const &name) const {
    if (!out.complete) {
      fuzz::fail(name + " left vertices unresolved");
    }
    for (std::size_t p = 0; p < 2; p++) {
      if (out.won[p] != truth_.won[p]) {
        fuzz::fail(name + " disagrees with Zielonka");
      }
    }
  }

private:
  void sound(Outcome const &out, std::string const &name) const {
    for (std::size_t p = 0; p < 2; p++) {
      for (long const vertex : out.won[p]) {
        if (!truth_.won[p].contains(vertex)) {
          fuzz::fail(name + " certified a win for the losing player");
        }
      }
    }
  }

  strahler_game const *game_;
  Outcome truth_;
};

Args pinned(std::string const &algorithm, unsigned k, unsigned t, bool iter) {
  Args args{"--algorithm=" + algorithm, "--tree-k=" + std::to_string(k),
            "--tree-t=" + std::to_string(t)};
  if (iter) {
    args.emplace_back("--iter-shave");
  }
  return args;
}

// A larger host contains the smaller one, so it must not lose a solved game.
void fixed_hosts(Check const &check, unsigned k, unsigned t) {
  for (std::string const algorithm : {"lifting", "lifting-sym"}) {
    for (bool const iter : {false, true}) {
      Args const args = pinned(algorithm, k, t, iter);
      if (!check.lifting(args).complete) {
        continue;
      }
      check.solved(check.lifting(pinned(algorithm, k + 1, t, iter)),
                   "raising k from " + fuzz::join(args));
      check.solved(check.lifting(pinned(algorithm, k, t + 1, iter)),
                   "raising t from " + fuzz::join(args));
    }
  }
}

void universal_solvers(Check const &check, strahler_game const *game) {
  check.solved(fuzz::decomposition_outcome(
                   fuzz::decompose(game, {"--algorithm=zielonka-sym"})),
               "zielonka-sym");
  std::vector<Args> const universal = {
      {"--algorithm=lifting"},
      {"--algorithm=lifting-sym"},
      {"--algorithm=lifting", "--iter-shave"},
      {"--algorithm=lifting-sym", "--iter-shave"},
      {"--algorithm=lifting", "--adaptive-t"},
      {"--algorithm=lifting", "--adaptive-t=incremental"},
      {"--algorithm=lifting", "--compress"},
      {"--algorithm=lifting-sym", "--initial-shave=all"},
  };
  for (auto const &args : universal) {
    check.solved(check.universal(args), fuzz::join(args));
  }
  std::vector<Args> const searches = {
      {"--algorithm=lifting", "--search=lexicographic"},
      {"--algorithm=lifting", "--search=lexicographic", "--iter-shave"},
      {"--algorithm=lifting-sym", "--search=lexicographic"},
      {"--algorithm=lifting-sym", "--search=lexicographic", "--iter-shave"},
  };
  for (auto const &args : searches) {
    check.solved(check.lifting(args), fuzz::join(args));
  }
  for (Args const &args : {Args{"--search=lexicographic"},
                           Args{"--search=lexicographic", "--iter-shave"}}) {
    check.solved(check.progress(args), "PM " + fuzz::join(args));
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data,
                                      std::size_t size) {
  Decoded const decoded = decode(data, size);
  fuzz::current_game() = decoded.text;
  strahler_game *game = nullptr;
  char *error = nullptr;
  int const status = strahler_game_parse(decoded.text.data(),
                                         decoded.text.size(), &game, &error);
  fuzz::finish(status, nullptr, error, "parse ");
  Outcome truth = fuzz::decomposition_outcome(
      fuzz::decompose(game, {"--algorithm=zielonka"}));
  if (!truth.complete) {
    fuzz::fail("Zielonka left vertices unresolved");
  }
  Check const check(game, std::move(truth));
  fixed_hosts(check, decoded.k, decoded.t);
  for (unsigned const k : {2U, 3U}) {
    check.progress(
        {"--k=" + std::to_string(k), "--t=" + std::to_string(decoded.t)});
  }
  if (decoded.small) {
    universal_solvers(check, game);
  }
  strahler_game_free(game);
  return 0;
}
