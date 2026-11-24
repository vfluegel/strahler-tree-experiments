"""Check bounded family reporting, fixture identity, and honest map coverage."""

import bz2
import os
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'jupyter-examples'))
import family_benchmarks as family


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
    assert {g['parameter'] for g in cabp['games']} == {2, 4}
    assert {g['series'] for g in cabp['games']} == {'branching-bisim', 'strong-bisim'}
    assert all(g['AD_height'] == [2, 2] and g['PM_depth'] == [1, 2] for g in cabp['games'])
    anchor = cabp['games'][0]
    assert anchor['pg_sha256'] == family.sha256(ROOT / 'examples/cabp_swp.ehoa.pg')
    assert not next(g for g in cabp['games'] if g['id'] == 'cabp-branching-bisim-data4')['official_syntcomp_2026']
    ladders = family.manifest('ladder')['games']
    assert len(ladders) == 9 and ladders[0]['parameter'] == 100 and ladders[-1]['parameter'] == 50000
    assert all(g['vertices'] == 2 * g['parameter'] and g['edges'] == 4 * g['parameter'] for g in ladders)
    assert [g['parameter'] for g in ladders if g['fixed_map']] == [100, 50000]
