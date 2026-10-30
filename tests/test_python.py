"""ctypes/CLI equivalence, reversible replay, original checks, and ownership."""

from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

import pytest
import strahler
from strahler import _native
from trace_replay import Replay
from otj22 import hk
from test_json import SLOW
from gen_decomp import BACKENDS, discover_games, player_stats

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('STRAHLER_BUILD_DIR', ROOT / 'build'))


def cli(name, args, text):
    return subprocess.run([str(BUILD / name), *args], input=text, text=True,
                          capture_output=True, timeout=1800)


def as_dict(node):
    return None if node is None else node.to_dict()


EXAMPLES = [pytest.param(path, backend, id=f'{path.name}/{backend.name}',
                         marks=pytest.mark.slow if (path.name, backend.name) in SLOW else ())
            for path in discover_games([ROOT / 'examples']) for backend in BACKENDS]


@pytest.mark.parametrize('path,backend', EXAMPLES)
def test_examples_match_cli_bytes(path, backend):
    text = path.read_text()
    flags = ['--format=json', *backend.options]
    expected = cli(backend.binary, flags, text)
    with strahler.Game.from_text(text) as game:
        if expected.returncode:
            with pytest.raises(strahler.StrahlerError) as failure:
                if backend.binary == 'pg2adot':
                    strahler.decompose(game, args=backend.options)
                else:
                    strahler.progress_measures(game, args=backend.options)
            assert failure.value.status == expected.returncode
            return
        if backend.binary == 'pg2adot':
            result = strahler.decompose(game, args=backend.options)
            assert result.to_json() == expected.stdout
            for p in range(2):
                assert strahler.check(game, result.trees[p], p, result.regions[p],
                                      compress='--compress' in backend.options).ok
        else:
            args = _native.options(backend.options, {})
            assert _native.call('progress_measures', game._pointer(), args, len(args)) == expected.stdout
            assert strahler.progress_measures(game, args=backend.options) == json.loads(expected.stdout)


@pytest.mark.parametrize('args', [(), ('--tree-k=1',), ('--kary=1',),
    ('--tree-file=' + str(ROOT / 'tests/trees/one_child.tree'),),
    ('--adaptive-t',), ('--adaptive-t=incremental',), ('--adaptive-t', '--tree-k=1'),
    ('--adaptive-t', '--max-host-leaves=1')])
@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
@pytest.mark.parametrize('iteration', [False, True])
def test_host_modes(args, algorithm, iteration):
    text = (ROOT / 'tests/games/strahler_two.pg').read_text()
    flags = [f'--algorithm={algorithm}', '--stats', *args, *(['--iter-shave'] if iteration else [])]
    expected = cli('pg2adot', ['--format=json', *flags], text)
    with strahler.Game.from_text(text) as game:
        if algorithm == 'lifting-sym' and any(a.startswith('--adaptive-t') for a in args):
            assert expected.returncode == 2
            assert 'adaptive' in expected.stderr
            for run in (strahler.decompose, strahler.trace):
                with pytest.raises(strahler.InputError, match='adaptive') as error:
                    run(game, args=flags)
                assert error.value.status == expected.returncode
            return
        result = strahler.decompose(game, args=flags)
        assert expected.returncode == 0, expected.stderr
        assert result.to_json() == expected.stdout
        reported = player_stats(expected.stderr)
        assert result.data['parameters'] == [
            {key: int(stats[key]) for key in ('H', 'K', 't', 'arity') if key in stats}
            for stats in reported]
        assert result.data['budget_exhausted'] == ('budget_exhausted=yes' in expected.stderr)
        observed = strahler.trace(game, args=flags)
        assert observed.state(-1).trees == observed.result.trees == result.trees


@pytest.mark.parametrize('algorithm', ['zielonka', 'zielonka-sym', 'lifting', 'lifting-sym'])
@pytest.mark.parametrize('compress', [False, True])
def test_every_trace_state(algorithm, compress, tmp_path):
    text = (ROOT / 'tests/games/otj22-fig1.gm').read_text()
    with strahler.Game.from_text(text) as game:
        traced = strahler.trace(game, algorithm, compress=compress)
        replay = Replay()
        snapshots = []
        for i, event in enumerate(traced.events):
            replay.apply(event)
            state = traced.state(i)
            expected = [replay.materialized(p) if event['event'] == 'result' else replay.tree(p)
                        for p in range(2)]
            assert [as_dict(tree) for tree in state.trees] == expected
            snapshots.append(state.trees)
            if event['event'] == 'check':
                candidate = state.candidate(event['player'], event['path'], event['subgame'])
                result = strahler.check(game, candidate, event['player'], event['subgame'], compress=compress)
                assert result.ok == event['is_attractor_decomposition']
        assert traced.state(-1).trees == traced.result.trees
        for i in range(len(traced) - 1, -1, -1):
            assert traced.state(i).trees == snapshots[i]
        assert all(actual.ok == event['is_attractor_decomposition']
                   for event, actual in traced.check_claims())
        assert len(list(traced.steps('call'))) == sum(e['event'] == 'call' for e in traced.events)
        assert snapshots[-1] == traced.result.trees
        with pytest.raises(IndexError):
            traced.state(len(traced))
        trace_path = tmp_path / 'trace.jsonl'
        flags = [f'--algorithm={algorithm}', *(['--compress'] if compress else [])]
        expected = cli('pg2adot', ['--format=json', *flags, f'--trace={trace_path}'], text)
        assert expected.returncode == 0, expected.stderr
        events = [json.loads(line) for line in trace_path.read_text().splitlines()]
        events[0]['options'] = traced.events[0]['options']
        assert events == list(traced.events)


@pytest.mark.parametrize('algorithm,expected', [
    ('zielonka', [10, 50, 218, 898, 3626, 14546]),
    ('zielonka-sym', [7, 23, 47, 83, 131, 191])])
def test_hk_paper_calls(algorithm, expected):
    actual = []
    for k in range(2, 13, 2):
        with strahler.Game.from_text(hk(k)) as game:
            traced = strahler.trace(game, algorithm)
            counts = Counter(e['event'] for e in traced.events)
            actual.append(counts['call'])
            assert counts['result'] == counts['header'] == 1
    assert actual == expected


def test_located_errors_and_restriction():
    with strahler.Game.from_text('0 2 0 0;1 0 0 1;2 0 0 2;3 1 1 3;') as game:
        tree = dict(h=2, H=[0], T=[], S=[], children=[
            dict(h=0, H=[1, 2], T=[], S=[], children=[])])
        valid = strahler.check(game, tree, 0, [0, 1, 2])
        assert valid.ok and valid.condition is strahler.Condition.OK
        assert valid.condition.name == 'OK' and int(valid.condition) == 0
        assert strahler.check(game, tree, 0, []).ok
        # A child with the parent's bound breaks Definition 1 at that child.
        tree['children'][0]['h'] = 2
        error = strahler.check(game, tree, 0, [0, 1, 2])
        assert not error.ok and error.condition == 8 and error.path == (0,)
        assert error.condition is strahler.Condition.CHILD
        tree['children'][0]['h'] = 0
        tree['h'] = 0
        tree['children'] = []
        tree['H'] = [0, 1, 2]
        error = strahler.check(game, tree, 0, [0, 1, 2])
        assert not error.ok and error.condition == 4 and error.vertex == 0 and error.path == ()
        assert error.condition is strahler.Condition.PRIORITY
        assert error.message
        assert strahler.check(game, None, 0, []).ok
        assert not strahler.check(game, None, 0, [0]).ok


def test_relative_parts_are_checked():
    with strahler.Game.from_text('0 0 0 0;') as game:
        escaped = r'{"\u0068":0,"H":[0],"T":[],"S":[],"children":[]}'
        assert strahler.check(game, escaped, 0).ok
        tree = dict(h=0, H=[], T=[0], S=[], children=[])
        error = strahler.check(game, tree, 0)
        assert (error.ok, error.condition, error.path, error.vertex) == (False, 5, (), 0)
        tree.update(H=[0], T=[], S=[0])
        error = strahler.check(game, tree, 0)
        assert (error.ok, error.condition, error.path, error.vertex) == (False, 10, (), 0)


def test_partial_labels():
    with strahler.Game.from_text('0 2 1 1;1 1 1 1;2 1 1 2;') as game:
        labels = dict(player=0, h=2, tree='0|1|', labels=[
            dict(vertex=1, path=[0], part='S'), dict(vertex=2, path=[1], part='S')])
        result = strahler.materialize_partial(game, labels)
        assert [(p['condition'], p['path'], p['vertex']) for p in result['problems']] == [
            (22, [0], 1), (22, [1], 2)]
        assert len(result['tree']['children']) == 2
        labels['labels'][0]['part'] = 'H'
        with pytest.raises(strahler.InputError, match='compatible'):
            strahler.materialize_partial(game, labels)


def test_game_and_generators():
    with strahler.Game.from_text('parity 99;start 42;42 12 1 99 "a";99 3 0 42;') as game:
        assert game.vertices == (42, 99)
        assert game.edges == ((42, 99), (99, 42))
        assert game.start == 42 and game.priority == {42: 12, 99: 3}
        assert game.owner == {42: 1, 99: 0}
        assert game.successors == {42: (99,), 99: (42,)}
        assert game.names == {42: 'a', 99: None}
    with pytest.raises(ValueError, match='closed'):
        strahler.decompose(game)
    project = json.loads((BUILD / 'meson-info/intro-projectinfo.json').read_text())
    assert strahler.version() == project['version']
    actual = strahler.genstree(2, 3, 1)
    expected = cli('genstree', ['--json', '--strahler=2', '--height=3', '--log-leaves=1'], '')
    assert actual == json.loads(expected.stdout)
    assert strahler.pms_tree('0|1|') == json.loads(cli('pms2dot', ['--format=json'], '0|1|').stdout)
    assert strahler.pms_tree('1|0|', reorder=True)['tree'] == strahler.pms_tree('0|1|')['tree']


def test_errors():
    for text in ('bad', '', '0 0 0 7;', '0 0 0 0;\0junk'):
        with pytest.raises(strahler.InputError):
            strahler.Game.from_text(text)
    with strahler.Game.from_text('0 0 0 0;') as game:
        for flags in (['--algorithm=bad'], ['--format=dot'], ['--trace=x'], ['--labels=sets'], ['--adaptive-t=oops']):
            with pytest.raises(strahler.InputError):
                strahler.decompose(game, args=flags)
        for tree in ('{', '{}', '{"h":0,"h":0}', '{"h":-1}', 'null trailing', 'null\0oops', '[' * 1100 + 'null' + ']' * 1100):
            with pytest.raises(ValueError):
                strahler.check(game, tree, 0)
        with pytest.raises(strahler.StrahlerError, match='limit') as error:
            strahler.trace(game, trace_limit=1)
        assert error.value.events[-1]['event'] == 'truncated'
        with pytest.raises(strahler.InputError):
            strahler.progress_measures(game, bits=3)
        with pytest.raises(ValueError):
            strahler.decompose(game, args=['--compress\0foo'])
    with pytest.raises(strahler.InputError):
        strahler.genstree(0, 1, 0)
    with pytest.raises(ValueError):
        strahler.genstree(2**32, 1, 0)
    with pytest.raises(strahler.StrahlerError):
        strahler.pms_tree('bad')


def test_concurrent_calls():
    with strahler.Game.from_file(ROOT / 'tests/games/otj22-fig1.gm') as game:
        algorithms = ['zielonka', 'zielonka-sym', 'lifting', 'lifting-sym'] * 5
        expected = {a: strahler.decompose(game, a).to_json() for a in set(algorithms)}
        with ThreadPoolExecutor(max_workers=4) as pool:
            actual = list(pool.map(lambda a: strahler.decompose(game, a).to_json(), algorithms))
        assert actual == [expected[a] for a in algorithms]
        assert game.priority[2] == 5


def test_repeated_calls_rss():
    statm = Path('/proc/self/statm')
    if not statm.exists():
        pytest.skip('Linux RSS check; native ownership also runs under sanitizers')

    def batch(count):
        for _ in range(count):
            with strahler.Game.from_text('0 2 0 0;1 1 1 1;') as game:
                result = strahler.decompose(game, 'zielonka')
                assert strahler.check(game, result.trees[0], 0, result.regions[0]).ok
                assert strahler.trace(game, 'zielonka-sym').state(-1).trees
                strahler.progress_measures(game)
                strahler.genstree(1, 2, 0)
                strahler.pms_tree('0|1|')
    batch(100)
    before = int(statm.read_text().split()[1]) * os.sysconf('SC_PAGE_SIZE')
    batch(1000)
    after = int(statm.read_text().split()[1]) * os.sysconf('SC_PAGE_SIZE')
    assert after - before < 8 * 1024 * 1024, (before, after)


def test_versioned_library_loader(tmp_path):
    source = Path(_native.library()._name).resolve()
    shutil.copyfile(source, tmp_path / source.name)
    for override in ('.', source.name):
        env = dict(os.environ, STRAHLER_LIB=override)
        completed = subprocess.run([sys.executable, '-c',
            'import strahler; assert strahler.version()'], env=env, cwd=tmp_path,
            capture_output=True, text=True, timeout=10)
        assert completed.returncode == 0, completed.stderr


@pytest.mark.parametrize('version', [b'999.0.0', b'0.5.0', b'0.3', b'0.3.bad', None])
def test_incompatible_library_rejected(version, monkeypatch):
    from unittest.mock import Mock

    lib = Mock()
    lib.strahler_version.return_value = version
    monkeypatch.setattr(_native, '_library', None)
    monkeypatch.setattr(_native.C, 'CDLL', lambda path: lib)
    with pytest.raises(ImportError, match='ABI.*0\\.3'):
        _native.library()
    assert _native._library is None
    lib.strahler_version.assert_called_once_with()


@pytest.mark.parametrize('version', [None, b'0.4.0', b'0.4.1.dev2+g1234567.dirty'])
def test_missing_abi_symbol_rejected(version, monkeypatch):
    from types import SimpleNamespace
    from unittest.mock import Mock

    lib = SimpleNamespace(strahler_version=Mock(return_value=version or strahler.version().encode()))
    monkeypatch.setattr(_native, '_library', None)
    monkeypatch.setattr(_native.C, 'CDLL', lambda path: lib)
    with pytest.raises(ImportError, match='missing.*strahler_game_parse'):
        _native.library()
    assert _native._library is None


def test_split_wheel_library_lookup(tmp_path, monkeypatch):
    source = Path(_native.library()._name).resolve()
    directory = tmp_path / 'platlib' / '.strahler.mesonpy.libs'
    directory.mkdir(parents=True)
    target = directory / source.name
    shutil.copyfile(source, target)
    monkeypatch.delenv('STRAHLER_LIB', raising=False)
    monkeypatch.setattr(_native, '__file__', str(tmp_path / 'purelib/strahler/_native.py'))
    monkeypatch.setattr(_native.sysconfig, 'get_path', lambda name: str(directory.parent))
    monkeypatch.setattr(_native, '_library', None)
    assert Path(_native.library()._name) == target


@pytest.mark.parametrize('options', [dict(player='even'), dict(player='odd'),
    dict(args=['--player=even']), dict(args=['--player=odd'])])
def test_trace_requires_both_players(options):
    with strahler.Game.from_text('0 0 0 0;1 1 1 1;') as game:
        with pytest.raises(ValueError, match='both'):
            strahler.trace(game, 'zielonka', **options)
        for both in ({}, dict(player='both'), dict(args=['--player=both'])):
            run = strahler.trace(game, 'zielonka', **both)
            assert all(tree is not None for tree in run.result.trees)
            assert run.state(-1).trees == run.result.trees


def test_deep_prefix_rejected_without_crashing():
    completed = subprocess.run([sys.executable, '-c', '''
import resource
import strahler
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
try:
    resource.setrlimit(resource.RLIMIT_STACK, (min(8 * 1024 * 1024, hard) if hard >= 0 else 8 * 1024 * 1024, hard))
except ValueError:
    # macOS rejects re-setting an unlimited hard stack limit; its default soft limit is 8 MiB.
    assert 0 <= soft <= 8 * 1024 * 1024, soft
try:
    strahler.pms_tree('e,' * 150000 + 'e|')
except strahler.InputError as error:
    assert error.status == 2 and 'depth' in str(error)
else:
    raise AssertionError('deep prefix accepted')
with strahler.Game.from_text('0 0 0 0;') as game:
    labels = dict(player=0, h=0, tree='e,' * 150000 + 'e|', labels=[])
    try:
        strahler.materialize_partial(game, labels)
    except strahler.InputError:
        pass
    else:
        raise AssertionError('deep label host accepted')
'''], capture_output=True, text=True, timeout=15)
    assert completed.returncode == 0, (completed.returncode, completed.stderr)


def test_all_violations_keep_first_failure_and_external_edges():
    with strahler.Game.from_text('10 2 0 10;20 0 1 30;30 0 0 30;40 0 0 30;') as game:
        child = lambda vertices: dict(h=0, H=vertices, T=[], S=[], children=[])
        tree = dict(h=2, H=[10], T=[], S=[], children=[child([20]), child([30])])
        result = strahler.check(game, tree, 0)
        assert (result.condition, result.path, result.vertex) == (strahler.Condition.TRAP, (0,), 20)
        assert result.violations == (
            strahler.Violation(strahler.Condition.TRAP, (0,), 20, (20, 30)),
            strahler.Violation(strahler.Condition.ATTRACTOR, (1,), 40, (40, 30)))
        tree['children'][0]['H'] = []
        # The first child is pruned; the later failure keeps its original index.
        restricted = strahler.check(game, tree, 0, [10, 30, 40])
        assert restricted.path == restricted.violations[0].path == (1,)
        tree['children'][1]['S'] = [40]
        assert strahler.check(game, tree, 0, [10, 30, 40]).violations == ()
