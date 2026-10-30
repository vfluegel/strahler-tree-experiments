# Offline Jupyter examples

Seven notebooks for the public `strahler` Python API. Run each notebook from top
to bottom. Its first cell prints the package and library versions and stops if
they differ. Use this directory as the notebook working directory so its game
and Python helpers are importable. Rendering uses inline ESM/CSS and SVG, with no
CDN, Graphviz, or runtime network access. Installation may need internet access
for Jupyter and the optional Python dependencies.

| Notebook | What it covers |
| --- | --- |
| [01-quickstart](01-quickstart.ipynb) | Every backend, linked game/tree views, metrics |
| [02-build-a-game](02-build-a-game.ipynb) | Edit PGSolver text, solve again, inspect changed trees |
| [03-otj22-mcnaughton-zielonka](03-otj22-mcnaughton-zielonka.ipynb) | Figure 1, Hₖ, both recursive algorithms, call trees, live k=2…16 counts |
| [04-step-through-lifting](04-step-through-lifting.ipynb) | Strahler/adaptive hosts, +iter, lifts/rounds/phases, partial candidates |
| [05-compare-backends](05-compare-backends.ipynb) | +pre/+iter metrics, generated and progress-measure prefix trees |
| [06-check-your-own-tree](06-check-your-own-tree.ipynb) | Handwritten trees, violation outlines, repair |
| [07-symmetric-lifting-kary-nester](07-symmetric-lifting-kary-nester.ipynb) | Binary/ternary lifting event counts, bounded Nester runs and historical parameters |

Metrics tables use `dominion` for the number of vertices covered by a player's
attractor decomposition (its tree), as in OTJ22 and Jurdziński–Morvan.
`won` counts every certified
win, including preprocessing (`+pre`), earlier shaving rounds (`+iter`), and their
attractors, once per vertex. `unresolved` counts vertices neither player's search
certified. Tree metrics cover only the reported dominion. Tables with these
columns print their definitions; `notebook_support.won_vertices(result, player)`
combines all wins.
Notebook 07 labels Strahler numbers as tree metrics; progress-measure rows
describe occupied measure trees, and AD rows describe dominion decompositions.

## Release zip

From this directory in an extracted release containing `wheels/`:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -m jupyterlab .
```

The zip bundles the Linux x86_64 (glibc 2.28+) and macOS arm64 (11.0+) wheels;
its `requirements.txt` pins the matching `strahler` wheel through
`--find-links ./wheels`. You need Python 3.10+. The zip is
self-contained for the game, generator, helpers, and frontend assets. JupyterLab,
anywidget, and their dependencies must already be installed or available from
a local wheel cache for a fully offline installation. Launching installed
notebooks and using the views requires no network.
The zip also includes the full `keiren/results.csv` and `KEIREN.md` column
definitions. Notebook 07 reads the repository CSV in a checkout, the bundled
copy in a release, or announces a Nester-only fallback if both are absent.

For Colab or a single machine, each wheel is also attached to the release, e.g.
`%pip install https://github.com/vfluegel/strahler-tree-experiments/releases/download/v<version>/strahler-<version>-py3-none-manylinux_2_28_x86_64.whl`.

## Checkout (venv/pip)

From the repository root, with the C23/C++20 toolchain described in
[the package README](../python/README.md):

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install meson-python meson ninja
python -m pip install --no-build-isolation -e '.[notebooks]' \
  --config-settings=build-dir="$PWD/build-editable" \
  --config-settings=setup-args=--buildtype=debug \
  --config-settings=compile-args=-j2
python -m pip install -r jupyter-examples/requirements-dev.txt
cd jupyter-examples
python -m jupyterlab .
```

Editable imports can rebuild native code; keep this venv activated so Meson is
available. `STRAHLER_LIB` may select an existing development build instead of the
library beside the package, but its version must match package metadata.

After changing a notebook or the library, run `python scripts/execute_notebooks.py`
from the repository root in that venv. It saves executed outputs for GitHub,
including static game and tree views, and removes widget state and timing data.
Rerun the cells in Jupyter to use the interactive controls.

## Checkout (uv)

```sh
uv venv .venv --python 3.10
. .venv/bin/activate
uv pip install meson-python meson ninja
uv pip install --no-build-isolation -e '.[notebooks]' \
  --config-settings=build-dir="$PWD/build-editable" \
  --config-settings=setup-args=--buildtype=debug \
  --config-settings=compile-args=-j2
uv pip install -r jupyter-examples/requirements-dev.txt
cd jupyter-examples
python -m jupyterlab .
```

## Widget API

```python
from strahler import Game, trace
from strahler.widgets import GameView, TreeView, Stepper

game = Game.from_file('otj22-fig1.gm')
view = GameView(game)  # deterministic priority layers
view.highlight([0, 1], colour='#bbf7d0', name='target')
stepper = Stepper(trace(game, 'zielonka-sym'))
stepper.next('check')
print(stepper.verify(stepper.state.event['player']))
display(view, stepper, stepper.call_tree())
```

- `GameView.show_check(verdict, player)` and `TreeView.show_check(verdict, player)`
  outline located violations: blue for Even, brown for Odd, with condition/node
  titles and witness edges. Successful checks clear that player’s layer.
- `GameView(game, coordinates={id: (x, y), ...})` scales caller coordinates.
  Squares are Even, regular pentagons Odd; labels show priority and name. `selected` is
  the clicked external ID. Named highlight layers are replaced independently.
  IDs above JavaScript's exact integer range (2⁵³−1) are rejected; renumber them
  before displaying the game.
- `TreeView(tree, game_view=view)` accepts a Node, H/T/S dictionary,
  JSON text, `genstree` document, or a PR #31 prefix tree. Set `tree` to update;
  switch `mode` between `squares` and `tree`. Decompositions with at most 40
  vertices and depth at most 4 (root depth 0) start in squares; others use a tree.
  Squares have a full-height S column on the right, H above T on the left, and
  ordered children below. Empty regions stay visible. Pass `game_view` or
  `game=game` for shapes, priorities, and game edges; otherwise vertices show IDs.
  Light grey edges run behind vertices. `edges=False` hides them; more than 200
  edges among displayed vertices suppresses all arrows. Hover highlights the
  node's vertices. `view.link(other)` shares hover and selection between two
  TreeViews of the same tree. `boxes` is a deprecated alias for one release.
  `None` and documents with `tree: null` display an empty placeholder.
- `Stepper(run)` exposes `state`, `index`, `badges`, `game_view`, `tree_views`,
  `seek(i)`, `step()`, `back()`, `next(kind)`, `end()`, and `verify(player)`.
  `call_tree()` creates a linked call tree. The slider sends on release, not
  during dragging. State and checkpoints are computed lazily; the default
  interval caches about 128 prefixes for large recordings. Timeline ticks are
  sampled to at most 1,000. No full trace is sent to the frontend.
  Tree views follow the same size/depth rule as the algorithm runs, until you
  choose a mode explicitly.

Collection is synchronous, and the trace's event limit still applies. Exceeding
the limit raises an exception and does not return a solved game. `end()` jumps
to the end of the recording. Badges show the algorithm's latest recorded check
with its step and original subgame. They also show a separate, fresh `check()`
verdict on the current candidate, including its `Condition` name (for example,
`PRIORITY`). Failed Definition 1 checks display "decomposition candidate"; this
does not independently certify Definition 3. Lifting candidates pass through
`materialize_partial`; structural diagnostics remain visible beside verdicts.

The paper notebook cites [Thejaswini, Ohlmann, and Jurdziński, FSTTCS 2022](https://doi.org/10.4230/LIPIcs.FSTTCS.2022.44).
Its handwritten Figure 1 tree includes the empty ε₁₁ child; solver trees prune
it. The generator is copied from the checked repository fixture. Its streaming
call counter uses the documented C ABI, retaining no event list, and frees all
native handles even on error. It reuses the package's ABI-checked loader and C
signatures through `strahler._native`. The notebook recomputes and asserts all
k=2…16 counts. `zielonka-sym` follows the corrected entry invariant and trap
closure documented in `docs/symmetric.md`; the notebook and widget identify
that difference from printed pseudocode. Pseudocode highlighting maps recorded
events to abbreviated semantic blocks; it does not identify exact C lines.

## Tests

With the checkout venv activated and package installed, select the registered
Meson tests (use the same build for all checks):

```sh
python -m playwright install --only-shell chromium
meson test -C build-editable \
  test_widgets test_widgets_browser test_notebooks --print-errorlogs
```

Or use an existing debug library:

```sh
STRAHLER_LIB="$PWD/build" \
PYTHONPATH=python python -m pytest tests/test_widget_state.py \
  tests/test_widgets.py tests/test_widgets_js.py tests/test_widgets_browser.py \
  tests/test_notebooks.py -q -s
```

`nbclient` starts the kernel from the test interpreter, executes every notebook,
prints each runtime, and rejects error outputs, including those captured in
Output widgets. Saved notebooks have outputs and execution counts, static
fallbacks for widgets, and no saved widget state. A version check reminds you to
rerun them after a release bump. The execution test runs a copy. Node
checks inline ESM syntax when available locally and is required for that check
in CI. The package CI runs the same tests on Linux and macOS. Browser rendering
and pointer/keyboard interactions are tested with Playwright's Chromium headless
shell in the Linux package job, with a cached browser installation. The browser
tests load inline ESM using a small anywidget model stub, round-trip changes to
Python, and reject console errors, exceptions, and attempted network requests.
They skip only when Playwright cannot be imported; missing browsers and launch
failures fail the tests. Browser installation is a development step and may
require network access; notebook use never downloads a browser.
