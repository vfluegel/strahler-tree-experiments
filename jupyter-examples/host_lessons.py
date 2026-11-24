"""Small native ordinary-lifting lessons; large trace collection is opt-in."""

import bz2
from pathlib import Path

import strahler
from strahler.widgets import Stepper

CHAIN = '''parity 3;
0 2 0 0 "h";
1 1 1 0 "t";
2 0 0 2 "a";
3 1 0 2 "x";
'''
BRANCH = CHAIN.replace('parity 3;', 'parity 4;') + '4 0 1 4,3 "b";\n'


def nester2_comparison():
    """Stream final host occupancy, discarding the large native event timeline."""
    path = Path(__file__).resolve().parent / 'nester/Nestern=2.gm.bz2'
    game = strahler.Game.from_text(bz2.open(path, 'rt').read())
    results, states = {}, {}
    for k, t in ((1, 0), (2, 1)):
        result, state = strahler.trace_snapshot(
            game, 'lifting', tree_k=k, tree_t=t, trace_limit=150000)
        results[k, t] = result
        states[k, t] = state
        for player in range(2):
            assert strahler.check(game, result.trees[player], player, result.regions[player]).ok
    return game, results, states


def interesting_lifts(run):
    """Locate sibling crossings and a concrete H predecessor invalidation.

    Only event indices are retained. Inspectors reconstruct full round-start
    states on demand. A predecessor example is checked before being suggested.
    """
    from strahler._widget_state import TraceCursor
    cursor = TraceCursor(run)
    hosts = [None, None]
    previous_round, current_round = [{}, {}], [{}, {}]
    sibling = predecessor = None
    for i, e in enumerate(run.events):
        player = e.get('player', 0)
        if 'host' in e:
            hosts[player] = strahler.HostSnapshot.from_metadata(e['host'], player)
            previous_round[player], current_round[player] = {}, {}
        if e['event'] == 'round_end':
            previous_round[player], current_round[player] = current_round[player], {}
        if e['event'] != 'lift' or hosts[player] is None:
            continue
        host = hosts[player]
        source, destination = host.positions[e['from']], host.positions[e['to']]
        if sibling is None:
            for a, b in zip(source.path, destination.path):
                if a != b:
                    if a < b:
                        sibling = i
                    break
        if (predecessor is None and source.part == 'H'
                and set(run.game.successors[e['vertex']]) & previous_round[player].keys()):
            explanation = cursor.explain_lift(i)
            # Before a previous-round successor moved it was strictly below
            # after; afterwards it is a concrete offending H edge.
            if any(w in previous_round[player] and
                   previous_round[player][w][0] < explanation['threshold'] <= previous_round[player][w][1]
                   for v, w in explanation['edges']):
                predecessor = i
        current_round[player][e['vertex']] = e['from'], e['to']
        if sibling is not None and predecessor is not None:
            break
    return dict(sibling_crossing=sibling, predecessor_invalidation=predecessor)


def nester2_explorer(game, k=2, t=1, event_limit=150000):
    """Generate a bounded native trace locally, then open its suggested lift."""
    run = strahler.trace(game, 'lifting', tree_k=k, tree_t=t, trace_limit=event_limit)
    suggestions = interesting_lifts(run)
    view = Stepper(run)
    index = suggestions['sibling_crossing'] or next(i for i, e in enumerate(run.events) if e['event'] == 'lift')
    view.seek(index)
    view.game_view.selected = run.events[index]['vertex']
    return view, suggestions
