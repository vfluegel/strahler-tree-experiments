"""Python widget/comm behaviour; frontend rendering is a separate browser check."""

from pathlib import Path
import runpy
from xml.etree import ElementTree as ET
import pytest
import strahler

pytest.importorskip('anywidget', reason='install strahler[notebooks] for widget tests')
from strahler.widgets import GameView, TreeView, Stepper
from otj22 import hk

ROOT = Path(__file__).resolve().parents[1]
NS = {'s': 'http://www.w3.org/2000/svg'}
WIDGET_MIME = 'application/vnd.jupyter.widget-view+json'


def bundle(view):
    result = view._repr_mimebundle_()
    return result[0] if isinstance(result, tuple) else result


def test_game_svg_fallback():
    with strahler.Game.from_text('10 2 0 10,42 "<a&b>";42 1 1 42;') as game:
        view = GameView(game)
        view.highlight([10, 42], '#abc', '<layer>')
        view.highlight([42], '#def', 'last')
        view.selected = 10
        view.hovered = [42]
        data = bundle(view)
        assert WIDGET_MIME in data and data['text/plain'] == 'Parity game'
        svg = ET.fromstring(data['image/svg+xml'])
        groups = svg.findall('s:g', NS)
        assert len(groups) == len(game.vertices)
        assert [g[0].tag for g in groups] == ['{' + NS['s'] + '}' + tag for tag in ('rect', 'polygon')]
        assert [g[0].get('fill') for g in groups] == ['#abc', '#def']
        assert [g[0].get('stroke') for g in groups] == ['#dc2626', '#c026d3']
        assert all(g[0].get('stroke-width') == '4' for g in groups)
        assert len(svg.findall('s:path', NS)) == len(game.edges)
        assert '<a&b>' in ''.join(svg.itertext())
        assert '&lt;a&amp;b&gt;' in data['image/svg+xml']
        assert data['image/svg+xml'] == bundle(GameView(game, highlights=view.highlights,
                                                       selected=10, hovered=[42]))['image/svg+xml']
        view.highlight([10], 'url(https://example.com/paint)', 'external')
        assert 'https://' not in bundle(view)['image/svg+xml']


@pytest.mark.parametrize('mode', ['squares', 'tree'])
def test_tree_svg_fallback(mode):
    view = TreeView(dict(label='<a&b>', H=[10], T=[42], S=[], children=[
        dict(label='child', H=[42], children=[])]), mode=mode)
    data = bundle(view)
    assert WIDGET_MIME in data and data['text/plain'] == 'Ordered tree'
    svg = ET.fromstring(data['image/svg+xml'])
    assert len(svg.findall('.//s:g[@data-path]', NS)) == 2
    assert '<a&b>' in ''.join(svg.itertext())
    assert '&lt;a&amp;b&gt;' in data['image/svg+xml']
    if mode == 'squares':
        for part, colour in [('H', '#dbeafe'), ('T', '#dcfce7'), ('S', '#ffedd5')]:
            shapes = svg.findall(f'.//s:rect[@class="part-{part}"]', NS)
            assert len(shapes) == 2 and all(s.get('fill') == colour for s in shapes)
    else:
        assert len(svg.findall('.//s:rect', NS)) == 2
        assert len(svg.findall('s:line', NS)) == 1
        assert view.data['xy'] == [130.0, 35]
        assert view.data['children'][0]['xy'] == [130, 120]
    view.tree = None
    assert '∅ — empty tree' in ''.join(ET.fromstring(bundle(view)['image/svg+xml']).itertext())


def test_stepper_static_fallback_and_call_tree():
    with strahler.Game.from_text(hk(2)) as game:
        view = Stepper(strahler.trace(game, 'zielonka-sym'))
        view.next('check')
        data = bundle(view)
        assert WIDGET_MIME in data
        html = ET.fromstring('<div>' + data['text/html'] + '</div>')
        assert len(html.findall('s:svg', NS)) == 3
        text = ''.join(html.itertext())
        assert f'{view.index} / {len(view.run)-1}: check' in text
        for badge in view.badges:
            assert badge['condition'] in text and badge['status'] in text
        calls = view.call_tree()
        assert ET.fromstring(bundle(calls)['image/svg+xml']).findall('.//s:g', NS)
        view.end()
        assert data['text/html'] != bundle(view)['text/html']


def test_game_state_sync_and_coordinates():
    with strahler.Game.from_text('10 2 0 10,42 "a";42 1 1 42;') as game:
        view = GameView(game)
        assert view.data == GameView(game).data
        assert view.selected is None
        view.selected = 42
        assert view.get_state()['selected'] == 42
        view.highlight([10], '#abc', 'domain')
        view.highlight([42], '#def', 'domain')
        assert view.highlights == [dict(name='domain', colour='#def', vertices=[42])]
        with pytest.raises(ValueError, match='unknown'):
            view.highlight([0])
        with pytest.raises(ValueError, match='every'):
            GameView(game, {10: (0, 0)})
        with pytest.raises(ValueError, match='finite'):
            GameView(game, {10: (0, 0), 42: (float('inf'), 1)})
        explicit = GameView(game, {10: (0, 0), 42: (1, 2)})
        assert explicit.data['vertices'][0]['xy'] == [60, 60]


def test_tree_json_and_linked_hover():
    with strahler.Game.from_file(ROOT / 'tests/games/otj22-fig1.gm') as game:
        view = GameView(game)
        tree = TreeView(strahler.decompose(game, 'zielonka').trees[0], view)
        tree.hovered = [0, 1]
        assert view.hovered == [0, 1]
        tree.mode = 'tree'
        assert tree.get_state()['mode'] == 'tree'
        tree.tree = strahler.genstree(2, 3, 1)
        assert tree.data['children']
        tree.tree = strahler.pms_tree('0,e|1,e|')
        assert len(tree.data['children']) == 2
        measures = strahler.progress_measures(game)
        tree.tree = measures['rounds'][-1]['trees'][0]
        assert tree.data['vertices']
        tree.tree = None
        assert tree.data is None


def test_figure1_squares_layout():
    from collections import Counter
    paper = runpy.run_path(str(ROOT / 'jupyter-examples/otj22.py'))['PAPER_FIG1']
    with strahler.Game.from_file(ROOT / 'tests/games/otj22-fig1.gm') as game:
        assert strahler.check(game, paper, 0).ok
        view = TreeView(paper, game=game)
        assert view.mode == 'squares'
        assert view.data == TreeView(paper, game=game).data
        nodes = {}

        def visit(n):
            nodes[tuple(n['path'])] = n
            x, y, w, h = n['box']
            hx, hy, hw, hh = n['regions']['H']
            tx, ty, tw, th = n['regions']['T']
            sx, sy, sw, sh = n['regions']['S']
            assert sx + sw == x + w and sy == y and sh == h
            assert hx == tx == x and hw == tw == sx-x
            assert hy == y and ty == hy+hh
            assert all(r[2] > 0 and r[3] >= 24 for r in n['regions'].values())
            child_x = x
            for child in n['children']:
                cx, cy, cw, ch = child['box']
                assert cx == child_x and cy == ty+th and cy+ch == y+h
                child_x += cw
                visit(child)
            if n['children']:
                assert child_x == sx

        visit(view.data)
        assert len(nodes) == 6 and nodes[(0, 0)]['vertices'] == []
        vertices = view.data['game']['vertices']
        assert Counter(v['id'] for v in vertices) == Counter(game.vertices)
        for vertex in vertices:
            node = nodes[tuple(vertex['path'])]
            assert vertex['id'] in node[vertex['part']]
            x, y, w, h = node['regions'][vertex['part']]
            vx, vy = vertex['xy']
            assert x+20 <= vx <= x+w-20 and y+24 <= vy-20 and vy+34 <= y+h
            assert vertex['priority'] == game.priority[vertex['id']]
        svg = ET.fromstring(view._static_svg())
        for part in 'HTS':
            assert len(svg.findall(f'.//s:rect[@class="part-{part}"]', NS)) == 6
        labels = [''.join(e.itertext()) for e in svg.findall('.//s:text', NS)]
        assert all(part+node['name'] in labels for node in nodes.values() for part in 'HTS')
        assert len(svg.findall('s:path', NS)) == len(game.edges)
        assert all(e.get('stroke') == '#94a3b8' for e in svg.findall('s:path', NS))
        # All edges precede all vertex glyphs.
        tags = [e.tag.split('}')[-1] for e in svg]
        assert max(i for i, tag in enumerate(tags) if tag == 'path') < min(
            i for i, e in enumerate(svg) if e.get('data-vertex') is not None)


@pytest.mark.parametrize('count', [200, 201])
@pytest.mark.parametrize('edges', [False, True])
def test_squares_edge_cutoff(count, edges):
    text = '\n'.join(f'{v} 2 {v % 2} ' + ','.join(map(str, range(10 + (v == 0 and count == 201)))) + ';'
                     for v in range(20))
    with strahler.Game.from_text(text) as game:
        tree = dict(H=list(game.vertices), T=[], S=[], children=[])
        view = TreeView(tree, game=game, edges=edges)
        svg = ET.fromstring(view._static_svg())
        assert len(view.data['game']['edges']) == count
        assert len(svg.findall('s:path', NS)) == (count if edges and count <= 200 else 0)


def test_owner_shapes_are_regular_and_shared():
    from math import hypot
    from strahler.widgets import _OWNER_STYLES
    square, pentagon = (_OWNER_STYLES[str(p)] for p in range(2))
    assert (square['shape'], pentagon['shape']) == ('rect', 'polygon')
    points = [tuple(map(float, xy.split(','))) for xy in pentagon['attrs']['points'].split()]
    assert len(points) == 5 and points[0] == (0, -20)
    assert max(y for x, y in points) - min(y for x, y in points) == pytest.approx(40)
    lengths = [hypot(x-u, y-v) for (x, y), (u, v) in zip(points, points[1:]+points[:1])]
    assert lengths == pytest.approx([lengths[0]] * 5)


def test_default_mode_and_deprecated_alias():
    def node(count=0, children=()):
        return dict(H=list(range(count)), T=[], S=[], children=list(children))
    view = TreeView(node(40))
    assert view.mode == 'squares'
    view.tree = node(41)
    assert view.mode == 'tree'
    view.tree = node(children=[node(children=[node(children=[node(children=[node()])])])])
    assert view.mode == 'squares'
    view.tree = node(children=[view.tree])
    assert view.mode == 'tree'
    view.mode = 'squares'
    view.tree = node(41)
    assert view.mode == 'squares'  # Explicit choices survive updates.
    assert TreeView(strahler.genstree(2, 3, 1)).mode == 'tree'
    with pytest.warns(DeprecationWarning, match='squares'):
        alias = TreeView(node(), mode='boxes')
    assert alias.mode == 'squares'
    with pytest.warns(DeprecationWarning, match='squares'):
        alias.mode = 'boxes'
    assert alias.mode == 'squares'


def test_tree_link_includes_empty_nodes_and_highlight_colours():
    with strahler.Game.from_file(ROOT / 'tests/games/otj22-fig1.gm') as game:
        paper = runpy.run_path(str(ROOT / 'jupyter-examples/otj22.py'))['PAPER_FIG1']
        graph = GameView(game)
        squares = TreeView(paper, graph)
        tree = squares.link(TreeView(paper, graph, mode='tree'))
        squares.hovered_path = [0, 0]
        assert tree.hovered_path == [0, 0] and tree.hovered == []
        tree.hovered_path = [1]
        tree.hovered = tree.data['children'][1]['vertices']
        assert squares.hovered_path == [1] and graph.hovered == squares.hovered == tree.hovered
        squares.selected = [0]
        assert tree.selected == [0]
        graph.highlight([0], '#abc', 'target')
        assert squares.highlights == graph.highlights
        svg = ET.fromstring(squares._static_svg())
        assert svg.find('s:g[@data-vertex="0"]/s:polygon', NS).get('fill') == '#abc'
        tree.close()
        squares.close()
        assert not squares._links


def test_stepper_updates_automatic_layout():
    with strahler.Game.from_text('\n'.join(f'{v} 2 0 {v};' for v in range(41))) as game:
        stepper = Stepper(strahler.trace(game, 'zielonka'))
        assert stepper.tree_views[0].mode == 'squares'
        stepper.end()
        assert len(stepper.tree_views[0].data['vertices']) == 41
        assert stepper.tree_views[0].mode == 'tree'
        stepper.seek(0)
        assert stepper.tree_views[0].mode == 'squares'


@pytest.mark.parametrize('tree', [None, {'tree': None},
    {'schema': 'strahler-decomp/1', 'tree': None},
    {'schema': 'strahler-tree/1', 'tree': None},
    {'schema': 'strahler-pm/1', 'tree': None},
    {'parameters': {'k': 1, 't': 1}, 'tree': None}, 'null', '{"tree": null}'])
def test_empty_tree_documents(tree):
    view = TreeView(tree)
    assert view.data is None
    view.tree = {'h': 0, 'H': [], 'T': [], 'S': [], 'children': []}
    assert view.data is not None  # An empty node still has a tree shape.
    view.tree = tree
    assert view.data is None


@pytest.mark.parametrize('algorithm', ['zielonka', 'zielonka-sym'])
def test_stepper_controls_and_call_tree(algorithm):
    with strahler.Game.from_text(hk(4)) as game:
        run = strahler.trace(game, algorithm)
        view = Stepper(run, checkpoint_interval=11)
        assert view.index == 0
        assert [badge['condition'] for badge in view.controls.data['badges']] == [
            view.verify(player).condition.name for player in range(2)]
        view.next('call')
        call = view.index
        assert view.controls.data['stack']
        view.step()
        view.back()
        assert view.index == call
        view._message(view.controls, dict(command='next', kind='return'), [])
        assert view.state.event['event'] == 'return'
        view._message(view.controls, dict(command='end'), [])
        assert view.state.trees == run.result.trees
        assert all(view.verify(p).ok for p in range(2))
        assert [badge['condition'] for badge in view.controls.data['badges']] == ['OK', 'OK']
        tree = view.call_tree()
        tree.selected = [0]
        assert view.index == call
        view._message(view.controls, dict(command='seek', index=0), [])
        assert view.index == 0
        assert view.controls.data['badges'][0]['claim'] is None


def test_replacing_notebook_panels_closes_owned_comms():
    import importlib.util
    from ipywidgets import VBox
    spec = importlib.util.spec_from_file_location('notebook_support', ROOT / 'jupyter-examples/notebook_support.py')
    support = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(support)
    with strahler.Game.from_text(hk(2)) as game:
        stepper = Stepper(strahler.trace(game, 'zielonka'))
        call_tree = stepper.call_tree()
        panel = VBox([stepper, call_tree])
        support.close_views(panel)
        assert panel.comm is None and call_tree.comm is None
        assert stepper.comm is None and stepper.game_view.comm is None
        assert all(tree.comm is None for tree in stepper.tree_views)


def test_violation_outlines_both_players_and_ascii_svg():
    from html import unescape
    with strahler.Game.from_text('10 2 0 10;20 0 1 30;30 0 0 30;40 0 0 30;') as game:
        tree = dict(h=2, H=[10], T=[], S=[], children=[
            dict(h=0, H=[20], T=[], S=[], children=[]),
            dict(h=0, H=[30], T=[], S=[], children=[])])
        verdict = strahler.check(game, tree, 0)
        view = GameView(game)
        view.show_check(verdict, 0)
        view.show_check(verdict, 1)
        assert [layer['name'] for layer in view.get_state()['outlines']] == ['Even AD violation', 'Odd AD violation']
        assert len({layer['colour'] for layer in view.outlines} | {'#c026d3'}) == 3
        svg = view._static_svg()
        assert svg.isascii()
        parsed = ET.fromstring(svg)
        for layer in view.outlines:
            assert layer['name'] in ''.join(parsed.itertext())
            assert parsed.findall(f"s:path[@stroke=\"{layer['colour']}\"]", NS)
        for mode in ('squares', 'tree'):
            candidate = TreeView(tree, view, mode=mode)
            candidate.show_check(verdict, 0)
            svg = candidate._static_svg()
            assert svg.isascii() and '&#949;' in svg and 'ε' in unescape(svg)
            parsed = ET.fromstring(svg)
            assert 'ε' in ''.join(parsed.itertext())
            assert 'TRAP at node [0]' in ''.join(parsed.itertext())
            assert parsed.findall('.//s:rect[@stroke="#2563eb"]', NS)
        # Live labels remain Unicode.
        assert candidate.data['name'] == 'ε'


def test_stepper_outlines_follow_both_current_verdicts():
    with strahler.Game.from_text(hk(4)) as game:
        stepper = Stepper(strahler.trace(game, 'zielonka-sym'))
        stepper.next('check')
        for player in (0, 1):
            verdict = stepper.verify(player)
            layer = [l for l in stepper.game_view.outlines if l['player'] == player]
            assert bool(layer) == bool(verdict.violations)
            if layer:
                assert len(layer[0]['violations']) == len(verdict.violations)
                assert stepper.tree_views[player].outlines
        before = stepper.game_view.outlines
        stepper.step()
        stepper.back()
        assert stepper.game_view.outlines == before
        stepper.end()
        assert not stepper.game_view.outlines


def test_lifting_outlines_use_the_materialized_candidate_paths():
    with strahler.Game.from_text(hk(2)) as game:
        stepper = Stepper(strahler.trace(game, 'lifting', kary=2))
        stepper.next('lift')
        for player, view in enumerate(stepper.tree_views):
            candidate = stepper.cursor.candidate(player)[0]
            expected = TreeView(candidate, game=game)
            assert view.data == expected.data
            expected.close()
