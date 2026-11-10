"""Check the parameter order, theory caps, and winning-region certificates."""

import bz2
import hashlib
import json
from pathlib import Path
import random
import sys

import pytest
import strahler

EXAMPLES = Path(__file__).resolve().parents[1] / 'jupyter-examples'
sys.path.insert(0, str(EXAMPLES))
from lexicographic_nester import bounds, manifest, pairs, summarize, tree_vertices
import lexicographic_nester as experiment


def ad_wins(result):
    won = [set(r) for r in result.regions]
    for player in range(2):
        won[player].update(result.data['initial_won'][player])
        for shave in result.data['shaves']:
            won[player].update(shave['attracted'][player])
    return won


def pm_wins(document):
    won = [set(), set()]
    for round_ in document['rounds']:
        for p, player in enumerate(round_['trees']):
            won[p].update(tree_vertices(player['tree']))
            won[p].update(player['attracted'])
            won[p].update(player['shaved'])
    return won


@pytest.mark.parametrize('vertices', [1, 2, 3, 4, 7, 8, 16, 31, 32])
def test_caps_include_the_root_and_powers_of_two(vertices):
    result = bounds(vertices, 8)
    t_max = vertices.bit_length() - 1
    assert result['t_max'] == t_max
    assert result['AD_height'] == [5, 5]
    assert result['PM_depth'] == [4, 5]
    assert result['AD_k_max'] == [min(5, t_max + 1)] * 2
    assert result['PM_k_max'] == [min(5, t_max + 1), min(6, t_max + 1)]
    sequence = list(pairs(vertices, 3))
    assert sequence == [(k, t) for k in range(1, min(3, t_max + 1) + 1)
                        for t in range(t_max + 1)]


@pytest.mark.parametrize('iterate', [False, True])
def test_ad_visits_a_lexicographic_prefix_with_fixed_initial_caps(iterate):
    with strahler.Game.from_file(EXAMPLES.parent / 'tests/games/strahler_two.pg') as game:
        run = strahler.trace(game, 'lifting', search='lexicographic', iter_shave=iterate)
        visited = []
        k = None
        for event in run.events:
            if event.get('phase') == 'k_raised':
                k = event['value']
            elif event.get('phase') == 't_raised':
                visited.append((k, event['value']))
        expected = list(pairs(len(game.vertices), max(p['H'] for p in run.result.data['parameters'])))
        assert visited == expected[:len(visited)]
        assert run.result.complete
        assert ad_wins(run.result) == ad_wins(strahler.decompose(game, 'zielonka'))
        if not iterate:
            assert (2, 0) in visited  # Reached only after exhausting every t for K=1.
        else:
            assert len(run.result.data['shaves']) > 0
            assert visited[:2] == [(1, 0), (1, 1)]


def test_pm_iter_visits_the_same_grid_before_increasing_k():
    info = manifest()[0]
    with strahler.Game.from_text(bz2.decompress((EXAMPLES / 'nester' / info['file']).read_bytes()).decode()) as game:
        data = strahler.progress_measures(game, search='lexicographic', iter_shave=True)
        expected = list(pairs(info['vertices'], max(info['PM_depth']) + 1))
        for index, round_ in enumerate(data['rounds']):
            assert round_['round'] == index + 1
            k, t = expected[index]
            for player in round_['trees']:
                params = player['parameters']
                assert (params['k'], params['t']) == (min(k, params['depth'] + 1), t)
        assert data['rounds'][-1]['complete']
        assert pm_wins(data) == ad_wins(strahler.decompose(game, 'zielonka'))


@pytest.mark.parametrize('iterate', [False, True])
def test_theory_caps_suffice_for_both_algorithms_and_players(iterate):
    rng = random.Random(314159)
    for count in [1, 2, 3, 4, 8, 12]:
        for _ in range(3):
            text = '\n'.join(f'{v} {rng.randrange(7)} {rng.randrange(2)} '
                             f'{rng.randrange(count)},{rng.randrange(count)};'
                             for v in range(count))
            with strahler.Game.from_text(text) as game:
                expected = ad_wins(strahler.decompose(game, 'zielonka'))
                ad = strahler.decompose(game, 'lifting', search='lexicographic', iter_shave=iterate)
                pm = strahler.progress_measures(game, search='lexicographic', iter_shave=iterate)
                assert ad.complete and pm['rounds'][-1]['complete']
                assert ad_wins(ad) == pm_wins(pm) == expected
                t_max = count.bit_length() - 1
                for params in ad.data['parameters']:
                    assert params['t'] <= t_max
                    assert params['K'] <= min(params['H'], t_max + 1)
                for round_ in pm['rounds']:
                    for player in round_['trees']:
                        params = player['parameters']
                        assert params['t'] <= t_max
                        assert params['k'] <= min(params['depth'] + 1, t_max + 1)


@pytest.mark.parametrize('algorithm,options', [
    ('AD', dict(search='other')),
    ('AD', dict(search='lexicographic', adaptive_t=True)),
    ('AD', dict(search='lexicographic', kary=2)),
    ('PM', dict(search='other')),
    ('PM', dict(search='lexicographic', k=2, t=1)),
    ('PM', dict(search='lexicographic', algorithm='succinct')),
])
def test_rejects_ambiguous_search_requests(algorithm, options):
    with strahler.Game.from_text('0 0 0 0;') as game:
        with pytest.raises(strahler.InputError):
            (strahler.decompose if algorithm == 'AD' else strahler.progress_measures)(game, **options)


def test_every_plain_nester_fixture_matches_its_manifest():
    games = manifest()
    assert [g['n'] for g in games] == list(range(1, 8))
    for game in games:
        assert '_compact' not in game['file']
        assert hashlib.sha256((EXAMPLES / 'nester' / game['file']).read_bytes()).hexdigest() == game['sha256']
        assert game['t_max'] == game['vertices'].bit_length() - 1


def test_pm_summary_includes_wins_removed_in_earlier_rounds():
    info = manifest()[1]
    text = bz2.decompress((EXAMPLES / 'nester' / info['file']).read_bytes()).decode()
    with strahler.Game.from_text(text) as game:
        document = strahler.progress_measures(game, search='lexicographic', iter_shave=True)
        summary = summarize(document, 'PM+iter', info)
        assert summary['outcome'] == 'complete'
        assert sum(summary['won']) == info['vertices']
        assert summary['unresolved'] == 0


@pytest.fixture
def recordings(tmp_path, monkeypatch):
    original = experiment.recorded_results()
    for name in ('RESULTS', 'TIMEOUT_RESULTS', 'FIVE_MINUTE_RESULTS', 'FINAL_RESULTS'):
        monkeypatch.setattr(experiment, name, tmp_path / getattr(experiment, name).name)
    experiment.RESULTS.write_text(json.dumps(original, indent=2) + '\n')
    metadata = {key: value for key, value in original.items() if key != 'rows'}
    monkeypatch.setattr(experiment, '_run_metadata', lambda games: dict(metadata, rows=[]))
    return original


@pytest.mark.parametrize('jobs', [1, 2])
def test_timeout_reruns_preserve_finished_cases_and_resume(recordings, monkeypatch, jobs):
    before = experiment.RESULTS.read_bytes()
    selected = {(r['n'], r['backend']): r for r in recordings['rows'] if r['outcome'] == 'timeout'}
    calls = []

    def measure(game, backend, seconds, memory_gib):
        key = (game['n'], backend)
        calls.append(key)
        return dict(selected[key], timeout_seconds=seconds, memory_gib=memory_gib)

    monkeypatch.setattr(experiment, 'measure_case', measure)
    result = experiment.run_timeouts(jobs=jobs)
    assert set(calls) == selected.keys() and len(calls) == len(selected)
    assert result['cases'] == len(selected)
    assert experiment.recorded_timeout_results() == result
    assert experiment.RESULTS.read_bytes() == before
    assert not experiment.TIMEOUT_RESULTS.with_suffix('.json.tmp').exists()
    calls.clear()
    assert experiment.run_timeouts(jobs=jobs) == result
    assert not calls

    experiment.RESULTS.write_bytes(before + b'\n')
    with pytest.raises(AssertionError):
        experiment.run_timeouts(jobs=jobs)
    assert not calls


def test_final_results_combine_cases_at_one_cap_with_provenance(recordings, monkeypatch):
    solved_n3 = next(r for r in recordings['rows']
                     if r['n'] == 3 and r['backend'] == 'AD')
    calls = []

    def measure(game, backend, seconds, memory_gib):
        calls.append((game['n'], backend, seconds))
        if game['n'] == 3 and (backend == 'PM' or seconds == 300):
            return dict(solved_n3, backend=backend, timeout_seconds=seconds, seconds=seconds - 1)
        return dict(n=game['n'], backend=backend, outcome='timeout', seconds=seconds,
                    timeout_seconds=seconds, memory_gib=memory_gib)

    monkeypatch.setattr(experiment, 'measure_case', measure)
    experiment.run_timeouts()
    calls.clear()
    experiment.run_timeouts(seconds=300, output=experiment.FIVE_MINUTE_RESULTS,
                            baseline_path=experiment.TIMEOUT_RESULTS, jobs=2)
    assert calls and all((n, backend) != (3, 'PM') and seconds == 300
                         for n, backend, seconds in calls)
    final = experiment.collect_results()
    assert experiment.recorded_final_results() == final
    assert len(final['rows']) == 28
    assert final['timeout_seconds'] == 300 and final['memory_gib'] == 2
    by_case = {(r['n'], r['backend']): r for r in final['rows']}
    assert by_case[1, 'AD']['recording'] == experiment.RESULTS.name
    assert by_case[3, 'PM']['recording'] == experiment.TIMEOUT_RESULTS.name
    assert by_case[3, 'PM+iter']['recording'] == experiment.FIVE_MINUTE_RESULTS.name
    assert by_case[3, 'PM+iter']['outcome'] == 'complete'
    assert by_case[4, 'AD']['recording'] == experiment.FIVE_MINUTE_RESULTS.name
    assert all(r['timeout_seconds'] == 300 for r in final['rows'] if r['outcome'] == 'timeout')
    for path in (experiment.RESULTS, experiment.TIMEOUT_RESULTS, experiment.FIVE_MINUTE_RESULTS):
        assert final['recordings'][path.name]['sha256'] == hashlib.sha256(path.read_bytes()).hexdigest()

    final['rows'][0]['outcome'] = 'timeout'
    experiment.FINAL_RESULTS.write_text(json.dumps(final))
    with pytest.raises(AssertionError):
        experiment.recorded_final_results()


@pytest.mark.parametrize('options', [dict(seconds=30), dict(jobs=0)])
def test_rerun_rejects_an_unchanged_deadline_or_no_workers(recordings, monkeypatch, options):
    def fail(*args, **kwargs):
        pytest.fail('Invalid run settings must fail before starting a solver')

    monkeypatch.setattr(experiment, 'measure_case', fail)
    with pytest.raises(ValueError):
        experiment.run_timeouts(**options)


def test_no_timeouts_still_writes_a_valid_empty_recording(recordings, monkeypatch, tmp_path):
    baseline = tmp_path / 'completed.json'
    baseline.write_text(json.dumps(dict(recordings, rows=[r for r in recordings['rows']
                                                         if r['outcome'] == 'complete'])))

    def fail(*args, **kwargs):
        pytest.fail('Completed cases must not run again')

    monkeypatch.setattr(experiment, 'measure_case', fail)
    result = experiment.run_timeouts(baseline_path=baseline)
    assert result['rows'] == [] and result['cases'] == 0
    assert experiment.recorded_timeout_results(baseline_path=baseline) == result


def test_frontier_is_preserved_on_resume_and_larger_games_are_not_timeouts(recordings, monkeypatch):
    calls = []

    def measure(game, backend, seconds, memory_gib):
        calls.append(game['n'])
        return dict(n=game['n'], backend=backend, outcome='timeout', seconds=seconds,
                    timeout_seconds=seconds, memory_gib=memory_gib)

    monkeypatch.setattr(experiment, 'measure_case', measure)
    experiment.run_timeouts()
    calls.clear()
    result = experiment.run_timeouts(seconds=300, output=experiment.FIVE_MINUTE_RESULTS,
                                    baseline_path=experiment.TIMEOUT_RESULTS, jobs=2, stop_after_n=5)
    assert calls and max(calls) == 5
    assert result['cases'] < result['planned_cases'] and result['stop_after_n'] == 5
    assert experiment.recorded_timeout_results(experiment.FIVE_MINUTE_RESULTS,
                                              experiment.TIMEOUT_RESULTS) == result
    calls.clear()
    assert experiment.run_timeouts(seconds=300, output=experiment.FIVE_MINUTE_RESULTS,
                                   baseline_path=experiment.TIMEOUT_RESULTS, jobs=2) == result
    assert not calls
    final = experiment.collect_results()
    larger = [r for r in final['rows'] if r['n'] > 5]
    assert len(larger) == 8
    assert all(r['outcome'] == 'not measured' and 'seconds' not in r for r in larger)
    assert final['stop_after_n'] == 5
