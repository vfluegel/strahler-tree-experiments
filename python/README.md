# strahler

Python 3.10+ bindings for libstrahler, using only the standard library at runtime.
The package contains a shared C library and pure Python ctypes bindings; it
needs no Python extension module. Linux x86_64 and macOS arm64 are the tested
CI targets. Prebuilt wheels and the notebooks zip are attached to
[GitHub releases](https://github.com/vfluegel/strahler-tree-experiments/releases),
with [pip and uv instructions](../jupyter-examples/README.md). Wheels are not
published to PyPI.

## Install from a checkout

Install a C23 compiler (GCC 14+, Clang 18+, or Apple Clang), a C++20 compiler,
and Python 3.10+. Run from the repository root:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install .
```

With uv:

```sh
uv venv .venv --python 3.10
uv pip install --python .venv/bin/python .
.venv/bin/python -c 'import strahler; print(strahler.version())'
```

Both commands use meson-python and enable `-Dpython=enabled`. CLI installation
is excluded from the wheel. Meson-python bundles `libstrahler` in
`.strahler.mesonpy.libs` beside the package and tags the wheel
`py3-none-<platform>`. Regular Meson installs use the system shared-library
search path. `STRAHLER_LIB` may instead name a library file or a Meson build
directory during development. The loader requires the compatible 0.3.x/0.4.x ABI
and rejects other library versions with `ImportError` before binding functions.

## Load, solve, and inspect

```python
from strahler import Game, decompose, check

with Game.from_text('parity 1; 0 2 0 0 "even"; 1 1 1 1 "odd";') as game:
    result = decompose(game, algorithm='zielonka')
    assert result.complete
    print(game.vertices, game.edges, game.names)
    print(result.regions)
    even = result.trees[0]
    print(even.h, even.H, even.T, even.S, even.path)
    assert check(game, even, 0, subgame=result.regions[0]).ok
    print(result.metrics)
    document = result.to_json()  # exact strahler-decomp/1 text
```

`Game.from_file(path)` reads UTF-8 PGSolver text. Vertices are external IDs,
not dense C indices. `vertices` is a tuple of IDs, `edges` a tuple of directed
pairs. `priority`, `owner`, `successors`, and `names` are dictionaries keyed
by ID; successors are tuples, and missing names are `None`. Owners and players
are 0 for Even and 1 for Odd. `start` is the declared/default start ID.
These Python containers describe the native game; editing them does not edit
it. A context manager or `close()` releases the native handle early; otherwise
Python garbage collection releases it. Keep the handle open during all calls,
including calls on other threads.

Supported decomposition algorithms are `lifting` (the CLI default),
`zielonka`, `zielonka-sym`, and `lifting-sym`. Options use the same values and
compatibility rules as pg2adot:

```python
with Game.from_file('tests/games/strahler_two.pg') as game:
    result = decompose(game, 'lifting', adaptive_t='incremental', iter_shave=True)
    result = decompose(game, 'lifting', tree_k=1)  # may be partial
    result = decompose(game, 'lifting-sym', kary=2, initial_shave='all')
    result = decompose(game, args=['--tree-file=tests/trees/one_child.tree'])
```

Use these calls while `game` is open. Keyword underscores become hyphens,
`True` emits a flag, and `False`/`None` omit it. Raw `args` are appended.
All lifting hosts are available: full/default, `tree_k`, `kary`, `tree_file`,
`adaptive_t=True`, and `adaptive_t='incremental'`; iterative shaving is
`iter_shave=True`. As in the CLI, symmetric lifting rejects adaptive hosts.
The library manages JSON format and callback traces, so `--format`, `--trace`,
`--help`, and `--version` are not solver options here.

`Result.trees` contains two `Node`/`None` values. Nodes have `h`, frozensets
`H`, `T`, `S`, tuple `children`, and a tuple `path` of zero-based child indices.
`to_dict()` returns the documented `{h,H,T,S,children}` JSON shape.
`regions`, `unresolved`, `complete`, and `metrics` preserve the solver's
claims. `regions[player]` is the dominion covered by that player's returned
attractor decomposition. `data` also contains `initial_won` and iterative
`shaves`; shaved winners have separate certificates outside that dominion.
The notebook metrics tables call its vertex count `dominion`; `won` counts
the union of `regions[player]`, `data['initial_won'][player]`, and every
`shave['attracted'][player]` in `data['shaves']`, once per vertex.
`unresolved` contains vertices neither player's search certified.
`data['parameters']` holds the solver-reported Even/Odd host bounds (`H,K,t`
for Strahler hosts); `data['budget_exhausted']` identifies an adaptive leaf-limit
stop. These come from the same native statistics as `pg2adot --stats`.
Partial results certify their reported regions; they do not decide unresolved
vertices.

## Step forwards and backwards

```python
from strahler import trace

with Game.from_file('tests/games/otj22-fig1.gm') as game:
    run = trace(game, 'zielonka-sym')
    first = run.state(0)             # header, before solver events
    last = run.state(-1)             # final event
    previous = run.state(len(run) - 2)
    assert last.trees == run.result.trees
    for state in run.steps(kind='check'):
        event = state.event
        tree = state.candidate(event['player'], event['path'], event['subgame'])
        actual = check(game, tree, event['player'], subgame=event['subgame'])
        assert actual.ok == event['is_attractor_decomposition']
    for event, actual in run.check_claims():
        assert actual.ok == event['is_attractor_decomposition']
```

Collection is synchronous. `events` contains the `strahler-trace/1` records,
including its header; `len(run)` counts all records. `state(i)` describes both
players immediately after event i. Seeks reuse lazily cached checkpoints
(about 128 on large traces), replaying the intervening events. Returned states remain independent of
later stepping. H/T/S come from set/move/label events, not the result trees.
Unfinished children can be `None`. Root TOP labels remain visible in interim
states; the final state's trees exclude TOP and match the result certificates.
`trace()` requires both players; `player='even'`/`'odd'` and their raw `args`
equivalents raise `ValueError`. Omit `player` or use `player='both'`.
`state.materialized(player)` obtains that certificate shape at any step.
`state.shaves` retains certificates from earlier iterative rounds.

The full event list stays in memory. Use `trace_limit=N` to bound collection;
exceeding it raises `StrahlerError`, with the observed prefix (ending in
`truncated`) attached as `error.events`. It never reports a truncated run as
complete.

## Check a candidate

`check(game, tree, player, subgame=None, compress=False)` accepts a `Node`,
a tree dict, JSON text, or `None`. The default subgame is the whole game;
pass the candidate's region when checking one winning region. The check is
Definition 1 on the induced subgame, which must be total; it makes no claim
about exits outside that subgame. Empty restrictions are valid. Sets are
restricted first, and error paths refer to the supplied tree before pruning.

Solver trees use normalized priorities, even without `--compress`. The checker
uses the same normalization. Pass `compress=True` for a tree produced with
`compress=True`; `run.check_claims()` does this automatically. `Game.priority`
continues to expose the input priorities.

A `CheckResult` has `ok`, `condition` (a `Condition` IntEnum), tuple `path`,
external `vertex` or `None`, `message`, and tuple `violations`. Each `Violation`
has a named condition, original node path, external vertex and optional directed
edge tuple. Set conditions retain one vertex witness per condition/node; invalid
metadata stops that branch, and dependent checks need total domains. H/T/S
inconsistencies locate every bad entry. The original first-failure fields keep
their meaning. Failed decomposition conditions are
ordinary results. Malformed JSON, unknown IDs, or invalid options raise
`InputError` (also a `ValueError`); solver/I/O/trace failures raise
`StrahlerError` with `status`. Allocation failures raise `MemoryError`.
The condition codes are listed in [the C API reference](../docs/developing.md#c-api-and-python-package).
For example, `result.condition.name` is `'PRIORITY'` for a priority-bound
failure, and `int(result.condition)` retains the C code `4`.

## Progress measures and generated trees

```python
from strahler import progress_measures, genstree, pms_tree, materialize_partial

with Game.from_text('0 2 0 0;') as game:
    measures = progress_measures(game, algorithm='succinct')
    print(measures['rounds'][-1]['trees'])

universal = genstree(k=2, h=3, t=1)   # strahler-tree/1 dict
prefix = pms_tree('0,e|1,e|')         # strahler-pm/1 dict
ordered = pms_tree('1|0|', reorder=True)
```

`progress_measures` supports pg2pmdot's `algorithm='strahler'|'succinct'`,
`k`/`t`, `bits`, `iter_shave`, and preprocessing options. It returns the
existing JSON dict with all rounds, their complete flags, and prefix trees.
`pms_tree` supports `check_order=True` and `reorder=True` (mutually exclusive).
Leaf streams in `pms_tree` and `materialize_partial` are limited to 1025
components per path; deeper paths raise `InputError` before native recursion.

`materialize_partial(game, labels, compress=False)` wraps the C label
materializer and returns a dict containing a candidate `tree` and structural
`problems`. Its small input vocabulary is documented in the C API reference;
a candidate or an empty-core problem is not a validity verdict. Use `check`
for Definition 1.

## Offline notebook views

Install `strahler[notebooks]` to enable the optional `strahler.widgets` module.
Ordinary `import strahler` does not import anywidget, traitlets, or Jupyter.
The seven notebooks and checkout/release setup commands are in
[jupyter-examples](../jupyter-examples/README.md).

```python
from strahler.widgets import GameView, TreeView, Stepper

game = Game.from_file('jupyter-examples/otj22-fig1.gm')
view = GameView(game)  # or coordinates={external_id: (x, y), ...}
view.highlight([0, 1], colour='#bbf7d0', name='target')
tree = TreeView(decompose(game, 'zielonka').trees[0], game_view=view)
stepper = Stepper(trace(game, 'zielonka-sym'))
stepper.next('check')
print(stepper.verify(stepper.state.event['player']))
```

`TreeView` also accepts `genstree` and progress-measure prefix-tree JSON.
`mode='squares'|'tree'` selects nested squares or node links. Squares put S in
a full-height right column, with H above T and the children below, from left
to right. Regions grow with their vertex counts; empty regions keep a labelled
band. The default is squares for decompositions with at most 40 vertices and
depth at most 4 (root depth 0), and a tree otherwise. Stepper views choose again
as their decompositions change, until you select a mode explicitly.

Pass `game_view=view` or `game=game` to draw vertices with their priorities and
game edges. Without game metadata, squares show vertex IDs. Even vertices are
squares; Odd vertices are regular pentagons. Both views use the same colours.
Game edges are light grey, behind the vertices; `edges=False` hides them, and
squares omit arrows when more than 200 edges join the displayed vertices.
Hover highlights the node's vertices in the linked game.
`tree.link(other_tree)` shares hover and selection between views of the same
tree, including empty nodes. `mode='boxes'` remains an alias for `squares` for
one release and emits `DeprecationWarning`.
`GameView.selected` is the clicked external vertex ID.
`Stepper` supports `seek(index)`, `step()`, `back()`, `next(kind)`, `end()`,
and a clickable `call_tree()`. Recorded claims are labelled with their event
index and domain, separately from the current independent checker verdict.
Keep the native game open while interacting. All frontend code is inline ESM
and CSS, without CDN imports or runtime network requests.

## Development checks

```sh
python -m pip install meson-python meson ninja pytest build
meson setup build-py -Dpython=enabled -Dlenstree=disabled
meson compile -C build-py
meson test -C build-py test_strahler test_strahler_alloc test_python --print-errorlogs
python -m build --wheel --outdir dist
```

`test_python` runs pytest through Meson. Existing measured slow corpus pairs
are in `test_python_slow`, excluded by the default Meson setup; run explicitly
with `meson test --setup=all ... test_python_slow`. Installed-wheel tests use
`STRAHLER_BUILD_DIR` to find comparison CLIs and leave `STRAHLER_LIB` unset.
C API ownership and failure injection also run in the sanitizer and Valgrind
jobs. The Linux Python test checks RSS across 1,000 repeated call batches.
