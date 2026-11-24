"""Native host ordinals and independent pre-round lift explanations."""

from copy import deepcopy
from pathlib import Path

import pytest
import strahler
from strahler import HostSnapshot
from strahler._widget_state import TraceCursor

CHAIN = 'parity 3;0 2 0 0 "h";1 1 1 0 "t";2 0 0 2 "a";3 1 0 2 "x";'
BRANCH = CHAIN.replace('parity 3', 'parity 4') + '4 0 1 4,3 "b";'
ROOT = Path(__file__).resolve().parents[1]


def shape(children=()):
    return dict(children=list(children))


def test_underlying_metrics_and_intervals():
    chain = shape([shape([shape([shape()])])])
    host = HostSnapshot.from_tree(chain)
    assert host.metrics == dict(nodes=4, leaves=1, height=4, strahler=1, positions=12)
    assert deepcopy(host) is host
    assert host.positions[-1].name == 'TOP'
    assert host.nodes[0].core == (0, 11)
    assert host.nodes[1].core[0] <= host.nodes[2].positions[2] < host.nodes[1].core[1]
    fork = shape([shape(), shape()])
    uneven = HostSnapshot.from_tree(shape([fork, shape([fork, fork])]))
    assert uneven.metrics['strahler'] == 3
    assert uneven.by_path[(0,)].strahler == 2
    assert uneven.by_path[(1,)].strahler == 3
    assert uneven.by_path[(1, 0)].strahler == 2
    assert uneven.by_path[(0, 0)].strahler == 1


def test_native_fixture_indices_compatibility_and_trajectory():
    with strahler.Game.from_text(BRANCH) as game:
        run = strahler.trace(game, 'lifting', kary=2)
        state = run.state(-1)
        host = state.hosts[0]
        assert host.metrics == dict(nodes=3, leaves=2, height=2, strahler=2, positions=9)
        assert [(p.path, p.part) for p in host.positions] == [
            ((), 'H'), ((), 'T'), ((0,), 'H'), ((0,), 'T'), ((0,), 'S'),
            ((1,), 'H'), ((1,), 'T'), ((1,), 'S'), ((), 'S')]
        for priority, accepted in [(0, [1, 2, 4, 5, 7, 8]), (1, [1, 4, 7, 8]), (2, [0, 8])]:
            assert [p.index for p in host.positions if p.accepts(priority)] == accepted
        assert [host.by_path[p].core for p in ((), (0,), (1,))] == [(0, 8), (2, 4), (5, 7)]
        lifts = [e for e in run.events if e['event'] == 'lift' and e['player'] == 0 and e['vertex'] == 4]
        assert [e['to'] for e in lifts] == [2, 4, 5]
        assert state.raw_labels[0][4] == ((1,), 'H', 0)
        assert run.result.regions[0] == frozenset(game.vertices)
        assert strahler.check(game, run.result.trees[0], 0).ok
        with pytest.raises(ValueError, match='native'):
            metadata = deepcopy(next(e['host'] for e in run.events if 'host' in e))
            metadata['nodes'][1]['positions'][2] = 5
            HostSnapshot.from_metadata(metadata)
    with strahler.Game.from_text(BRANCH) as game:
        limited = strahler.trace(game, 'lifting', kary=1)
        assert 4 in limited.result.unresolved
        assert limited.state(-1).raw_labels[0][4][1] == 'S'
        assert 4 not in limited.result.regions[1]  # TOP does not mean opponent-won.


@pytest.mark.parametrize('options', [dict(kary=2), dict(tree_k=1, tree_t=0),
    dict(adaptive_t=True), dict(adaptive_t='incremental', iter_shave=True), dict(compress=True, kary=2)])
@pytest.mark.parametrize('algorithm', ['lifting', 'lifting-sym'])
def test_every_native_short_lift_matches_host_and_pre_round(options, algorithm):
    if algorithm.endswith('-sym') and 'adaptive_t' in options:
        options = dict(search='lexicographic', iter_shave=options.get('iter_shave', False))
    # Gaps, parity reversal, external IDs and both root parities.
    text = '10 8 0 10;20 5 1 10;30 2 0 30;40 5 0 30;50 2 1 50,40;'
    with strahler.Game.from_text(text) as game:
        run = strahler.trace(game, algorithm, **options)
        cursor = TraceCursor(run, 7)
        for i, event in enumerate(run.events):
            if event['event'] == 'lift':
                cursor.seek(i)
                explanation = cursor.explain_lift()
                assert explanation['next'] == event['to']
                assert not explanation['valid']
                assert explanation.get('native_match') is not False
        cursor.seek(-1)
        for player, host in enumerate(cursor.state.hosts):
            if host is not None:
                assert host.player == player
                assert set(host.priority_map) == set(host.active)


def test_synchronous_h_and_lazy_witnesses():
    with strahler.Game.from_text(BRANCH) as game:
        run = strahler.trace(game, 'lifting', kary=2)
        cursor = TraceCursor(run)
        explanations = [cursor.explain_lift(i) for i, e in enumerate(run.events)
                        if e['event'] == 'lift' and e['player'] == 0]
        first = explanations[:3]
        assert len({e['before'] for e in first}) == 1
        assert [e['applied'] for e in first] == [[], [2], [2, 3]]
        assert all(e['target'] == [0] and e['reached'] == [0, 1] for e in first)
        h, side = explanations[-2:]
        assert h['round'] == 2 and h['edges'] == [[4, 3]] and h['threshold'] == 4
        assert side['round'] == 3 and side['edges'] == [[4, 4]] and side['avoidance'] == [4]
        assert side['native_match'] is True
        cursor.seek(0)
        selected = cursor.next_lift_index(4, 0)
        assert run.events[selected]['vertex'] == 4
        cursor.seek(selected)
        assert cursor.next_lift_index(4, 0) > selected


def test_file_host_is_self_contained_and_legacy_is_unavailable(tmp_path):
    path = tmp_path / 'host.tree'
    path.write_text('0|1|')
    with strahler.Game.from_text(BRANCH) as game:
        run = strahler.trace(game, 'lifting', tree_file=path)
        path.unlink()
        assert run.state(-1).hosts[0].metrics['nodes'] == 3
        events = deepcopy(run.events)
        for event in events:
            event.pop('host', None)
        old = strahler.Trace(game, events, run.result)
        assert old.state(-1).hosts == (None, None)
        cursor = TraceCursor(old)
        cursor.seek(next(i for i, e in enumerate(events) if e['event'] == 'lift'))
        assert cursor.explain_lift()['kind'] == 'unavailable'
        assert old.state(-1).trees == run.result.trees


def test_arbitrary_uneven_file_host_ordinals(tmp_path):
    path = tmp_path / 'uneven.tree'
    path.write_text('0,0|0,1|1|')
    with strahler.Game.from_text('10 4 0 10;20 2 0 20;30 1 1 10;40 0 0 40;50 0 1 40,20;') as game:
        run = strahler.trace(game, 'lifting', tree_file=path)
        path.unlink()
        cursor = TraceCursor(run)
        for player, host in enumerate(run.state(-1).hosts):
            assert host.metrics == dict(nodes=5, leaves=3, height=3, strahler=2, positions=15)
            assert [n.h for n in host.nodes] == [4+player,2+player,player,player,2+player]
            # Pruning by priority must agree with a complete linear scan,
            # including jumps over whole incompatible subtrees.
            for priority in range(7):
                for start in range(-1, len(host.positions)):
                    expected = next((p.index for p in host.positions[start+1:] if p.accepts(priority)), None)
                    assert host.next_compatible_position(start, priority) == expected
        for i,e in enumerate(run.events):
            if e['event']=='lift':
                explanation = cursor.explain_lift(i)
                assert explanation['next']==e['to'] and not explanation['valid']


def test_lazy_symmetric_prefix_and_epoch_seek():
    with strahler.Game.from_text(BRANCH) as game:
        run = strahler.trace(game, 'zielonka-sym')
        events = [i for i, e in enumerate(run.events) if e.get('phase') == 'host_node']
        assert events
        final = run.state(-1).hosts[0]
        assert not final.complete
        assert final.next_compatible_position(0, 0) is None
        early = run.state(events[0] - 1).hosts[run.events[events[0]]['player']]
        later = run.state(events[0]).hosts[early.player]
        assert len(later.nodes) == len(early.nodes) + 1
        assert later.epoch == early.epoch
        assert run.state(-1).trees == run.result.trees
