"""Check inline ESM and compare its geometry with the static SVGs."""

import ast
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest
from xml.etree import ElementTree as ET

SOURCE = Path(__file__).resolve().parents[1] / 'python/strahler/widgets.py'


def frontend():
    return next(ast.literal_eval(node.value) for node in ast.parse(SOURCE.read_text()).body
                if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == '_ESM'
                                                       for t in node.targets))


def test_no_remote_frontend_dependencies():
    javascript = frontend()
    assert 'import ' not in javascript and 'fetch(' not in javascript
    assert 'https://' not in javascript
    assert 'http://' not in javascript.replace('http://www.w3.org/2000/svg', '')
    assert 'innerHTML' not in javascript


def test_inline_esm_syntax(tmp_path):
    node = shutil.which('node')
    if not node:
        if os.environ.get('CI'):
            pytest.fail('Node is required to check inline widget ESM syntax in CI')
        pytest.skip('node is not installed; inline ESM syntax check unavailable')
    source = tmp_path / 'widgets.mjs'
    source.write_text(frontend(), encoding='utf-8')
    subprocess.run([node, '--check', str(source)], check=True, capture_output=True, text=True)


@pytest.mark.parametrize('kind,edges', [('game', True), ('squares', True), ('squares', False), ('tree', True)])
@pytest.mark.parametrize('fixture', ['fig1', 'h4', 'dense', 'violations', 'symmetric-violations'])
def test_svg_geometry_matches_frontend(tmp_path, kind, fixture, edges):
    node = shutil.which('node')
    if not node:
        if os.environ.get('CI'):
            pytest.fail('Node is required for widget layout comparisons in CI')
        pytest.skip('node is not installed; widget layout comparison unavailable')
    pytest.importorskip('anywidget')
    import strahler
    from strahler.widgets import GameView, TreeView, Stepper
    from otj22 import hk

    root = SOURCE.parents[2]
    text = ((root / 'tests/games/otj22-fig1.gm').read_text() if fixture == 'fig1' else hk(4)
            if fixture in ('h4', 'symmetric-violations') else '\n'.join(
                f'{v} 2 {v % 2} ' + ','.join(map(str, range(15))) + ';' for v in range(15)))
    if fixture == 'violations':
        text = '10 2 0 10;20 0 1 30;30 0 0 30;40 0 0 30;'
    with strahler.Game.from_text(text) as game:
        import runpy
        tree = (runpy.run_path(str(root / 'jupyter-examples/otj22.py'))['PAPER_FIG1']
                if fixture == 'fig1' else strahler.decompose(game, 'zielonka').trees[0])
        if fixture == 'violations':
            tree = dict(h=2, H=[10], T=[], S=[], children=[
                dict(h=0, H=[20], T=[], S=[], children=[]),
                dict(h=0, H=[30], T=[], S=[], children=[])])
        view = GameView(game) if kind == 'game' else TreeView(tree, game=game, mode=kind, edges=edges)
        if fixture == 'symmetric-violations':
            stepper = Stepper(strahler.trace(game, 'lifting-sym', kary=2))
            stepper.seek(389)
            assert stepper.state.event['path'] == [1, 1]
            assert stepper.verify(1).condition is strahler.Condition.TOP
            view = stepper.game_view if kind == 'game' else stepper.tree_views[1]
            if kind != 'game':
                view.mode, view.edges = kind, edges
        if kind == 'game':
            view.highlight(game.vertices[:2], '#abc', 'layer')
            view.selected = game.vertices[0]
            view.hovered = [game.vertices[-1]]
        else:
            view.highlights = [dict(name='layer', vertices=game.vertices[:2], colour='#abc')]
            child = view.data['children'][0] if view.data['children'] else view.data
            view.hovered_path = child['path']
            view.hovered = child['vertices']
            view.selected = [1]
        if fixture == 'violations':
            verdict = strahler.check(game, tree, 0)
            view.show_check(verdict, 0)
            view.show_check(verdict, 1)
        fields = ('kind', 'data', 'highlights', 'outlines', 'hovered', 'selected', 'mode', 'edges', 'hovered_path', 'owner_styles')
        state = {key: getattr(view, key) for key in fields}
        source = tmp_path / 'widgets.mjs'
        source.write_text(frontend(), encoding='utf-8')
        runner = tmp_path / 'compare.mjs'
        runner.write_text('''
import renderer from './widgets.mjs';
class Element {
  constructor(tag) { this.tag=tag; this.attrs={}; this.children=[]; this.style={}; this.classList={add(){}}; }
  set textContent(value) { this.text=String(value); }
  get textContent() { return (this.text??'')+this.children.map(c=>c.textContent).join(''); }
  setAttribute(k,v) { this.attrs[k]=String(v); }
  append(child) { this.children.push(child); }
  replaceChildren() { this.children=[]; }
  contains() { return false; }
}
globalThis.document={createElement:tag=>new Element(tag),createElementNS:(_,tag)=>new Element(tag)};
const state=JSON.parse(process.argv[2]), el=new Element('div');
renderer.render({model:{model_id:'fixture',get:key=>state[key],on(){}},el});
const groups=[], edges=[]; let viewBox;
function visit(e) {
  if(e.tag==='svg') viewBox=e.attrs.viewBox;
  if(e.attrs['data-vertex']!==undefined || e.attrs['data-path']!==undefined) {
    const id=e.attrs['data-vertex'] ?? e.attrs['data-path'];
    groups.push([id,e.children.filter(c=>['rect','polygon','text'].includes(c.tag)).map(c=>[c.tag,c.attrs,c.textContent??''])]);
  }
  if(e.tag==='line' || e.tag==='path' && e.attrs['marker-end']) edges.push([e.tag,e.attrs]);
  e.children.forEach(visit);
}
visit(el);
console.log(JSON.stringify({groups,edges,viewBox}));
''', encoding='utf-8')
        result = subprocess.run([node, str(runner), json.dumps(state)], check=True,
                                capture_output=True, text=True)
        actual = json.loads(result.stdout)
        svg = ET.fromstring(view._static_svg())
        if fixture == 'symmetric-violations' and kind != 'game':
            node = svg.find('.//{http://www.w3.org/2000/svg}g[@data-path="[1,1]"]')
            assert node is not None
            assert any(e.get('stroke') == '#b45309' for e in node)
            shapes = next(shapes for path, shapes in actual['groups'] if path == '[1,1]')
            assert any(attrs.get('stroke') == '#b45309' for tag, attrs, text in shapes)
        assert list(map(float, actual['viewBox'].split())) == list(map(float, svg.get('viewBox').split()))
        groups, edges = [], []
        for element in svg.iter():
            tag = element.tag.split('}')[-1]
            key = element.get('data-vertex', element.get('data-path'))
            if key is not None:
                groups.append([key, [[e.tag.split('}')[-1], e.attrib, ''.join(e.itertext())] for e in element
                                     if e.tag.split('}')[-1] in ('rect', 'polygon', 'text')]])
            if tag == 'line' or tag == 'path' and 'marker-end' in element.attrib:
                edges.append([tag, element.attrib])

        def normalized(rows):
            # JS and Python may print the same float differently.
            import re
            def attributes(attrs):
                return {key: ([float(n) for n in re.findall(r'-?\d+(?:\.\d+)?(?:e[+-]?\d+)?', value)]
                              if key in ('d', 'x', 'y', 'x1', 'x2', 'y1', 'y2', 'cx', 'cy',
                                         'r', 'rx', 'width', 'height', 'stroke-width', 'transform', 'points') else
                              value.replace('violation-fixture', 'strahler-violation').replace('arrow-fixture', 'strahler-arrow-' + ('game' if kind == 'game' else 'squares'))) for key, value in attrs.items()}
            return [[row[0], attributes(row[1]), *row[2:]] for row in rows]

        for expected_group, actual_group in zip(sorted(groups), sorted(actual['groups']), strict=True):
            assert expected_group[0] == actual_group[0]
            for expected, observed in zip(normalized(expected_group[1]), normalized(actual_group[1]), strict=True):
                assert expected[0] == observed[0] and expected[2:] == observed[2:]
                assert expected[1].keys() == observed[1].keys()
                for key, value in expected[1].items():
                    assert observed[1][key] == (pytest.approx(value) if isinstance(value, list) else value)
        expected_edges = sorted(normalized(edges), key=str)
        actual_edges = sorted(normalized(actual['edges']), key=str)
        for expected, observed in zip(expected_edges, actual_edges, strict=True):
            assert expected[0] == observed[0]
            assert expected[1].keys() == observed[1].keys()
            for key, value in expected[1].items():
                assert observed[1][key] == (pytest.approx(value) if isinstance(value, list) else value)
