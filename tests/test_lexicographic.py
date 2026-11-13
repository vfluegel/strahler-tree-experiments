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
from json_check import document as check_document
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
@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
def test_ad_visits_a_lexicographic_prefix_with_fixed_initial_caps(iterate, algorithm):
    with strahler.Game.from_file(EXAMPLES.parent / 'tests/games/strahler_two.pg') as game:
        run = strahler.trace(game, algorithm, search='lexicographic', iter_shave=iterate)
        visited = []
        shaves = []
        k = None
        for event in run.events:
            if event.get('phase') == 'k_raised':
                k = event['value']
            elif event.get('phase') == 't_raised':
                visited.append((k, event['value']))
            elif event.get('phase') == 'shave_applied':
                shaves.append((len(visited), any(event['result']['regions'])))
        expected = list(pairs(len(game.vertices), max(p['H'] for p in run.result.data['parameters'])))
        distinct = [pair for i, pair in enumerate(visited) if i == 0 or pair != visited[i - 1]]
        assert distinct == expected[:len(distinct)]
        assert run.result.complete
        assert ad_wins(run.result) == ad_wins(strahler.decompose(game, 'zielonka'))
        if not iterate:
            assert (2, 0) in visited  # Reached only after exhausting every t for K=1.
        else:
            assert len(run.result.data['shaves']) > 0
            assert visited[:2] == [(1, 0), (1, 0)]
            for index, removed in shaves:
                if index < len(visited):
                    assert (visited[index] == visited[index - 1]) == removed


def test_pm_iter_repeats_productive_pairs_and_advances_only_when_stalled():
    info = manifest()[1]
    with strahler.Game.from_text(bz2.decompress((EXAMPLES / 'nester' / info['file']).read_bytes()).decode()) as game:
        data = strahler.progress_measures(game, search='lexicographic', iter_shave=True)
        expected = list(pairs(info['vertices'], max(info['PM_depth']) + 1))
        pair_index = 0
        before = info['vertices']
        repeated = False
        for index, round_ in enumerate(data['rounds']):
            assert round_['round'] == index + 1
            k, t = expected[pair_index]
            for player in round_['trees']:
                params = player['parameters']
                assert (params['k'], params['t']) == (min(k, params['depth'] + 1), t)
            after = len(round_['unresolved'])
            if not round_['complete']:
                if after < before:
                    repeated = True
                else:
                    pair_index += 1
            before = after
        assert repeated
        assert data['rounds'][-1]['complete']
        assert pm_wins(data) == ad_wins(strahler.decompose(game, 'zielonka'))


@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
def test_ad_repeats_a_productive_pair_at_the_end_of_a_capped_grid(algorithm):
    with strahler.Game.from_file(EXAMPLES.parent / 'tests/games/strahler_two.pg') as game:
        run = strahler.trace(game, algorithm, search='lexicographic', iter_shave=True,
                             tree_k=1, start_t=2)
        assert run.result.complete
        assert all(p['K'] == 1 and p['t'] == 2 for p in run.result.data['parameters'])
        attempts = [e['value'] for e in run.events if e.get('phase') == 't_raised']
        assert attempts == [2, 2]
        assert ad_wins(run.result) == ad_wins(strahler.decompose(game, 'zielonka'))


@pytest.mark.parametrize('iterate', [False, True])
@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
def test_theory_caps_suffice_for_both_algorithms_and_players(iterate, algorithm):
    rng = random.Random(314159)
    for count in [1, 2, 3, 4, 8, 12]:
        for _ in range(3):
            text = '\n'.join(f'{v} {rng.randrange(7)} {rng.randrange(2)} '
                             f'{rng.randrange(count)},{rng.randrange(count)};'
                             for v in range(count))
            with strahler.Game.from_text(text) as game:
                expected = ad_wins(strahler.decompose(game, 'zielonka'))
                ad = strahler.decompose(game, algorithm, search='lexicographic', iter_shave=iterate)
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
    ('AD', dict(search='ratio', adaptive_t=True)),
    ('AD', dict(search='ratio', algorithm='zielonka')),
    ('AD', dict(search='ratio', algorithm='lifting-sym', adaptive_t=True)),
    ('AD', dict(search='lexicographic', adaptive_t=True)),
    ('AD', dict(search='lexicographic', kary=2)),
    ('AD', dict(algorithm='lifting-sym', search='lexicographic', adaptive_t=True)),
    ('AD', dict(algorithm='lifting-sym', search='lexicographic', kary=2)),
    ('PM', dict(search='other')),
    ('PM', dict(search='ratio', k=2, t=1)),
    ('PM', dict(search='ratio', algorithm='succinct')),
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
    for name in ('RESULTS', 'TIMEOUT_RESULTS', 'FIVE_MINUTE_RESULTS', 'FINAL_RESULTS',
                 'SYMMETRIC_RESULTS', 'FRONTIER_RESULTS', 'LATEST_RESULTS',
                 'REPEAT_SHAVING_RESULTS', 'REPEAT_SHAVING_FINAL_RESULTS'):
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


def test_symmetric_recording_adds_cases_without_repeating_the_baseline(recordings, monkeypatch):
    for path in (experiment.TIMEOUT_RESULTS, experiment.FIVE_MINUTE_RESULTS):
        path.write_bytes((EXAMPLES / 'nester' / path.name).read_bytes())
    baseline = experiment.collect_results()
    before = {path: path.read_bytes() for path in
              (experiment.RESULTS, experiment.TIMEOUT_RESULTS, experiment.FIVE_MINUTE_RESULTS)}
    calls = []
    example = next(r for r in recordings['rows'] if r['n'] == 1 and r['backend'] == 'AD')

    def measure(game, backend, seconds, memory_gib):
        calls.append((game['n'], backend))
        assert backend in experiment.SYMMETRIC_BACKENDS
        if game['n'] == 1:
            return dict(example, backend=backend, timeout_seconds=seconds)
        return dict(n=game['n'], backend=backend, outcome='timeout', seconds=seconds,
                    timeout_seconds=seconds, memory_gib=memory_gib)

    monkeypatch.setattr(experiment, 'measure_case', measure)
    options = dict(seconds=300, output=experiment.SYMMETRIC_RESULTS,
                   backends=experiment.SYMMETRIC_BACKENDS, jobs=2, stop_after_n=5)
    result = experiment.run_all(**options)
    assert set(calls) == {(n, b) for n in range(1, 6) for b in experiment.SYMMETRIC_BACKENDS}
    assert len(calls) == 10
    assert experiment.recorded_symmetric_results() == result
    assert len(result['rows']) == 14
    calls.clear()
    assert experiment.run_all(**options) == result
    assert not calls

    final = experiment.collect_results()
    assert experiment.recorded_final_results() == final
    assert len(final['rows']) == 42
    assert [r for r in final['rows'] if r['backend'] in experiment.BASE_BACKENDS] == baseline['rows']
    assert final['recordings'][experiment.SYMMETRIC_RESULTS.name]['sha256'] == hashlib.sha256(
        experiment.SYMMETRIC_RESULTS.read_bytes()).hexdigest()
    assert all(path.read_bytes() == content for path, content in before.items())

    # An interrupted recording resumes only its missing case.
    missing = result['rows'].pop(0)
    experiment.SYMMETRIC_RESULTS.write_text(json.dumps(result))
    experiment.run_all(**options)
    assert calls == [(missing['n'], missing['backend'])]
    with pytest.raises(AssertionError):
        experiment.run_all(**dict(options, seconds=301))


@pytest.mark.parametrize('iterate', [False, True])
def test_symmetric_worker_uses_the_same_search_and_winner_certificates(iterate):
    backend = 'AD-sym+iter' if iterate else 'AD-sym'
    row = experiment.measure_case(manifest()[1], backend, seconds=10)
    original = next(r for r in experiment.recorded_results()['rows']
                    if r['n'] == 2 and r['backend'] == ('AD+iter' if iterate else 'AD'))
    assert row['outcome'] == 'complete'
    for key in ('won', 'unresolved', 'winner_sha256'):
        assert row[key] == original[key]
    if iterate:
        assert (row['k'], row['t']) == (1, 0)
        assert (row['k'], row['t']) < (original['k'], original['t'])
    else:
        assert row['parameters'] == original['parameters']


@pytest.mark.parametrize('options', [dict(tree_k=1), dict(max_host_leaves=1)])
def test_symmetric_capped_search_keeps_sound_partial_certificates(options):
    text = '0 2 0 3; 1 2 1 2; 2 0 1 1,4; 3 0 0 3; 4 3 1 3;'
    with strahler.Game.from_text(text) as game:
        plain = strahler.decompose(game, 'lifting', search='lexicographic', **options)
        symmetric = strahler.decompose(game, 'lifting-sym', search='lexicographic', **options)
        assert not symmetric.complete
        assert ad_wins(symmetric) == ad_wins(plain)
        assert symmetric.data['budget_exhausted'] == ('max_host_leaves' in options)
        for player, tree in enumerate(symmetric.trees):
            assert strahler.check(game, tree, player, symmetric.regions[player]).ok


@pytest.mark.parametrize('jobs', [1, 2])
def test_longer_reruns_extend_each_frontier_sequentially_and_resume(recordings, monkeypatch, jobs):
    for path in (experiment.TIMEOUT_RESULTS, experiment.FIVE_MINUTE_RESULTS,
                 experiment.SYMMETRIC_RESULTS):
        path.write_bytes((EXAMPLES / 'nester' / path.name).read_bytes())
    baseline = experiment.collect_results()
    before = experiment.FINAL_RESULTS.read_bytes()
    calls = []
    stopping = {'AD': 6, 'AD+iter': None, 'AD-sym': 4, 'AD-sym+iter': 5,
                'PM': 4, 'PM+iter': 5}

    def measure(game, backend, seconds, memory_gib):
        n = game['n']
        calls.append((n, backend))
        assert seconds == 600 and memory_gib == 2
        if n == stopping[backend]:
            return dict(n=n, backend=backend, outcome='timeout', seconds=seconds,
                        timeout_seconds=seconds, memory_gib=memory_gib)
        donor = next((r for r in baseline['rows'] if r['n'] == n and r['outcome'] == 'complete'),
                     dict(outcome='complete', winner_sha256=[f'even-{n}', f'odd-{n}']))
        return dict(donor, n=n, backend=backend, seconds=1,
                    timeout_seconds=seconds, memory_gib=memory_gib)

    monkeypatch.setattr(experiment, 'measure_case', measure)
    result = experiment.run_frontiers(jobs=jobs)
    assert result['complete'] and result['cases'] == 11 and result['planned_cases'] == 22
    assert experiment.recorded_frontier_results() == result
    assert (6, 'AD') in calls and (7, 'AD') not in calls
    assert (7, 'AD+iter') in calls
    assert (4, 'AD-sym') in calls and (5, 'AD-sym') not in calls
    assert len(calls) == len(set(calls)) == 11
    for backend in experiment.BACKENDS:
        ns = [n for n, b in calls if b == backend]
        assert ns == sorted(ns)
        frontier = result['frontiers'][backend]
        assert frontier['stopped_at_n'] == stopping[backend]
        assert frontier['last_solved_n'] == (stopping[backend] - 1 if stopping[backend] else 7)

    final = experiment.collect_results()
    assert experiment.recorded_final_results() == final
    assert final['timeout_seconds'] == 600 and len(final['rows']) == 42
    assert final['frontiers'] == result['frontiers']
    completed = [r for r in baseline['rows'] if r['outcome'] == 'complete']
    assert all(r in final['rows'] for r in completed)
    assert experiment.FINAL_RESULTS.read_bytes() == before
    for row in final['rows']:
        if row['outcome'] == 'timeout':
            assert row['timeout_seconds'] == 600
        if row['outcome'] == 'not measured':
            assert 'seconds' not in row
    saved = experiment.FRONTIER_RESULTS.read_bytes()
    calls.clear()
    assert experiment.run_frontiers(jobs=jobs) == result
    assert not calls and experiment.FRONTIER_RESULTS.read_bytes() == saved
    assert experiment.recorded_final_results() == final

    # Simulate interruption while AD's n=6 case was running; keep its earlier successes.
    result['rows'] = [r for r in result['rows'] if not (r['backend'] == 'AD' and r['n'] >= 6)]
    result['complete'] = False
    result['frontiers'].pop('AD')
    experiment.FRONTIER_RESULTS.write_text(json.dumps(result))
    resumed = experiment.run_frontiers(jobs=jobs)
    assert calls == [(6, 'AD')]
    assert experiment.recorded_frontier_results() == resumed
    calls.clear()
    experiment.FINAL_RESULTS.write_bytes(before + b'\n')
    with pytest.raises(AssertionError):
        experiment.run_frontiers(jobs=jobs)
    assert not calls


@pytest.mark.parametrize('options', [dict(seconds=300), dict(jobs=0), dict(memory_gib=0)])
def test_longer_frontier_rerun_rejects_invalid_limits_before_running(monkeypatch, options):
    monkeypatch.setattr(experiment, 'measure_case', lambda *a, **kw: pytest.fail('Must not run'))
    with pytest.raises(ValueError):
        experiment.run_frontiers(**options)


@pytest.mark.parametrize('jobs', [1, 2])
def test_fresh_frontiers_remeasure_successes_at_the_same_deadline(recordings, monkeypatch, jobs):
    reference = json.loads((EXAMPLES / 'nester' / experiment.LATEST_RESULTS.name).read_text())
    experiment.LATEST_RESULTS.write_text(json.dumps(reference))
    original = experiment.LATEST_RESULTS.read_bytes()
    calls = []

    def measure(game, backend, seconds, memory_gib):
        calls.append((game['n'], backend))
        if game['n'] == 2:
            return dict(n=2, backend=backend, outcome='timeout', seconds=seconds,
                        timeout_seconds=seconds, memory_gib=memory_gib)
        donor = next(r for r in reference['rows'] if r['n'] == 1 and r['backend'] == backend)
        return dict(donor, seconds=7, timeout_seconds=seconds)

    monkeypatch.setattr(experiment, 'measure_case', measure)
    options = dict(jobs=jobs, rerun_all=True)
    result = experiment.run_frontiers(**options)
    assert set(calls) == {(n, b) for n in (1, 2) for b in experiment.BACKENDS}
    assert len(calls) == result['cases'] == 12
    assert result['planned_cases'] == len(result['rows']) == 42
    assert experiment.recorded_frontier_results(
        experiment.REPEAT_SHAVING_RESULTS, experiment.LATEST_RESULTS) == result
    assert all(f['last_solved_n'] == 1 and f['stopped_at_n'] == 2
               for f in result['frontiers'].values())
    final = experiment.collect_results()
    assert experiment.recorded_final_results() == final
    assert final['rerun_all'] and final['iter_shave_policy'] == 'repeat-until-stalled'
    assert all(r['timeout_seconds'] == 600 for r in final['rows'])
    assert all(r['seconds'] == 7 for r in final['rows'] if r['outcome'] == 'complete')
    assert experiment.LATEST_RESULTS.read_bytes() == original
    calls.clear()
    before = experiment.REPEAT_SHAVING_RESULTS.read_bytes()
    assert experiment.run_frontiers(**options) == result
    assert not calls and experiment.REPEAT_SHAVING_RESULTS.read_bytes() == before


def visited_pairs(run):
    visited, k = [], None
    for event in run.events:
        if event.get('phase') == 'k_raised':
            k = event['value']
        elif event.get('phase') == 't_raised':
            visited.append((k, event['value']))
    return visited


@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
@pytest.mark.parametrize('iterate', [False, True])
def test_ratio_order_and_repeat_on_progress(algorithm, iterate):
    with strahler.Game.from_file(EXAMPLES.parent / 'tests/games/strahler_two.pg') as game:
        run = strahler.trace(game, algorithm, search='ratio', iter_shave=iterate)
        assert visited_pairs(run) == ([(1, 1), (1, 1)] if iterate else [(1, 1), (2, 1)])
        check_document(run.result.data)
        assert run.result.data['round_pairs'] == ([[1, 1], [1, 1]] if iterate else [[2, 1]])
        assert run.result.complete
        assert ad_wins(run.result) == ad_wins(strahler.decompose(game, 'zielonka'))
        pm = strahler.progress_measures(game, search='ratio', iter_shave=iterate)
        assert pm_wins(pm) == ad_wins(run.result)
        assert [(max(p['parameters']['k'] for p in r['trees']), r['trees'][0]['parameters']['t'])
                for r in pm['rounds']] == ([(1, 1), (1, 1)] if iterate else [(2, 1)])


@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
@pytest.mark.parametrize('iterate', [False, True])
def test_ratio_completes_when_preprocessing_removes_every_vertex(algorithm, iterate):
    with strahler.Game.from_text('0 0 0 0;1 1 1 1;') as game:
        run = strahler.trace(game, algorithm, search='ratio', iter_shave=iterate,
                             initial_shave='all')
        assert run.result.complete and not run.result.unresolved
        assert visited_pairs(run) == []
        assert run.result.data['round_pairs'] == []
        assert ad_wins(run.result) == [{0}, {1}]
        check_document(run.result.data)
        pm = strahler.progress_measures(game, search='ratio', iter_shave=iterate,
                                        initial_shave='all')
        assert len(pm['rounds']) == 1
        assert pm['rounds'][0]['complete'] and not pm['rounds'][0]['unresolved']
        assert pm_wins(pm) == [{0}, {1}]
        check_document(pm)


@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
@pytest.mark.parametrize('iterate', [False, True])
def test_ratio_exhaustion_with_a_cap_and_without_admissible_pairs(algorithm, iterate):
    with strahler.Game.from_file(EXAMPLES.parent / 'tests/games/strahler_two.pg') as game:
        run = strahler.trace(game, algorithm, search='ratio', tree_k=1, iter_shave=iterate)
        assert visited_pairs(run) == ([(1, 1), (1, 1)] if iterate else [(1, 1), (1, 2)])
        assert run.result.complete == iterate
    with strahler.Game.from_text('0 0 0 0;') as game:
        run = strahler.trace(game, algorithm, search='ratio', iter_shave=iterate)
        assert not run.result.complete and visited_pairs(run) == []
        assert run.result.data['round_pairs'] == []
        assert run.result.unresolved == {0}
        with pytest.raises(strahler.StrahlerError, match='search exhausted'):
            strahler.progress_measures(game, search='ratio', iter_shave=iterate)


@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
@pytest.mark.parametrize('iterate', [False, True])
@pytest.mark.parametrize('start', [(1, 1), (2, 0), (2, 99)])
def test_warm_start_skips_the_prefix_and_clamps_t(algorithm, iterate, start):
    with strahler.Game.from_file(EXAMPLES.parent / 'tests/games/strahler_two.pg') as game:
        k, t = start
        run = strahler.trace(game, algorithm, search='lexicographic', start_k=k,
                             start_t=t, iter_shave=iterate)
        check_document(run.result.data)
        visits = visited_pairs(run)
        assert visits[0] == (k, min(t, 2))
        expected = list(pairs(5, 3))
        offset = expected.index(visits[0])
        distinct = [p for i, p in enumerate(visits) if i == 0 or p != visits[i - 1]]
        assert distinct == expected[offset:offset + len(distinct)]
        pm = strahler.progress_measures(game, search='lexicographic', start_k=k,
                                        start_t=t, iter_shave=iterate)
        first = pm['rounds'][0]['trees'][0]['parameters']
        if iterate:
            assert (first['k'], first['t']) == visits[0]
        assert pm['rounds'][-1]['complete'] and run.result.complete
        assert ad_wins(run.result) == pm_wins(pm) == ad_wins(strahler.decompose(game, 'zielonka'))


@pytest.mark.parametrize('solver', [strahler.decompose, strahler.progress_measures])
@pytest.mark.parametrize('options', [dict(start_k=1), dict(start_t=0),
    dict(search='ratio', start_k=1), dict(search='ratio', start_t=0),
    dict(search='lexicographic', start_k=0), dict(search='lexicographic', start_k=-1),
    dict(search='lexicographic', start_t=-1), dict(search='ratio', kary=2)])
def test_new_search_option_validation(solver, options):
    with strahler.Game.from_text('0 0 0 0;') as game:
        with pytest.raises(strahler.InputError):
            solver(game, **options)


@pytest.mark.parametrize('mode', ['ratio', 'boundary'])
@pytest.mark.parametrize('jobs', [1, 2])
def test_new_frontiers_resume_and_propagate_same_method_boundary(tmp_path, monkeypatch, mode, jobs):
    calls = []
    reference = json.loads(experiment.LATEST_RESULTS.read_text())
    output = tmp_path / f'{mode}.json'

    def measure(game, backend, seconds, memory_gib, *, search, start_pair=None):
        n = game['n']
        calls.append((n, backend))
        assert search == ('ratio' if mode == 'ratio' else 'lexicographic')
        if mode == 'boundary':
            assert start_pair == ([1, 0] if n == 1 else [2, experiment.BACKENDS.index(backend) + 1])
        row = dict(n=n, backend=backend, outcome='timeout', seconds=seconds,
                   timeout_seconds=seconds, memory_gib=memory_gib)
        if start_pair is not None:
            row['start_pair'] = list(start_pair)
        if n == 1:
            donor = next(r for r in reference['rows'] if r['n'] == n and r['backend'] == backend)
            final = [2, experiment.BACKENDS.index(backend) + 1]
            row.update(donor, seconds=1, timeout_seconds=seconds, k=final[0], t=final[1],
                       final_pair=final, rounds=2,
                       round_pairs=[start_pair or [1, 1], final])
        return row

    monkeypatch.setattr(experiment, 'measure_case', measure)
    run = experiment.run_ratio if mode == 'ratio' else experiment.run_boundary
    load = experiment.recorded_ratio_results if mode == 'ratio' else experiment.recorded_boundary_results
    result = run(seconds=60, output=output, jobs=jobs, max_n=3)
    assert set(calls) == {(n, b) for n in (1, 2) for b in experiment.BACKENDS}
    assert result['cases'] == 12 and len(result['rows']) == 18
    assert load(output) == result
    assert all(f['last_solved_n'] == 1 and f['stopped_at_n'] == 2 for f in result['frontiers'].values())
    calls.clear()
    before = output.read_bytes()
    assert run(seconds=60, output=output, jobs=jobs, max_n=3) == result
    assert output.read_bytes() == before and not calls
    document = json.loads(before)
    document['library_sha256'] = '0' * 64
    output.write_text(json.dumps(document))
    with pytest.raises(AssertionError):
        run(seconds=60, output=output, jobs=jobs, max_n=3)
    assert not calls


@pytest.mark.parametrize('run', [experiment.run_ratio, experiment.run_boundary])
@pytest.mark.parametrize('options', [dict(seconds=0), dict(memory_gib=0), dict(jobs=0),
                                     dict(max_n=0), dict(max_n=8)])
def test_new_frontiers_validate_limits_before_running(monkeypatch, run, options):
    monkeypatch.setattr(experiment, 'measure_case', lambda *a, **kw: pytest.fail('Must not run'))
    with pytest.raises(ValueError):
        run(**options)


def test_pm_ratio_retries_productive_round_then_advances_after_a_stall():
    info = manifest()[0]
    with strahler.Game.from_text(bz2.decompress((EXAMPLES / 'nester' / info['file']).read_bytes()).decode()) as game:
        document = strahler.progress_measures(game, search='ratio', iter_shave=True)
        sequence = [(max(p['parameters']['k'] for p in r['trees']),
                     r['trees'][0]['parameters']['t']) for r in document['rounds']]
        assert sequence == [(1, 1), (1, 1), (2, 1), (2, 2)]
        before = info['vertices']
        for index, round_ in enumerate(document['rounds'][:-1]):
            after = len(round_['unresolved'])
            assert (sequence[index + 1] == sequence[index]) == (after < before)
            before = after
        assert pm_wins(document) == ad_wins(strahler.decompose(game, 'zielonka'))
