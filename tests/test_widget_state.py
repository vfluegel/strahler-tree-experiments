"""Replay invariants and independently located badges, without frontend extras."""

from copy import deepcopy
import importlib.util
from pathlib import Path
import random

import pytest
import strahler
from strahler._replay import Replay
from strahler._widget_state import TraceCursor
from otj22 import hk

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize('algorithm', ['zielonka', 'zielonka-sym', 'lifting', 'lifting-sym'])
@pytest.mark.parametrize('text', [(ROOT / 'tests/games/otj22-fig1.gm').read_text(), hk(4)], ids=['fig1', 'h4'])
def test_checkpoints_match_full_replay(algorithm, text):
    with strahler.Game.from_text(text) as game:
        run = strahler.trace(game, algorithm)
        cursor = TraceCursor(run, checkpoint_interval=17)
        replay, expected = Replay(), []
        for i, event in enumerate(run.events):
            replay.apply(event)
            expected.append(strahler.State(replay, event, i).trees)
        last = cursor.seek(-1)
        assert last.trees == run.result.trees
        saved = deepcopy(last.trees)
        indices = [0, 16, 17, 18, len(run) - 2] + random.Random(42).sample(range(len(run)), 20)
        for index in reversed(indices):
            assert cursor.seek(index).trees == expected[index]
        assert last.trees == saved  # Later replay never mutates returned snapshots.
        assert cursor.seek(-1).trees == run.result.trees
        assert len(cursor.checkpoints) <= len(run) // 17 + 2


@pytest.mark.parametrize('algorithm', ['zielonka', 'zielonka-sym'])
@pytest.mark.parametrize('text', [(ROOT / 'tests/games/otj22-fig1.gm').read_text(), hk(4)], ids=['fig1', 'h4'])
def test_badges_and_claims(algorithm, text):
    with strahler.Game.from_text(text) as game:
        run = strahler.trace(game, algorithm)
        cursor = TraceCursor(run, 23)
        for index, event in enumerate(run.events):
            if event['event'] != 'check':
                continue
            cursor.seek(index)
            player = event['player']
            badge = cursor.badge(player)
            independent = strahler.check(game, run.state(index).candidate(player, event['path'], event['subgame']),
                                        player, event['subgame'])
            assert badge['claim'] == dict(index=index, event=event)
            assert badge['ok'] == independent.ok == event['is_attractor_decomposition']
            assert (badge['condition'], badge['path'], badge['vertex']) == (
                independent.condition.name, list(independent.path), independent.vertex)
        cursor.seek(-1)
        assert all(cursor.badge(p)['ok'] for p in range(2))
        cursor.seek(0)
        assert all(cursor.badge(p)['claim'] is None for p in range(2))
        if algorithm == 'zielonka':
            assert 'check' not in cursor.kinds  # Never invent Algorithm 1 claims.


def test_navigation_stack_and_checkpoint_context():
    with strahler.Game.from_text(hk(4)) as game:
        run = strahler.trace(game, 'zielonka-sym')
        cursor = TraceCursor(run, 11)
        cursor.seek(0)
        assert not cursor.stack
        first = cursor.next_index('call')
        cursor.seek(first)
        assert cursor.stack[-1]['event'] == run.events[first]
        second = cursor.next_index('call')
        cursor.seek(second)
        assert len(cursor.stack) == 2
        context = cursor._snapshot()
        cursor.seek(-1)
        cursor.seek(second)
        assert cursor.stack == context[0] and cursor.active == context[1] and cursor.claims == context[2]
        assert cursor.subgame == run.events[second]['subgame']
        assert cursor.next_index('nonexistent') == second
        with pytest.raises(IndexError):
            cursor.seek(len(run))
        with pytest.raises(TypeError):
            cursor.seek(0.5)
        assert cursor.calls[1]['parent'] == first


@pytest.mark.parametrize('options', [dict(tree_k=2), dict(adaptive_t=True),
                                      dict(adaptive_t='incremental', iter_shave=True)])
def test_lifting_partial_candidates(options):
    with strahler.Game.from_text(hk(2)) as game:
        run = strahler.trace(game, 'lifting', **options)
        assert run.result.complete and not run.result.unresolved
        cursor = TraceCursor(run, 19)
        observed = set()
        for index, event in enumerate(run.events):
            cursor.seek(index)
            if event['event'] in ('lift', 'round_end', 'phase'):
                for player in range(2):
                    badge = cursor.badge(player)
                    verdict = cursor.verify(player)
                    assert badge['ok'] == verdict.ok
                    assert badge['condition'] == verdict.condition.name
                observed.add(event['event'])
        assert observed == {'lift', 'round_end', 'phase'}
        assert all(cursor.verify(p).ok for p in range(2))


def test_large_recording_is_lazy_and_bounded(monkeypatch):
    with strahler.Game.from_text('0 0 0 0;') as game:
        result = strahler.decompose(game, 'zielonka')
        header = dict(event='header', schema='strahler-trace/1', algorithm='lifting')
        events = [header] + [dict(event='round_end', step=i, player=0, path=[]) for i in range(1, 100001)]
        run = strahler.Trace(game, events, result)
        cursor = TraceCursor(run)
        assert list(cursor.checkpoints) == [-1]
        cursor.seek(-1)
        assert len(cursor.checkpoints) <= 130
        cursor.seek(99990)
        checkpoint = max(i for i in cursor.checkpoints if i <= 99990)
        assert 99990 - checkpoint < cursor.interval
        assert len(cursor.ticks) <= 1000
        cursor.seek(1)
        applied = 0
        original = Replay.apply
        def count(replay, event):
            nonlocal applied
            applied += 1
            original(replay, event)
        monkeypatch.setattr(Replay, 'apply', count)
        cursor.seek(-1)
        assert applied < cursor.interval  # Forward jumps also reuse checkpoints.


def examples_otj():
    spec = importlib.util.spec_from_file_location('examples_otj22', ROOT / 'jupyter-examples/otj22.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_shipped_fixtures_and_counting_observer(monkeypatch):
    example = examples_otj()
    assert (ROOT / 'jupyter-examples/otj22-fig1.gm').read_bytes() == (ROOT / 'tests/games/otj22-fig1.gm').read_bytes()
    for k in (1, 2, 4, 8):
        assert example.hk(k) == hk(k)
        assert set(example.hk_coordinates(k)) == set(range(5*k))
    with strahler.Game.from_file(ROOT / 'jupyter-examples/otj22-fig1.gm') as game:
        assert strahler.check(game, example.PAPER_FIG1, 0).ok
    # The counting observer must reuse the loaded library without a path override.
    monkeypatch.delenv('STRAHLER_LIB', raising=False)
    for algorithm in ('zielonka', 'zielonka-sym'):
        with strahler.Game.from_text(hk(4)) as game:
            expected = sum(e['event'] == 'call' for e in strahler.trace(game, algorithm).events)
        assert example.count_calls(hk(4), algorithm) == expected
    with pytest.raises(strahler.StrahlerError):
        example.count_calls('invalid game', 'zielonka')


def test_empty_core_diagnostics_survive_materialization():
    with strahler.Game.from_text('0 2 1 1;1 1 1 1;2 1 1 2;') as game:
        result = strahler.decompose(game)
        events = [dict(event='header', schema='strahler-trace/1', algorithm='lifting'),
                  dict(event='phase', step=1, player=0, path=[], phase='labels_reset'),
                  dict(event='labels', step=2, player=0, path=[0], vertex=1, part='S', h=0),
                  dict(event='labels', step=3, player=0, path=[1], vertex=2, part='S', h=0)]
        cursor = TraceCursor(strahler.Trace(game, events, result), 2)
        cursor.seek(-1)
        assert [p['condition'] for p in cursor.badge(0)['problems']] == [22, 22]
        assert not cursor.verify(0).ok
        with pytest.raises(ValueError, match='player'):
            cursor.verify(2)
        with pytest.raises(ValueError, match='positive'):
            TraceCursor(cursor.run, 0)


def test_base_import_does_not_import_widgets():
    import os
    import subprocess
    import sys
    subprocess.run([sys.executable, '-c', 'import strahler, sys; '
                    'assert not {"anywidget", "traitlets", "strahler.widgets"} & sys.modules.keys()'],
                   check=True, env=os.environ.copy())


def test_lazy_inspector_preserves_opponent_exit_from_safe():
    with strahler.Game.from_text('10 2 0 10;20 0 1 10,30;30 0 0 30;') as game:
        run = strahler.trace(game, 'lifting', kary=2)
        metadata = next(e['host'] for e in run.events if 'host' in e)
        events = [run.events[0], dict(event='phase', phase='host_built', host=metadata),
                  dict(event='phase', phase='labels_reset'),
                  dict(event='labels', vertex=10, path=[], part='H', h=2),
                  dict(event='labels', vertex=20, path=[], part='T', h=2),
                  dict(event='labels', vertex=30, path=[0], part='H', h=0),
                  dict(event='labels', vertex=20, path=[0], part='H', h=0),
                  dict(event='lift', vertex=20, path=[0], part='H', h=0, **{'from':1,'to':2})]
        for i,e in enumerate(events[1:],1):
            e.update(step=i, player=0)
            e.setdefault('path', [])
        cursor = TraceCursor(strahler.Trace(game,events,run.result))
        cursor.seek(-1)
        explanation = cursor.explain_lift()
        assert explanation['safe'] == [10,20] and explanation['target'] == [10]
        assert explanation['reached'] == [10] and not explanation['valid']
        assert explanation['escape'] == [30] and explanation['edges'] == [[20,30]]


def test_host_labels_do_not_cross_epoch_boundaries():
    with strahler.Game.from_text(hk(2)) as game:
        run = strahler.trace(game, 'lifting', search='lexicographic')
        epochs = [i for i,e in enumerate(run.events) if 'host' in e and e['player']==0]
        assert epochs
        for epoch, index in enumerate(epochs, 1):
            state = run.state(index)
            assert state.hosts[0].epoch == epoch and not state.raw_labels[0]
        for index in reversed(epochs):
            assert run.state(index).hosts[0].epoch == epochs.index(index)+1


def test_iterative_checkpoint_context():
    with strahler.Game.from_file(ROOT / 'tests/games/iter_shave.pg') as game:
        run = strahler.trace(game, 'lifting', tree_k=1, iter_shave=True)
        cursor = TraceCursor(run, 7)
        indices = [i for i, e in enumerate(run.events) if e.get('phase') == 'shave_applied']
        assert indices
        cursor.seek(-1)
        for index in reversed(indices):
            state = cursor.seek(index)
            expected = run.state(index)
            assert state.trees == expected.trees and state.shaves == expected.shaves
            assert cursor.active == run.events[index]['vertices']
        assert cursor.seek(-1).trees == run.result.trees
