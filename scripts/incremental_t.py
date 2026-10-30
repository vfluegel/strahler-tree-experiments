#!/usr/bin/env python3
"""Reproduce the operator and compare cold and certified-TOP lifting.

The independent model is checked against pg2adot's complete witnesses and lift
counts on every input. It also tests dominion removal, keeping the original
compact priorities on the remaining game (after preprocessing and earlier shaving rounds).
"""

import argparse
from collections import Counter
from functools import cache
import json
from pathlib import Path
import random
import re
import time

from find_disagreements import Node, Runner, embeds, random_game, read_game, structured_game


@cache
def utree(k, t, h):
    if h == 1:
        return ()
    if k == 1:
        return (utree(1, t, h - 1),)
    if t == 0:
        return (utree(k - 1, 0, h - 1),)
    if h == k:
        return vtree(k, t, h)
    return vtree(k, t, h) + (utree(k, t, h - 1),) + vtree(k, t, h)


@cache
def vtree(k, t, h):
    if t == 0:
        return (utree(k - 1, 0, h - 1),)
    return vtree(k, t - 1, h) + (utree(k - 1, t, h - 1),) + vtree(k, t - 1, h)


class Host:
    def __init__(self, shape, player, bound):
        self.positions, self.nodes, self.player = [], [], player

        def build(children, level):
            regular = len(self.positions)
            self.positions.extend([('regular', level, regular), ('lazy', level - 1, None)])
            descendants = [build(c, level - 2) for c in children]
            after = len(self.positions)
            self.positions.append(('lazy', level + 1, None))
            self.positions[regular] = ('regular', level, after)
            self.nodes.append((regular, after, level, descendants))
            return self.nodes[-1]

        self.root = build(shape, bound)
        self.top = len(self.positions) - 1
        self.positions[-1] = ('top', None, None)

    def compatible(self, position, priority):
        kind, level, _ = self.positions[position]
        return kind == 'top' or (priority == level if kind == 'regular' else priority <= level)

    def first(self, start, priority):
        return next(i for i in range(start, self.top + 1) if self.compatible(i, priority))


def compact(game):
    mapping, last = {}, -1
    for p in sorted({p for p, _, _ in game}):
        last += 1
        last += (last % 2 != p % 2)
        mapping[p] = last
    return tuple((mapping[p], o, e) for p, o, e in game)


def attractor(game, active, safe, target, player):
    result = set(target)
    while True:
        added = set()
        for v in safe - result:
            edges = set(game[v][2]) & active
            if (bool(edges & result) if game[v][1] == player else edges <= result):
                added.add(v)
        if not added:
            return result
        result |= added


def lift(game, active, host, known_losing=frozenset(), initial=None):
    assert all(set(game[v][2]) & active for v in active), 'non-total active game'
    labels = dict(initial) if initial is not None else {
        v: host.top if v in known_losing else host.first(0, game[v][0]) for v in active}
    lifts = 0
    while True:
        invalid = set()
        for position in sorted(set(labels.values()) - {host.top}):
            fibre = {v for v in active if labels[v] == position}
            kind, _, after = host.positions[position]
            if kind == 'regular':
                for v in fibre:
                    allowed = [labels[w] < after for w in game[v][2] if w in active]
                    if not (any(allowed) if game[v][1] == host.player else all(allowed)):
                        invalid.add(v)
            else:
                below = {v for v in active if labels[v] < position}
                invalid |= fibre - attractor(game, active, below | fibre, below, host.player)
        if not invalid:
            return labels, lifts
        lifts += len(invalid)
        for v in invalid:
            labels[v] = host.first(labels[v] + 1, game[v][0])


def materialize(host, labels):
    def visit(node):
        regular, after, level, children = node
        region = tuple(sorted(v for v, label in labels.items() if regular <= label < after))
        if not region:
            return None
        top = tuple(v for v in region if labels[v] <= regular + 1)
        entries = []
        for child in children:
            subtree = visit(child)
            attraction = tuple(sorted(v for v in region if child[0] <= labels[v] <= child[1]))
            if attraction:
                assert subtree is not None
                entries.append((attraction, subtree))
        return Node(level, region, top, tuple(entries))
    return visit(host.root)


def verify_tree(game, node, player):
    if node is None:
        return
    active = set(node.region)
    assert max(game[v][0] for v in active) <= node.bound
    target = {v for v in active if game[v][0] == node.bound}
    assert attractor(game, active, active, target, player) == set(node.top)
    active -= set(node.top)
    for attraction, child in node.children:
        trap = set(child.region)
        assert trap <= active and child.bound == node.bound - 2
        assert max(game[v][0] for v in trap) <= node.bound - 2
        for v in trap:
            edges = set(game[v][2]) & active
            assert bool(edges & trap) if game[v][1] == player else edges <= trap
        assert attractor(game, active, active, trap, player) == set(attraction)
        verify_tree(game, child, player)
        active -= set(attraction)
    assert not active


def residual_scheme(game):
    """Scheme (i): Even then Odd, remove each dominion's attractor immediately.

    Keep fixed bounds/heights. An empty root two levels higher composes each
    player's ordered per-round witnesses into a checked classic witness.
    """
    active, regions, entries = set(range(len(game))), [set(), set()], [[], []]
    bounds = [max(p for p, _, _ in game)] * 2
    for player in range(2):
        bounds[player] += bounds[player] % 2 != player
    lifts = 0
    for t in range(len(game).bit_length()):
        for player in range(2):
            if not active:
                break
            bound = bounds[player]
            h = (bound - player) // 2 + 1
            host = Host(utree(min(h, t + 1), t, h), player, bound)
            labels, count = lift(game, active, host)
            lifts += count
            node = materialize(host, labels)
            if node:
                attraction = attractor(game, active, active, set(node.region), player)
                entries[player].append((tuple(sorted(attraction)), node))
                regions[player] |= attraction
                active -= attraction
        if not active:
            break
    assert not active
    trees = tuple(Node(bounds[p] + 2, tuple(sorted(regions[p])), (), tuple(entries[p]))
                  if regions[p] else None for p in range(2))
    for p in range(2):
        verify_tree(game, trees[p], p)
        for v in regions[p]:
            edges = set(game[v][2])
            assert bool(edges & regions[p]) if game[v][1] == p else edges <= regions[p]
    return trees, lifts, t


def host_labels(node, host, game):
    """Greedy embedding of a classic witness into a fixed host, for thesis ⊑."""
    labels = {v: host.top for v in node.region}

    def visit(witness, target):
        regular, _, _, children = target
        for v in witness.top:
            labels[v] = regular if game[v][0] == witness.bound else regular + 1
        available = iter(children)
        for attraction, child in witness.children:
            for candidate in available:
                shape = shape_of_host(candidate)
                if embeds(child.shape(), shape):
                    visit(child, candidate)
                    for v in set(attraction) - set(child.region):
                        labels[v] = candidate[1]
                    break
            else:
                raise ValueError('witness does not embed in host')

    visit(node, host.root)
    return labels


def shape_of_host(node):
    return tuple(shape_of_host(c) for c in node[3])


def leaf_stream(shape, path=()):
    if not shape:
        return ','.join(path) + '|'
    return ''.join(leaf_stream(child, path + (bin(i + 1)[2:],))
                   for i, child in enumerate(shape))


def stats_lifts(text, total=False):
    name = 'total_lifts' if total else 'lifts'
    return sum(map(int, re.findall(rf'\b{name}=(\d+)', text)))


# A 16-leaf subtree of U^3_{2,4} that does not embed in U^4_{3,4}.
SEPARATING_TREE = ((((), (), (), ()),), (((), ()), ((),), ((),)),
                  (((), (), (), ()), ((),), ((),), ((),), ((),)))


def saturated_game(shape):
    """All vertices belong to Odd. Every regular vertex has a self-loop.

    A regular vertex points to every position below its after boundary; a
    lazy vertex points to every earlier position. Omit the unused level -1
    leaves. A host solves the whole game iff it embeds shape; see the proof
    in docs/incremental-t.md.
    """
    def height(s):
        return 1 + max(map(height, s), default=0)
    host = Host(shape, 0, 2 * (height(shape) - 1))
    positions = [i for i, (_, level, _) in enumerate(host.positions[:-1]) if level >= 0]
    rows = []
    for i in positions:
        kind, level, after = host.positions[i]
        limit = after if kind == 'regular' else i
        edges = tuple(v for v, j in enumerate(positions) if j < limit)
        assert edges
        rows.append((level, 1, edges))
    return tuple(rows)


def nonnested_example():
    assert embeds(SEPARATING_TREE, utree(3, 2, 4))
    assert not embeds(SEPARATING_TREE, utree(4, 3, 4))
    game = saturated_game(SEPARATING_TREE)
    counts = []
    for k, t in ((3, 2), (4, 3)):
        host = Host(utree(k, t, 4), 0, 6)
        labels, _ = lift(game, set(range(len(game))), host)
        counts.append(sum(label < host.top for label in labels.values()))
    assert counts[0] == len(game) and counts[1] < counts[0]
    return game, counts


def adaptive_nonnested_example():
    game, _ = nonnested_example()

    def balanced(h):
        return () if h == 1 else (balanced(h - 1), balanced(h - 1))

    offset = len(game)
    game += tuple((p, o, tuple(v + offset for v in edges))
                  for p, o, edges in saturated_game(balanced(4)))
    counts = []
    for k, t in ((3, 2), (4, 3)):
        host = Host(utree(k, t, 4), 0, 6)
        labels, _ = lift(game, set(range(len(game))), host)
        counts.append(sum(label < host.top for label in labels.values()))
    assert counts == [70, 36]
    return game, counts


def experiment(game, runner):
    game = compact(game)
    cold_adaptive, adaptive_stats = runner.run(game, extra=('--adaptive-t',))
    final_t = int(re.search(r'first_complete_t=(\d+)', adaptive_stats)[1])
    cold, cold_stats = runner.run(game, extra=('--adaptive-t', f'--start-t={final_t}'))
    warm, warm_stats = runner.run(game, extra=('--adaptive-t=incremental',))
    assert cold_adaptive == cold == warm, 'certified-TOP changed a witness'
    model_trees, model_lifts = [], 0
    maximum = max(p for p, _, _ in game)
    for player in range(2):
        bound = maximum + (maximum % 2 != player)
        h = (bound - player) // 2 + 1
        host = Host(utree(min(h, final_t + 1), final_t, h), player, bound)
        labels, count = lift(game, set(range(len(game))), host)
        model_trees.append(materialize(host, labels))
        model_lifts += count
    assert tuple(model_trees) == cold
    assert model_lifts == stats_lifts(cold_stats)
    residual, residual_lifts, residual_t = residual_scheme(game)
    assert tuple(n.region if n else () for n in residual) == tuple(n.region if n else () for n in cold)
    # Compare residual composition with cold after giving cold the same empty root.
    padded = tuple(Node(n.bound + 2, n.region, (), ((n.region, n),)) if n else None for n in cold)
    return dict(cold_final_t=final_t, residual_final_t=residual_t,
                cold_lifts=model_lifts, adaptive_lifts=stats_lifts(adaptive_stats, True),
                incremental_lifts=stats_lifts(warm_stats, True), residual_lifts=residual_lifts,
                residual_same_shape=all(a is None or a.shape() == b.shape() for a, b in zip(padded, residual)),
                residual_same_tree=padded == residual)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/pg2adot'))
    parser.add_argument('--samples', type=int, default=30)
    parser.add_argument('--seed', type=int, default=20261019)
    parser.add_argument('--output', type=Path, default=Path('/tmp/incremental-t.json'))
    args = parser.parse_args()
    runner, rng = Runner(args.binary.resolve(), 3), random.Random(args.seed)
    rows, totals, started = {}, Counter(), time.monotonic()
    excluded = {}
    games = [(f'random:{n},{h}:{i}', random_game(rng, n, h, 2))
             for n in (4, 8, 16, 32) for h in (2, 3, 4) for i in range(args.samples)]
    games += [(f'{family}:{n},{h}', structured_game(family, n, h))
              for n in (8, 16) for h in (2, 3, 4) for family in ('ladder', 'nested_cycles')]
    separating_game, counts = nonnested_example()
    _, adaptive_counts = adaptive_nonnested_example()
    games.append(('saturated_nonnesting', separating_game))
    for path in sorted(Path('examples').glob('*.pg')) + sorted(Path('tests/games').glob('*.pg')):
        if path.stat().st_size <= 1_000_000 and path.name != 'incomplete.pg':
            games.append((str(path), read_game(path, args.binary.resolve())))
        else:
            excluded[str(path)] = ('invalid parser fixture' if path.name == 'incomplete.pg'
                                   else 'large input; see timed CLI attempts in docs/research/examples.json')
    for name, game in games:
        row = experiment(game, runner)
        rows[name] = row
        totals['games'] += 1
        for key in ('cold_lifts', 'adaptive_lifts', 'incremental_lifts', 'residual_lifts'):
            totals[key] += row[key]
        totals['incremental_better_than_cold'] += row['incremental_lifts'] < row['cold_lifts']
        totals['incremental_better_than_adaptive'] += row['incremental_lifts'] < row['adaptive_lifts']
        totals['residual_same_shape'] += row['residual_same_shape']
        totals['residual_same_tree'] += row['residual_same_tree']
        if totals['games'] % 100 == 0:
            print(f'{totals["games"]} games', flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(seed=args.seed, totals=totals, games=rows,
                                          excluded=excluded,
                                          nonnested_dominion_sizes=counts,
                                          adaptive_nonnested_dominion_sizes=adaptive_counts,
                                          wall_seconds=time.monotonic() - started), indent=2) + '\n')
    print(dict(totals))


if __name__ == '__main__':
    main()
