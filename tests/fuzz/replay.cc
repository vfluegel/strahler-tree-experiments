// Run a fuzz target over saved inputs without libFuzzer.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data,
                                      std::size_t size);

namespace {

void replay(std::filesystem::path const &path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<char> const bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
  std::vector<std::uint8_t> const data(bytes.begin(), bytes.end());
  LLVMFuzzerTestOneInput(data.data(), data.size());
}

int run(int const argc, char **argv) {
  std::size_t count = 0;
  for (int i = 1; i < argc; i++) {
    std::filesystem::path const path(argv[i]);
    std::vector<std::filesystem::path> inputs;
    if (std::filesystem::is_directory(path)) {
      for (auto const &entry : std::filesystem::directory_iterator(path)) {
        inputs.push_back(entry.path());
      }
      std::ranges::sort(inputs);
    } else {
      inputs.push_back(path);
    }
    for (auto const &input : inputs) {
      replay(input);
      count++;
    }
  }
  std::printf("replayed %zu inputs\n", count);
  return count == 0 ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (std::exception const &error) {
    std::fprintf(stderr, "replay failed: %s\n", error.what());
    return 1;
  }
}
