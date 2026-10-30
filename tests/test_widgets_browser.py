"""Render the packaged inline ESM offline, with a small anywidget model stub."""

from pathlib import Path

import pytest
import strahler

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    sync_playwright = None

from otj22 import hk

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ('kind', 'data', 'hovered', 'highlights', 'outlines', 'selected', 'mode', 'next_kind', 'edges', 'hovered_path', 'owner_styles')


@pytest.fixture(scope='module')
def browser():
    if sync_playwright is None:
        print('Browser tests skipped: playwright is not importable')
        pytest.skip('playwright is not importable')
    with sync_playwright() as playwright:
        # Missing browsers or launch failures are failures, not skipped coverage.
        browser = playwright.chromium.launch(headless=True)
        yield browser
        browser.close()


@pytest.fixture
def page(browser):
    page = browser.new_page()
    errors, requests = [], []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.on('console', lambda message: errors.append(message.text) if message.type == 'error' else None)
    page.route('**/*', lambda route: (requests.append(route.request.url), route.abort()))
    yield page
    page.close()
    assert not errors, errors
    assert not requests, f'Widgets attempted network access: {requests}'


def mount(page, *widgets):
    """Round-trip saved traits and custom messages through the Python widgets."""
    from strahler.widgets import _ESM, _CSS
    by_id = {widget.model_id: widget for widget in widgets}

    def snapshots():
        return [dict(id=widget.model_id, state={field: getattr(widget, field) for field in FIELDS})
                for widget in widgets]

    def update(model_id, changes, message):
        widget = by_id[model_id]
        for field, value in changes.items():
            setattr(widget, field, value)
        if message is not None:
            widget._handle_custom_msg(message, [])
        return snapshots()

    page.expose_function('updatePython', update)
    page.evaluate('''async ({source, css, snapshots}) => {
      const url = URL.createObjectURL(new Blob([source], {type:'text/javascript'}));
      const renderer = (await import(url)).default;
      URL.revokeObjectURL(url);
      const style = document.createElement('style'); style.textContent = css;
      document.head.append(style);
      window.models = [];
      window.pending = Promise.resolve();
      const sync = snapshots => snapshots.forEach(({state}, i) => models[i].update(state));
      for (const {id, state} of snapshots) {
        const listeners = {}, changes = {};
        const model = {
          model_id:id, state, saved:0, sent:[],
          get(key) { return state[key]; },
          set(key, value) { changes[key] = value; this.update({[key]:value}); },
          on(event, callback) { (listeners[event] ||= new Set()).add(callback); },
          off(event, callback) { listeners[event]?.delete(callback); },
          update(patch) {
            for (const [key,value] of Object.entries(patch)) {
              if (JSON.stringify(state[key]) === JSON.stringify(value)) continue;
              state[key] = value;
              for (const callback of listeners['change:'+key] || []) callback();
            }
          },
          save_changes() {
            this.saved++;
            const patch = {...changes};
            for (const key of Object.keys(changes)) delete changes[key];
            return window.pending = updatePython(id, patch, null).then(sync);
          },
          send(message) {
            this.sent.push(message);
            return window.pending = updatePython(id, {}, message).then(sync);
          }
        };
        models.push(model);
        const el = document.createElement('div'); el.id = 'widget-'+(models.length-1);
        document.body.append(el);
        renderer.render({model, el});
      }
    }''', dict(source=_ESM, css=_CSS, snapshots=snapshots()))


@pytest.mark.parametrize('text', [(ROOT / 'tests/games/otj22-fig1.gm').read_text(), hk(4)],
                         ids=['fig1', 'h4'])
def test_game_render_and_selection(page, text):
    from strahler.widgets import GameView
    with strahler.Game.from_text(text) as game:
        view = GameView(game)
        mount(page, view)
        assert page.locator('svg').count() == 1
        assert page.locator('[data-vertex]').count() == len(game.vertices)
        assert page.locator('g[data-vertex] > rect').count() == sum(p == 0 for p in game.owner.values())
        assert page.locator('g[data-vertex] > polygon').count() == sum(p == 1 for p in game.owner.values())
        assert page.locator('path[marker-end]').count() == len(game.edges)
        vertex = game.vertices[-1]
        page.locator(f'[data-vertex="{vertex}"]').click()
        page.evaluate('window.pending')
        assert view.selected == vertex
        assert page.evaluate('models[0].get("selected")') == vertex
        assert page.locator(f'#widget-0 [data-vertex="{vertex}"] > :first-child').get_attribute('stroke') == '#dc2626'
        page.locator(f'[data-vertex="{game.vertices[0]}"]').focus()
        page.keyboard.press('Enter')
        page.evaluate('window.pending')
        assert view.selected == game.vertices[0]


def nodes(root):
    return [] if root is None else [root] + [n for child in root['children'] for n in nodes(child)]


@pytest.mark.parametrize('mode', ['squares', 'tree'])
@pytest.mark.parametrize('kind', ['decomposition', 'strahler', 'prefix', 'measures', 'empty'])
def test_tree_render_and_interaction(page, mode, kind):
    from strahler.widgets import GameView, TreeView
    with strahler.Game.from_file(ROOT / 'tests/games/otj22-fig1.gm') as game:
        data = dict(decomposition=strahler.decompose(game, 'zielonka').trees[0],
                    strahler=strahler.genstree(2, 3, 1), prefix=strahler.pms_tree('0,e|1,e|'),
                    measures=strahler.progress_measures(game)['rounds'][-1]['trees'][0],
                    empty={'tree': None})[kind]
        game_view = GameView(game)
        view = TreeView(data, game_view, mode=mode)
        mount(page, game_view, view)
        tree = page.locator('#widget-1')
        expected = nodes(view.data)
        assert len(expected) == dict(decomposition=5, strahler=17, prefix=5, measures=7, empty=0)[kind]

        def counts():
            squares = view.mode == 'squares'
            assert tree.locator('svg').count() == (1 if expected else 0)
            assert tree.locator('svg g[role="button"]').count() == len(expected)
            assert tree.locator('svg line').count() == (0 if squares else max(0, len(expected)-1))
            for part in 'HTS':
                assert tree.locator(f'.part-{part}').count() == (len(expected) if squares else 0)

        counts()
        if not expected:
            assert 'empty tree' in tree.inner_text()
            return
        # Select a child to catch bubbling through nested parent regions.
        child = expected[1]
        target = tree.locator('[data-path="' + str(child['path']).replace(' ', '') + '"]')
        # Hit the node center; SVG group corners can be outside its rounded rectangle.
        position = {'x': 10, 'y': 10} if mode == 'squares' else None
        target.hover(position=position)
        page.evaluate('window.pending')
        assert view.hovered == game_view.hovered == child['vertices']
        assert page.evaluate('models[1].get("hovered")') == child['vertices']
        for vertex in child['vertices']:
            assert page.locator(f'#widget-0 [data-vertex="{vertex}"] > :first-child').get_attribute('stroke') == '#c026d3'
        target.click(position=position)
        page.evaluate('window.pending')
        assert view.selected == child['path']
        target.focus()
        page.keyboard.press('Enter')
        page.evaluate('window.pending')
        assert page.evaluate('models[1].sent.at(-1)') == dict(command='select', path=child['path'])
        page.mouse.move(0, 0)
        tree.get_by_role('combobox').focus()
        page.evaluate('window.pending')
        assert game_view.hovered == []
        tree.get_by_role('combobox').select_option('tree' if mode == 'squares' else 'squares')
        page.evaluate('window.pending')
        counts()


@pytest.mark.parametrize('algorithm', ['zielonka', 'zielonka-sym'])
def test_stepper_render_and_navigation(page, algorithm):
    from strahler.widgets import Stepper
    with strahler.Game.from_file(ROOT / 'tests/games/otj22-fig1.gm') as game:
        stepper = Stepper(strahler.trace(game, algorithm))
        calls = stepper.call_tree()
        mount(page, stepper.controls, stepper.game_view, *stepper.tree_views, calls)
        controls = page.locator('#widget-0')

        def check_state():
            assert page.evaluate('models[0].get("data").index') == stepper.index
            assert controls.locator('.badge').count() == 2
            assert controls.locator('pre.current').count() == 1
            assert page.locator('#widget-1 [data-vertex]').count() == 10
            assert controls.locator('.timeline line').count() == len(stepper.cursor.ticks) + 1
            for i, view in enumerate(stepper.tree_views, 2):
                for part in 'HTS':
                    assert page.locator(f'#widget-{i} .part-{part}').count() == (len(nodes(view.data)) if view.mode == 'squares' else 0)
            for player, badge in enumerate(stepper.badges):
                text = controls.locator('.badge').nth(player).inner_text()
                assert badge['status'] in text
                assert f"condition {stepper.verify(player).condition.name}" in text

        check_state()
        for label in ['Next call', 'Step', 'Back', 'Next return'] + (['Next check'] if algorithm.endswith('-sym') else []):
            controls.get_by_role('button', name=label, exact=True).click()
            page.evaluate('window.pending')
            check_state()
        kind = 'labels' if algorithm.endswith('-sym') else 'set'
        controls.get_by_role('combobox', name='Next event kind').select_option(kind)
        page.evaluate('window.pending')
        assert stepper.controls.next_kind == kind
        controls.get_by_role('button', name='Next event of kind', exact=True).click()
        page.evaluate('window.pending')
        assert stepper.state.event['event'] == kind
        check_state()
        # Seek through distinct tree-building prefixes and the final certificates.
        for index in [len(stepper.run)//3, 2*len(stepper.run)//3, len(stepper.run)-1]:
            controls.get_by_role('slider').evaluate('''(slider, index) => {
              slider.value = index; slider.dispatchEvent(new Event('change', {bubbles:true}));
            }''', index)
            page.evaluate('window.pending')
            assert stepper.index == index
            check_state()
        controls.get_by_role('button', name='Run to end', exact=True).click()
        page.evaluate('window.pending')
        assert stepper.state.trees == stepper.run.result.trees
        assert all(badge['ok'] for badge in stepper.badges)
        assert [len(nodes(view.data)) for view in stepper.tree_views] == [5, 0]
        # Click a call twice: selected-path sync and custom-message paths both jump.
        first = page.locator('#widget-4 [data-path="[0]"]')
        for _ in range(2):
            first.click()
            page.evaluate('window.pending')
            assert stepper.index == stepper.cursor.calls[0]['index']
            check_state()


def test_squares_link_to_game_and_tree(page):
    import runpy
    from strahler.widgets import GameView, TreeView
    paper = runpy.run_path(str(ROOT / 'jupyter-examples/otj22.py'))['PAPER_FIG1']
    with strahler.Game.from_file(ROOT / 'tests/games/otj22-fig1.gm') as game:
        graph = GameView(game)
        squares = TreeView(paper, graph, mode='squares')
        tree = squares.link(TreeView(paper, graph, mode='tree'))
        graph.highlight([0], '#abc', 'target')
        mount(page, graph, squares, tree)
        assert page.locator('#widget-1 .part-S').count() == 6
        assert page.locator('#widget-1 g[data-vertex] > polygon').count() == sum(
            owner == 1 for owner in game.owner.values())
        assert page.locator('#widget-1 path[marker-end]').count() == len(game.edges)
        assert page.locator('#widget-1 [data-vertex="0"] > polygon').get_attribute('fill') == '#abc'

        def check(path, vertices):
            page.evaluate('window.pending')
            assert squares.hovered_path == tree.hovered_path == path
            assert graph.hovered == squares.hovered == tree.hovered == vertices
            key = str(path).replace(' ', '')
            for index in (1, 2):
                assert page.locator(f'#widget-{index} [data-node-outline="{key}"]').get_attribute('stroke') == '#c026d3'
            for v in vertices:
                for index in (0, 1):
                    assert page.locator(f'#widget-{index} [data-vertex="{v}"] > :first-child').get_attribute('stroke') == '#c026d3'

        # Hover S, H, and T of the same node, then an empty descendant.
        for part in 'SHT':
            page.locator(f'#widget-1 [data-path="[0]"] > .part-{part}').hover(position={'x': 10, 'y': 10})
            check([0], [0, 1, 2])
        page.locator('#widget-1 [data-path="[0,0]"] > .part-H').hover(position={'x': 10, 'y': 10})
        check([0, 0], [])
        page.locator('#widget-2 [data-path="[1]"]').hover()
        check([1], [3, 4, 5, 6, 7, 8, 9])
        page.locator('#widget-2 [data-path="[1]"]').click()
        page.evaluate('window.pending')
        assert squares.selected == tree.selected == [1]
        page.mouse.move(0, 0)
        page.locator('#widget-2 select').focus()
        page.evaluate('window.pending')
        assert tree.hovered_path is squares.hovered_path is None
        assert graph.hovered == []
