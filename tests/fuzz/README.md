# Fuzzing

Two libFuzzer targets exercise the public C API.

| Target | Input | Checks |
| --- | --- | --- |
| `solvers` | Bytes decoded into a parity game of 1 to 40 vertices | See below |
| `parse` | Arbitrary bytes given to the game parser | No crash, leak or undefined behaviour; accepted games of at most 64 vertices are solved |

For each decoded game, `solvers` checks that:

- every AD backend returns the same JSON document as `scalar`, and full SIMD
  Strahler PM returns the same document as native scalar;
- no solver certifies a win that Zielonka gives to the opponent;
- on games of at most 8 vertices, every complete solver finds Zielonka's winners;
- a fixed `(k,t)` host that solves a game still solves it after raising `k` or `t`.

## Run locally

Use Clang. On small machines, keep to one compile job and one fuzzer.

```sh
CC=clang CXX=clang++ meson setup build-fuzz --buildtype=debugoptimized -Dfuzz=true \
  -Db_sanitize=address,undefined -Db_lundef=false -Dlenstree=disabled
meson compile -C build-fuzz -j1 fuzz-solvers fuzz-parse
mkdir -p grown/solvers
cp tests/fuzz/corpus/solvers/* grown/solvers/
build-fuzz/fuzz-solvers grown/solvers -max_total_time=600 -rss_limit_mb=2048 \
  -timeout=60 -max_len=512 -len_control=20
```

Every build also has `fuzz-solvers-replay` and `fuzz-parse-replay`. Pass them
input files or directories to reproduce a finding without libFuzzer. The Meson
tests `fuzz_replay_solvers` and `fuzz_replay_parse` replay the saved corpora.

## Continuous integration

`.github/workflows/fuzz.yml` fuzzes each target for 5 minutes on pull requests and
30 minutes every Monday. Findings and grown corpora are uploaded as an artifact.

The `mutants` job checks that the fuzzer can detect real faults. It applies each
bug in `mutants.json` to a copy of the sources and requires the saved `solvers`
corpus to fail. To check one locally, run
`python3 tests/fuzz/mutants.py --only NAME`.
