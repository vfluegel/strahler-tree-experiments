"""Check bounded family reporting, fixture identity, and honest map coverage."""

import bz2
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'jupyter-examples'))
import family_benchmarks as family
import fixed_pair_maps as fixed


def test_fixed_reference_is_capped_cached_and_separate(monkeypatch):
    fixed._reference_winners.cache_clear()
    calls = []

    def run(command, **options):
        calls.append((json.loads(options['input']), options['timeout']))
        return SimpleNamespace(returncode=0, stdout=json.dumps(
            dict(outcome='solved', won_vertices=[[0], [1]])))

    monkeypatch.setattr(fixed.subprocess, 'run', run)
    for _ in range(2):
        result = fixed.reference_winners('game.pg', 2, 0.01, 2)
        assert result['outcome'] == 'solved' and 'seconds' in result
    assert len(calls) == 1
    request, timeout = calls[0]
    assert request['backend'] == 'reference' and request['memory_gib'] == 2
    assert timeout == 0.01
    fixed._reference_winners.cache_clear()


@pytest.mark.parametrize('changed', ['fixtures', 'jobs', 'lifting_backends', 'sidecar'])
def test_fixed_resume_rejects_changed_conditions(tmp_path, monkeypatch, changed):
    fixture = dict(file='game.pg', sha256='A')
    provenance = dict(library_sha256='library', worker_sha256='worker',
                      lifting_backends={'STRAHLER_AD_BACKEND': 'scalar'})
    monkeypatch.setattr(fixed, 'manifest', lambda: [fixture])
    monkeypatch.setattr(fixed, 'native_provenance', lambda: provenance)
    monkeypatch.setattr(fixed, 'measure', lambda *args: dict(outcome='unsolved', seconds=0))
    output = tmp_path / 'checkpoint.jsonl'
    walk = lambda measured, n, backend: measured(n, backend, 1, 0)
    fixed.run(walk, (4,), 60, 2, 1, output, ('PM',))
    if changed == 'fixtures':
        fixture['sha256'] = 'B'
    elif changed == 'lifting_backends':
        provenance['lifting_backends'] = {'STRAHLER_AD_BACKEND': 'packed'}
    elif changed == 'sidecar':
        output.with_suffix('.metadata.json').unlink()
    with pytest.raises((AssertionError, ValueError)):
        fixed.run(walk, (4,), 60, 2, 2 if changed == 'jobs' else 1, output, ('PM',))


@pytest.fixture
def case_request(tmp_path):
    pytest.importorskip('ijson')
    build = Path(os.environ.get('STRAHLER_BUILD_DIR', ROOT / 'build')).resolve()
    if not (build / 'pg2adot').exists():
        pytest.skip('Native CLI build needed for streaming benchmark checks')
    fixture = tmp_path / 'game.pg.bz2'
    fixture.write_bytes(bz2.compress((ROOT / 'tests/games/lifted_shape.pg').read_bytes()))
    return dict(path=str(fixture), game=dict(vertices=5, sha256=family.sha256(fixture)),
                build_dir=str(build), cache=str(tmp_path), memory_gib=2)


@pytest.mark.parametrize('backend', family.BACKENDS)
@pytest.mark.parametrize('order', family.ORDERS)
def test_streamed_cli_checks_all_wins_against_a_bounded_reference(case_request, backend, order, tmp_path):
    reference = tmp_path / 'winners.gz'
    truth = family.measure(dict(case_request, backend='reference', save_reference=str(reference)), 10)
    assert truth['outcome'] == 'solved' and truth['won'] == [5, 0]
    result = family.measure(dict(case_request, backend=backend, order=order, reference=str(reference)), 10)
    assert result['outcome'] == 'solved' and result['unresolved'] == 0
    assert result['independent_winner_check'] and result['native_verification']
    assert result['winner_sha256'] == truth['winner_sha256']
    if backend.startswith('PM'):
        assert sum(result['dispatch'].values()) > 0
    assert not list(tmp_path.glob('stage-*'))


def test_fixed_pm_policy_includes_all_shaving_rounds(case_request, tmp_path):
    reference = tmp_path / 'winners.gz'
    family.measure(dict(case_request, backend='reference', save_reference=str(reference)), 10)
    once = family.measure(dict(case_request, backend='PM', pair=[1, 0], reference=str(reference)), 10)
    repeated = family.measure(dict(case_request, backend='PM+iter', pair=[1, 0], reference=str(reference)), 10)
    assert once['outcome'] == 'unsolved' and once['unresolved'] > 0
    assert repeated['outcome'] == 'solved' and repeated['rounds'] == 2
    assert once['independent_winner_check'] and repeated['won'] == [5, 0]


def test_reference_identity_and_partition_are_checked(tmp_path):
    game = dict(vertices=9, sha256='a' * 64)
    won = [bytes([255, 0]), bytes([0, 1])]
    family.check_partition(won, bytes(2), 9)
    path = tmp_path / 'reference.gz'
    family.save_winners(path, won, game)
    assert family.load_winners(path, game) == tuple(won)
    with pytest.raises(ValueError, match='different fixture'):
        family.load_winners(path, dict(game, sha256='b' * 64))
    with pytest.raises(ValueError, match='partition'):
        family.check_partition([b'\x01', b'\x01'], b'\x00', 1)
    with pytest.raises(ValueError, match='disagrees'):
        family.check_partition([b'\x01', b'\x00'], b'\x00', 1, [b'\x00', b'\x01'])


def test_grid_does_not_infer_a_solve_from_a_timeout():
    rows = []
    def measured(k, t):
        row = dict(k=k, t=t, outcome='timeout' if (k, t) == (1, 0) else
                   'solved' if (k, t) == (2, 1) else 'unsolved')
        rows.append(row)
        return row
    family.measured_grid(measured, 2, 2)
    assert {(r['k'], r['t']) for r in rows} == {(1, 0), (1, 1), (1, 2), (2, 0), (2, 1)}
    assert family.pareto(rows)[:2] == ([(2, 1)], [])


def test_pinned_family_bounds_and_separate_relations():
    cabp = family.manifest('cabp')
    assert len(cabp['games']) == 4
    assert [g['id'] for g in cabp['games'] if g['in_v2026_selection']] == ['cabp-branching-bisim-data2']
    assert {g['parameter'] for g in cabp['games']} == {2, 4}
    assert {g['series'] for g in cabp['games']} == {'branching-bisim', 'strong-bisim'}
    assert all(g['AD_height'] == [2, 2] and g['PM_depth'] == [1, 2] for g in cabp['games'])
    anchor = cabp['games'][0]
    assert anchor['pg_sha256'] == family.sha256(ROOT / 'examples/cabp_swp.ehoa.pg')
    assert not next(g for g in cabp['games'] if g['id'] == 'cabp-branching-bisim-data4')['source_in_v2026_repository']
    ladders = family.manifest('ladder')['games']
    assert len(ladders) == 9 and ladders[0]['parameter'] == 100 and ladders[-1]['parameter'] == 50000
    assert all(g['vertices'] == 2 * g['parameter'] and g['edges'] == 4 * g['parameter'] for g in ladders)
    assert [g['parameter'] for g in ladders if g['fixed_map']] == [100, 50000]


def test_recursive_ladder_generator_matches_the_archived_games():
    import importlib.util
    sys.path.insert(0, str(ROOT / 'scripts'))
    spec = importlib.util.spec_from_file_location('gen_notebook_families', ROOT / 'scripts/gen_notebook_families.py')
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    info = family.manifest('recursiveladder')
    assert [g['parameter'] for g in info['games']] == [2, 4, 8, 16, 32, 64, 100, 200, 300]
    assert [g['parameter'] for g in info['games'] if g['fixed_map']] == [32, 100]
    assert info['reference_algorithm'] == 'zielonka-sym'

    def graph(text):
        return {int(fields[0]): (int(fields[1]), int(fields[2]),
                                 frozenset(map(int, fields[3].split(','))))
                for line in text.splitlines()
                if (fields := line.split()) and fields[0].isdigit()}

    for game in info['games']:
        n = game['parameter']
        expected = graph(bz2.decompress((family.HERE / 'recursiveladder' / game['file']).read_bytes()).decode())
        generated = graph(generator.recursive_ladder(n))
        assert generated == expected
        assert len(expected) == 5 * n
        assert sum(len(v[2]) for v in expected.values()) == 11 * n - 3
        assert game['generated'] == (n not in (100, 200))
    with pytest.raises(ValueError):
        generator.recursive_ladder(0)


def test_symmetric_reference_is_supported(case_request, tmp_path):
    reference = tmp_path / 'symmetric-winners.gz'
    result = family.measure(dict(case_request, backend='reference',
                                 reference_algorithm='zielonka-sym', save_reference=str(reference)), 10)
    assert result['outcome'] == 'solved' and result['won'] == [5, 0]
    assert reference.is_file()


@pytest.mark.parametrize('damage', ['pair', 'parameter', 'height', 'status', 'range'])
def test_fixed_recordings_reject_inconsistent_requested_hosts(damage):
    info = family.manifest('cabp')
    document = json.loads((family.HERE / 'cabp/results-60s.json').read_text())
    row = next(r for r in document['fixed_rows'] if r['backend'] == 'PM+iter' and r['outcome'] == 'solved')
    game = next(g for g in info['games'] if g['id'] == row['game'])
    family.validate_fixed_row(row, game)
    if damage == 'pair':
        row['final_pair'] = [1, 0]
    elif damage == 'parameter':
        row['parameters'][0]['k'] = 0
    elif damage == 'height':
        row['parameters'][0]['depth'] += 1
    elif damage == 'status':
        row['outcome'] = 'error'
    else:
        row['t'] = game['t_max'] + 1
    with pytest.raises(AssertionError):
        family.validate_fixed_row(row, game)


def test_published_family_evidence_includes_all_cases_and_fixed_followups():
    from fixed_pair_maps import embedding_source
    for name in ('cabp', 'ladder', 'recursiveladder'):
        info, document = family.recorded(name)
        if name == 'recursiveladder':
            assert [r['game'] for r in document['references'] if r['outcome'] != 'solved'] == [
                'recursiveladder-200', 'recursiveladder-300']
        else:
            assert all(r['outcome'] == 'solved' for r in document['references'])
        assert len(document['rows']) == len(info['games']) * len(family.BACKENDS) * len(family.ORDERS)
        for game in info['games']:
            for backend in family.FIXED_BACKENDS:
                if not backend.startswith('AD'):
                    continue
                cells = [r for r in document['fixed_rows']
                         if (r['game'], r['backend']) == (game['id'], backend)]
                solved = {(r['k'], r['t']) for r in cells if r['outcome'] == 'solved'}
                assert all(embedding_source(solved, r['k'], r['t']) is None
                           for r in cells if r['outcome'] == 'unsolved')
        if name == 'cabp':
            assert {(r['k'], r['t']) for r in document['followup_rows']} == {(3, 5), (3, 7)}
            assert all(r['outcome'] == 'solved' for r in document['followup_rows'])
        elif name == 'ladder':
            assert all(r['outcome'] == 'solved' for r in document['rows'])
            assert all(r['final_pair'] == [1, 0] for r in document['fixed_rows'])
        else:
            largest = [r for r in document['rows'] if r['game'] == 'recursiveladder-300']
            assert any(r['backend'] == 'PM' and r['order'] == 'table' and r['outcome'] == 'solved'
                       for r in largest)


def test_alternate_budget_cannot_delete_a_published_reference(case_request, tmp_path, monkeypatch):
    """A timed-out oracle in a new recording must leave old evidence intact."""
    directory = tmp_path / 'cabp'
    directory.mkdir()
    fixture = directory / 'game.pg.bz2'
    fixture.write_bytes(Path(case_request['path']).read_bytes())
    game = dict(case_request['game'], id='toy', file=fixture.name, series='toy', parameter=1,
                fixed_map=False)
    monkeypatch.chdir(tmp_path)  # Meson also runs tests outside the checkout.
    monkeypatch.setattr(family, 'HERE', tmp_path)
    monkeypatch.setattr(family, 'manifest', lambda _: dict(games=[game]))

    def measured(request, seconds):
        result = dict(outcome='timeout', seconds=seconds, timeout_seconds=seconds, memory_gib=2)
        if request['backend'] == 'reference' and seconds == 60:
            family.save_winners(request['save_reference'], [bytes([31]), bytes([0])], game)
            result['outcome'] = 'solved'
        return result

    monkeypatch.setattr(family, 'measure', measured)
    first = family.run('cabp', case_request['build_dir'], seconds=60)
    saved = directory / first['references'][0]['bitset_file']
    original = saved.read_bytes()
    second = family.run('cabp', case_request['build_dir'], seconds=1, output=directory / 'short.json')
    assert second['references'][0]['outcome'] == 'timeout'
    assert saved.read_bytes() == original
    assert not list(saved.parent.glob('.winners-*'))
    with pytest.raises(AssertionError, match='jobs'):
        family.run('cabp', case_request['build_dir'], seconds=60, jobs=1)
    import ijson
    with monkeypatch.context() as context:
        context.setattr(ijson, '__version__', 'different-parser')
        with pytest.raises(AssertionError, match='ijson_version'):
            family.run('cabp', case_request['build_dir'], seconds=60)
    saved.write_bytes(b'corrupt reference')
    with pytest.raises(ValueError, match='checksum'):
        family.run('cabp', case_request['build_dir'], seconds=60)
    saved.write_bytes(original)
    saved.unlink()
    with pytest.raises(FileNotFoundError):
        family.run('cabp', case_request['build_dir'], seconds=60)
