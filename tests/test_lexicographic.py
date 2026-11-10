"""Check the parameter order, theory caps, and winning-region certificates."""

import bz2
import hashlib
from pathlib import Path
import random
import sys

import pytest
import strahler

EXAMPLES = Path(__file__).resolve().parents[1] / 'jupyter-examples'
sys.path.insert(0, str(EXAMPLES))
from lexicographic_nester import bounds, manifest, pairs, summarize, tree_vertices


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
