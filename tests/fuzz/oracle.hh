// Small JSON reader and solver calls shared by the fuzz targets.
#ifndef TESTS_FUZZ_ORACLE_HH
#define TESTS_FUZZ_ORACLE_HH

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "strahler.h"

namespace fuzz {

struct Json {
  enum class Kind : std::uint8_t {
    null,
    boolean,
    number,
    string,
    array,
    object
  };
  Kind kind = Kind::null;
  bool boolean = false;
  long number = 0;
  std::vector<Json> items;
  std::map<std::string, Json> fields;

  Json const &operator[](std::string const &key) const {
    return fields.at(key);
  }
  [[nodiscard]] bool has(std::string const &key) const {
    return fields.contains(key);
  }
};

class JsonReader {
public:
  explicit JsonReader(char const *text) : at_(text) {}

  Json value() {
    space();
    Json out;
    if (*at_ == '{') {
      out.kind = Json::Kind::object;
      at_++;
      for (space(); *at_ != '}'; space()) {
        std::string key = string();
        space();
        at_++;
        out.fields.emplace(std::move(key), value());
        separator();
      }
      at_++;
    } else if (*at_ == '[') {
      out.kind = Json::Kind::array;
      at_++;
      for (space(); *at_ != ']'; space()) {
        out.items.push_back(value());
        separator();
      }
      at_++;
    } else if (*at_ == '"') {
      out.kind = Json::Kind::string;
      string();
    } else if (*at_ == 't' || *at_ == 'f') {
      out.kind = Json::Kind::boolean;
      out.boolean = *at_ == 't';
      at_ += out.boolean ? 4 : 5;
    } else if (*at_ == 'n') {
      at_ += 4;
    } else {
      out.kind = Json::Kind::number;
      char *end = nullptr;
      out.number = std::strtol(at_, &end, 10);
      at_ = end;
      while (*at_ == '.' || *at_ == 'e' || *at_ == 'E' || *at_ == '+' ||
             *at_ == '-' || (*at_ >= '0' && *at_ <= '9')) {
        at_++;
      }
    }
    return out;
  }

private:
  void space() {
    while (*at_ == ' ' || *at_ == '\n' || *at_ == '\t' || *at_ == '\r') {
      at_++;
    }
  }
  void separator() {
    space();
    if (*at_ == ',') {
      at_++;
    }
  }
  std::string string() {
    std::string out;
    for (at_++; *at_ != '"'; at_++) {
      if (*at_ == '\\') {
        at_++;
      }
      out += *at_;
    }
    at_++;
    return out;
  }

  char const *at_;
};

using Args = std::vector<std::string>;
using Won = std::set<long>;

struct Outcome {
  bool complete = false;
  Won won[2];
};

inline std::string &current_game() {
  static std::string game;
  return game;
}

[[noreturn]] inline void fail(std::string const &property) {
  std::fprintf(stderr, "FUZZ PROPERTY FAILED: %s\n--- game\n%s---\n",
               property.c_str(), current_game().c_str());
  std::abort();
}

inline std::string join(Args const &args) {
  std::string out;
  for (auto const &arg : args) {
    out += arg + ' ';
  }
  return out;
}

// Backends read the environment on each solve, so one process can compare them.
class Backend {
public:
  Backend(char const *variable, std::string const &name) : variable_(variable) {
    if (setenv(variable_, name.c_str(), 1) != 0) {
      fail("setenv failed");
    }
  }
  Backend(Backend const &) = delete;
  Backend &operator=(Backend const &) = delete;
  ~Backend() { unsetenv(variable_); }

private:
  char const *variable_;
};

inline std::string finish(int const status, char *json, char *error,
                          std::string const &call) {
  std::string const message = error == nullptr ? "" : error;
  std::string const text = json == nullptr ? "" : json;
  strahler_free(json);
  strahler_free(error);
  if (status != 0) {
    fail(call + "returned status " + std::to_string(status) + ": " + message);
  }
  return text;
}

inline std::vector<char const *> pointers(Args const &args) {
  std::vector<char const *> out;
  out.reserve(args.size());
  for (auto const &arg : args) {
    out.push_back(arg.c_str());
  }
  return out;
}

inline std::string decompose(strahler_game const *game, Args const &args) {
  auto const argv = pointers(args);
  char *json = nullptr;
  char *error = nullptr;
  int const status =
      strahler_decompose(game, argv.data(), static_cast<int>(argv.size()),
                         nullptr, nullptr, &json, &error);
  return finish(status, json, error, "decompose " + join(args));
}

inline std::string measures(strahler_game const *game, Args const &args) {
  auto const argv = pointers(args);
  char *json = nullptr;
  char *error = nullptr;
  int const status = strahler_progress_measures(
      game, argv.data(), static_cast<int>(argv.size()), &json, &error);
  return finish(status, json, error, "progress measures " + join(args));
}

inline void add(Won &to, Json const &ids) {
  for (auto const &id : ids.items) {
    to.insert(id.number);
  }
}

inline Outcome decomposition_outcome(std::string const &text) {
  Json const doc = JsonReader(text.c_str()).value();
  Outcome out;
  out.complete = doc["complete"].boolean;
  for (std::size_t p = 0; p < 2; p++) {
    add(out.won[p], doc["regions"].items[p]);
    add(out.won[p], doc["initial_won"].items[p]);
    for (auto const &shave : doc["shaves"].items) {
      add(out.won[p], shave["attracted"].items[p]);
    }
  }
  return out;
}

inline void tree_vertices(Json const &node, Won &to) {
  if (node.kind != Json::Kind::object) {
    return;
  }
  add(to, node["vertices"]);
  for (auto const &child : node["children"].items) {
    tree_vertices(child.has("node") ? child["node"] : child, to);
  }
}

inline Outcome measure_outcome(std::string const &text) {
  Json const doc = JsonReader(text.c_str()).value();
  Outcome out;
  out.complete = doc["rounds"].items.back()["complete"].boolean;
  for (auto const &round : doc["rounds"].items) {
    for (std::size_t p = 0; p < 2; p++) {
      Json const &player = round["trees"].items[p];
      tree_vertices(player["tree"], out.won[p]);
      add(out.won[p], player["attracted"]);
      add(out.won[p], player["shaved"]);
    }
  }
  return out;
}

} // namespace fuzz

#endif
