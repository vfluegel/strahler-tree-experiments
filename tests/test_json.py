#!/usr/bin/env python3
"""Validate documents against DOT, metrics, trace results, and CLI failures."""

from collections import defaultdict
import html
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from threading import Thread

from json_check import document
from test_trace import ROOT, SLOW_PAIRS
from gen_decomp import (BACKENDS, Backend, FIXED_HOST_EXAMPLES, discover_games,
                        game_info, tree_metrics)

# These additional PM runs already exceed DECOMP's ordinary budget.
SLOW = SLOW_PAIRS | {(game, backend + suffix)
                     for game, backend in (
                         ('cabp_swp.ehoa.pg', 'pm-strahler'),
                         ('cabp_swp.ehoa.pg', 'pm-succinct'),
                         ('FairSchedulern=2_compact.gm', 'pm-strahler'),
                         ('FairSchedulern=2_compact.gm', 'pm-succinct'))
                     for suffix in ('', '+pre', '+iter', '+pre+iter')}


def run(binary, options=(), data=None):
    return subprocess.run([str(binary), *map(str, options)], input=data,
                          text=True, capture_output=True, timeout=1800)


def load(text):
    assert text.endswith('\n') and text.count('\n') == 1
    assert not any(line.rstrip() != line for line in text.splitlines())
    value = json.loads(text)
    document(value)
    return value


def json_bytes(value):
    return json.dumps(value, separators=(',', ':'), ensure_ascii=False)


def sets(text):
    return [list(map(int, values.split(','))) if values else []
            for values in re.findall(r'\{([\d,]*)\}', text)]


def compare_ad(value, dot):
    nodes, domains = {}, {}
    for name, label in re.findall(r'^  (\w+) \[label=(.*)\];$', dot, re.M):
        text = html.unescape(re.sub(r'<[^>]*>', '', label))
        if 'd = ' in text:
            data = sets(text)
            assert len(data) == 4, text
            domains[name] = data[0]
            nodes[name] = dict(h=int(re.search(r'd = (\d+)', text)[1]),
                               H=data[1], T=data[2], S=data[3], children=[])
    edges = re.findall(r'^  (\w+) -> (\w+) \[label=(.*)\];$', dot, re.M)
    for parent, child, label in edges:
        if parent in nodes and child in nodes:
            nodes[parent]['children'].append(nodes[child])
            assert sets(label) == [domains[child]], (parent, child)
    def check(result, suffix=''):
        for index, player in enumerate(('even', 'odd')):
            assert result['trees'][index] == nodes.get(player + suffix), (player, suffix)
            assert result['regions'][index] == domains.get(player + suffix, []), (player, suffix)
        for index, shave in enumerate(result['shaves'], 1):
            check(shave['certified'], f'_round_{index}')
            for player, attracted in zip(('even', 'odd'), shave['attracted']):
                if attracted:
                    line = next(line for line in dot.splitlines() if line.startswith(f'  {player}_shaved_{index} ['))
                    assert sets(line) == [attracted]
    check(value)
    for player, won in zip(('even', 'odd'), value['initial_won']):
        if won:
            line = next(line for line in dot.splitlines() if line.startswith(f'  {player}_shaved ['))
            assert sets(line) == [won]
    if value['unresolved']:
        line = next(line for line in dot.splitlines() if line.startswith('  unresolved ['))
        assert sets(line) == [value['unresolved']]
    def check_metrics(result, reported):
        for node, actual in zip(result['trees'], reported['trees']):
            if actual is None:
                continue
            children = {}
            def visit(node):
                children[id(node)] = [id(c) for c in node['children']]
                for child in node['children']:
                    visit(child)
            if node:
                visit(node)
            expected, _ = tree_metrics(children, id(node) if node else None)
            assert actual.keys() == expected.keys()
            for key in expected:
                assert abs(actual[key] - expected[key]) < .00051, (key, actual, expected)
        for shave, metrics_ in zip(result['shaves'], reported['shaves'], strict=True):
            check_metrics(shave['certified'], metrics_)
    check_metrics(value, value['metrics'])


def compare_prefix(tree, dot):
    edges = defaultdict(list)
    for parent, child, label in re.findall(r'^\s*(\d+) -- (\d+) \[label=(".*")\];$', dot, re.M):
        edges[parent].append((json.loads(label), child))
    def visit(name):
        return {'children': [{'label': label, 'node': visit(child)}
                             for label, child in edges[name]]}
    assert tree == visit('1')


def compare_pm(value, dot):
    graphs = dot.split('digraph progress_measures {')[1:]
    assert len(graphs) == len(value['rounds'])
    for round_, graph in zip(value['rounds'], graphs):
        labels = dict(re.findall(r'^  (n\d+) \[.*?label="(.*)"\];$', graph, re.M))
        edges = defaultdict(list)
        for parent, child, label in re.findall(r'^  (n\d+) -> (n\d+) \[label=(".*")\];$', graph, re.M):
            edges[parent].append((json.loads(label), child))
        def visit(name):
            label = labels[name]
            if 'no non-top measures' in label:
                return None
            priority = re.search(r'(?:^|\\n)p=(\d+)', label)
            vertices = sets(label)
            return dict(priority=int(priority[1]) if priority else None,
                        vertices=vertices[0] if vertices else None,
                        children=[{'label': label, 'node': visit(child)} for label, child in edges[name]])
        for player, tree in zip(('Even', 'Odd'), round_['trees']):
            if tree is None:
                continue
            root = next(name for name, label in labels.items() if label.startswith(player + ' ('))
            assert tree['tree'] == visit(root)
            params = dict((key, int(number)) for key, number in re.findall(r'(k|t|bits|depth)=(\d+)', labels[root]))
            assert all(tree['parameters'][key] == number for key, number in params.items())
            for kind in ('shaved', 'attracted'):
                found = [sets(label)[0] for label in labels.values() if label.startswith(kind + ' (' + player + ')')]
                assert found == ([tree[kind]] if tree[kind] else [])
        unresolved = re.search(r'Unresolved\\nU=(\{[\d,]*\})', graph)
        assert round_['unresolved'] == (sets(unresolved[1])[0] if unresolved else [])
        assert round_['complete'] == ('synthetic; partial' not in graph)


def compare_trace(build, backend, game, directory):
    # Drain full traces on small inputs; the counting observer retains large results.
    fifo = directory / 'trace.fifo'
    os.mkfifo(fifo)
    final = []
    def drain():
        with fifo.open() as stream:
            for line in stream:
                if line.startswith('{"event":"result"'):
                    final.append(line)
    reader = Thread(target=drain, daemon=True)
    reader.start()
    large = game.parent == ROOT / 'examples' or game.name == 'thesis_f4.pg'
    binary = build / ('trace_count_pg2adot' if large else 'pg2adot')
    if large:
        print(f'counting observer with production result writer: {game.name}/{backend.name}', flush=True)
    try:
        observed = run(binary, [*backend.options, '--priority-mode=compact',
                       '--format=json', f'--trace={fifo}',
                       '--trace-limit=18446744073709551615', game])
    finally:
        reader.join(timeout=30)
        fifo.unlink()
    assert not reader.is_alive()
    if observed.returncode:
        assert observed.returncode in (3, 4) and not final, observed.stderr
        print(f'EXPECTED FAILURE (trace): {game.name}/{backend.name}: {observed.returncode}', flush=True)
        return
    value = load(observed.stdout)
    assert len(final) == 1
    event = json.loads(final[0])
    assert {key: value[key] for key in event['result']} == event['result']
    assert value['initial_won'] == event['initial_won']
    # The complete nested result fragment checks every tree, including shaves.
    fragment = json_bytes(event['result'])
    assert fragment in final[0]
    assert fragment[1:-1] in observed.stdout
    print(f'trace bytes passed: {game.name}/{backend.name}', flush=True)


def check_measure_prefixes(build, value):
    def stream(node, path=()):
        if not node['children']:
            return ','.join(part or 'e' for part in path) + '|'
        return ''.join(stream(branch['node'], (*path, branch['label']))
                       for branch in node['children'])

    def shape(node):
        return {'children': [{'label': branch['label'], 'node': shape(branch['node'])}
                             for branch in node['children']]}

    for round_ in value['rounds']:
        for player in round_['trees']:
            if player is None or player['tree'] is None:
                continue
            data = stream(player['tree']) + '\n'
            binary = build / 'pms2dot'
            output = run(binary, ['--format=json', '--check-order'], data)
            dot = run(binary, ['--format=dot', '--check-order'], data)
            assert output.returncode == dot.returncode == 0, (output.stderr, dot.stderr)
            tree = load(output.stdout)['tree']
            assert tree == shape(player['tree'])
            compare_prefix(tree, dot.stdout)


def example_options(backend, game):
    options = list(backend.options)
    if game.name in FIXED_HOST_EXAMPLES and backend.name.startswith('pm-strahler'):
        # These fixtures test fixed hosts, rather than unrestricted searches.
        options += ['--k=3', '--t=2']
    return options


def check_game(build, backend, game, directory, priority="compact"):
    options = [*example_options(backend, game), f'--priority-mode={priority}']
    if game.name == 'cabp_swp.ehoa.pg' and backend.name in ('pm-strahler+iter', 'pm-strahler+pre+iter'):
        options += ['--k=1', '--t=1']
    binary = build / backend.binary
    dot_options = ['--format=dot', '--labels=sets', '--max-set-items=2147483647']
    if backend.binary == 'pg2adot':
        dot_options += ['--view=tree-relative']
    dot = run(binary, [*options, *dot_options, game])
    output = run(binary, [*options, '--format=json', game])
    assert (dot.returncode, dot.stderr) == (output.returncode, output.stderr), (game, backend.name, dot.stderr, output.stderr)
    if dot.returncode:
        assert dot.returncode in (3, 4), (game, backend.name, dot)
        print(f'EXPECTED FAILURE: {game.name}/{backend.name}: {dot.returncode} {dot.stderr.strip()}', flush=True)
        return
    value = load(output.stdout)
    summary = game_info(game)
    assert (value['game']['vertices'], value['game']['edges'], len(value['game']['priorities']), max(value['game']['priorities'], default=0)) == summary, (value['game'], summary)
    if backend.binary == 'pg2adot':
        compare_ad(value, dot.stdout)
        # Full traces for the large fixtures are registered in the slow suite.
        if game.parent != ROOT / 'examples' and game.name != 'thesis_f4.pg':
            compare_trace(build, backend, game, directory)
        else:
            print(f'result-event comparison registered in slow suite: {game.name}/{backend.name}', flush=True)
    else:
        compare_pm(value, dot.stdout)
        check_measure_prefixes(build, value)
    print(f'passed: {game.name}/{backend.name}', flush=True)


def tools(build):
    cases = {(k, h, t) for k, h, t in itertools.product(range(1, 4), range(1, 5), range(3)) if k <= h}
    for path in (ROOT / 'examples').glob('*.hpp'):
        k, t, h = map(int, re.match(r'k(\d+)t(\d+)h(\d+)', path.name).groups())
        cases.add((k, h, t))
    for k, h, t in sorted(cases):
        options = [f'--strahler={k}', f'--height={h}', f'--log-leaves={t}']
        output = run(build / 'genstree', [*options, '--json'])
        assert output.returncode == 0, output.stderr
        value = load(output.stdout)
        dot = run(build / 'genstree', [*options, '--dot'])
        assert dot.returncode == 0
        compare_prefix(value['tree'], dot.stdout)
    for path in sorted((ROOT / 'tests/trees').glob('*.tree')):
        data = path.read_text()
        for options in ([], ['--reorder'], ['--check-order']):
            dot = run(build / 'pms2dot', ['--format=dot', *options], data)
            output = run(build / 'pms2dot', ['--format=json', *options], data)
            assert (output.returncode, output.stderr) == (dot.returncode, dot.stderr)
            if not dot.returncode:
                compare_prefix(load(output.stdout)['tree'], dot.stdout)
            else:
                assert dot.returncode == 1
                print(f'EXPECTED FAILURE: {path.name} {options}: {dot.stderr.strip()}', flush=True)
    # Explicit epsilon and bit ordering.
    for data in ('|\n', 'e|\n', '00,e|0,e|e,e|10,e|1,e|\n', '1,0|e,1|0,1|e,0|\n'):
        dot = run(build / 'pms2dot', ['--reorder'], data)
        output = run(build / 'pms2dot', ['--format=json', '--reorder'], data)
        assert output.returncode == dot.returncode == 0
        compare_prefix(load(output.stdout)['tree'], dot.stdout)


def limits(build):
    game = ROOT / 'tests/games/otj22-fig1.gm'
    params = ['--strahler=2', '--height=3', '--log-leaves=1']
    for option in ('--view=classic', '--labels=sets', '--max-set-items=32'):
        for order in ([option, '--format=json'], ['--format=json', option]):
            result = run(build / 'pg2adot', [*order, game])
            assert result.returncode == 2 and 'conflicts' in result.stderr
    for option in ('--dot', '--count', '--leaf=1', '--p-level=1'):
        result = run(build / 'genstree', [*params, '--json', option])
        assert result.returncode == 2 and 'exclusive' in result.stderr
    for tool in ('pg2adot', 'pg2pmdot', 'pms2dot'):
        assert run(build / tool, ['--format=bad']).returncode == 2
    for tool, options, data, statuses in (
            ('pg2adot', ['--algorithm=zielonka', '--format=json', game], None, (5,)),
            ('pg2pmdot', ['--format=json', game], None, (4, 5)),
            ('genstree', [*params, '--json'], None, (1,)),
            ('pms2dot', ['--format=json'], '0|1|\n', (1,))):
        for format_ in ('dot', 'json'):
            arguments = [('--' + format_ if option == '--json' else option.replace('--format=json', '--format=' + format_))
                         for option in map(str, options)]
            with open('/dev/full' if Path('/dev/full').exists() else os.devnull, 'wb' if Path('/dev/full').exists() else 'rb') as sink:
                result = subprocess.run([str(build / tool), *arguments], input=data,
                                        text=True, stdout=sink, stderr=subprocess.PIPE, timeout=30)
            assert result.returncode in statuses, (tool, format_, result)
    for tool in ('pg2adot', 'pg2pmdot'):
        for player in ('even', 'odd'):
            value = load(run(build / tool, ['--format=json', f'--player={player}', game]).stdout)
            trees = value['trees'] if tool == 'pg2adot' else value['rounds'][-1]['trees']
            assert trees[1 if player == 'even' else 0] is None
    for opts in (['--tree-k=1'], ['--adaptive-t', '--tree-k=1'], ['--kary=1'],
                 ['--tree-file=' + str(ROOT / 'tests/trees/chain_three.tree')],
                 ['--adaptive-t=incremental']):
        backend = Backend(str(opts), 'pg2adot', tuple(opts))
        with tempfile.TemporaryDirectory(dir=build) as temp:
            check_game(build, backend, ROOT / 'tests/games/strahler_two.pg', Path(temp))

    with tempfile.TemporaryDirectory(dir=build) as temp:
        directory = Path(temp)
        quoted = directory / 'game "quoted"\\\t\n.pg'
        quoted.write_bytes(game.read_bytes())
        for tool in ('pg2adot', 'pg2pmdot'):
            output = run(build / tool, ['--format=json', quoted])
            assert output.returncode == 0, output.stderr
            assert load(output.stdout)['options'][-1] == str(quoted)
        for name in ('pm-strahler', 'pm-succinct'):
            backend = next(b for b in BACKENDS if b.name == name + '+pre')
            check_game(build, backend, ROOT / 'tests/games/compress.pg', directory, priority='original')
        for tool, opts in (('pg2adot', ['--tree-k=1']),
                           ('pg2pmdot', ['--k=1', '--t=1'])):
            partial = load(run(build / tool, [*opts, '--format=json', ROOT / 'tests/games/strahler_two.pg']).stdout)
            assert not (partial['complete'] if tool == 'pg2adot' else partial['rounds'][-1]['complete'])
        compact = load(run(build / 'pg2adot', ['--format=json', '--priority-mode=compact', game]).stdout)
        original = load(run(build / 'pg2adot', ['--format=json', '--priority-mode=original', game]).stdout)
        assert original['trees'] == compact['trees']


def goldens(build):
    cases = [('pg2adot_zielonka', 'pg2adot', ['--algorithm=zielonka', '--format=json', 'tests/games/otj22-fig1.gm'], None),
             ('pg2adot_zielonka_sym', 'pg2adot', ['--algorithm=zielonka-sym', '--format=json', 'tests/games/otj22-fig1.gm'], None),
             ('pg2pmdot', 'pg2pmdot', ['--format=json', 'tests/games/otj22-fig1.gm'], None),
             ('genstree', 'genstree', ['--strahler=2', '--height=3', '--log-leaves=1', '--json'], None),
             ('pms2dot', 'pms2dot', ['--format=json'], '0,e|1,e|\n')]
    for name, tool, options, data in cases:
        result = run(build / tool, options, data)
        assert result.returncode == 0, result.stderr
        value = load(result.stdout)
        value['version'] = '<version>'
        actual = json.dumps(value, indent=2) + '\n'
        path = ROOT / 'tests/golden' / ('json_' + name + '.json')
        assert actual == path.read_text(), name


def main():
    build = Path(sys.argv[1]).resolve()
    mode = sys.argv[2]
    if mode in ('tools', 'limits', 'goldens'):
        globals()[mode](build)
        return
    backend = next(b for b in BACKENDS if b.name == sys.argv[3])
    games = discover_games([ROOT / ('examples' if mode == 'examples' else 'tests/games')])
    if mode in ('slow', 'trace'):
        games = [next(p for p in [ROOT / 'examples' / sys.argv[4], ROOT / 'tests/games' / sys.argv[4]] if p.is_file())]
    with tempfile.TemporaryDirectory(dir=build) as temp:
        checked = 0
        for game in games:
            if mode not in ('slow', 'trace') and (game.name, backend.name) in SLOW:
                print(f'delegated to slow suite: {game.name}/{backend.name}', flush=True)
                continue
            if mode == 'trace':
                compare_trace(build, backend, game, Path(temp))
            else:
                check_game(build, backend, game, Path(temp))
            checked += 1
        assert checked


if __name__ == '__main__':
    main()
