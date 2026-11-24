#!/usr/bin/env python3
"""Compare bounded STRPM with least lifting over independently generated leaves."""

from functools import lru_cache
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]


def paths(node, prefix=()):
    if not node['children']:
        yield prefix, node.get('vertices', [])
    for edge in node['children']:
        yield from paths(edge['node'], prefix + (edge['label'],))


@lru_cache(None)
def leaves(k, t, depth):
    value = subprocess.run(
        [str(BINARY.parent / 'genstree'), f'--strahler={k}',
         f'--log-leaves={t}', f'--height={depth + 1}', '--json'],
        capture_output=True, text=True, check=True, timeout=30)
    return [p for p, _ in paths(json.loads(value.stdout)['tree'])]


def least_labels(game, domain, player, depth):
    """A synchronous finite-chain solver, without native successor/refill code."""
    top = len(domain)
    transitions = []
    for cutoff in range(-1, depth):
        starts = {}
        for rank, leaf in enumerate(domain):
            starts.setdefault(leaf[:cutoff + 1], rank)
        beginnings = list(starts.values())
        following = dict(zip(beginnings, beginnings[1:] + [top]))
        transitions.append((
            [starts[leaf[:cutoff + 1]] for leaf in domain] + [top],
            [following[starts[leaf[:cutoff + 1]]] for leaf in domain] + [top]))
    labels = [0] * len(game)
    for _ in range(len(game) * top + 1):
        updated = []
        for v, (priority, owner, edges) in enumerate(game):
            cutoff = depth - ((priority + 1) // 2 if player else priority // 2) - 1
            transition = transitions[cutoff + 1][priority % 2 != player]
            candidates = [transition[labels[to]] for to in edges]
            best = min(candidates) if owner == player else max(candidates)
            updated.append(max(labels[v], best))
        if updated == labels:
            return labels
        labels = updated
    raise AssertionError('finite lifting did not converge')


class BoundedStrpmTests(unittest.TestCase):
    def compare(self, game, k, t):
        priority_map = {}
        previous = -1
        for priority in sorted({p for p, _, _ in game}):
            previous += 1 if previous % 2 != priority % 2 else 2
            priority_map[priority] = previous
        game = [(priority_map[p], o, edges) for p, o, edges in game]
        text = f'parity {len(game) - 1};\n' + ''.join(
            f'{v} {p} {o} ' + ','.join(map(str, edges)) + ';\n'
            for v, (p, o, edges) in enumerate(game))
        maximum = max(p for p, _, _ in game)
        expected = []
        domains = []
        for player in range(2):
            depth = (maximum + 1) // 2 if player == 0 else maximum // 2 + 1
            domain = leaves(min(k, depth + 1), t, depth)
            domains.append(domain)
            expected.append(least_labels(game, domain, player, depth))
        for backend in ('scalar', 'simd'):
            with self.subTest(k=k, t=t, backend=backend):
                result = subprocess.run(
                    [str(BINARY), f'--k={k}', f'--t={t}', '--format=json'],
                    input=text, text=True, capture_output=True, check=True,
                    env=dict(os.environ, STRAHLER_PM_BACKEND=backend), timeout=30)
                value = json.loads(result.stdout)['rounds'][0]
                finite = set()
                for player, tree in enumerate(value['trees']):
                    domain = domains[player]
                    ranks = {leaf: i for i, leaf in enumerate(domain)}
                    actual = [len(domain)] * len(game)
                    if tree['tree'] is not None:
                        for leaf, vertices in paths(tree['tree']):
                            # k=1 uses a legacy dummy string for its sole leaf.
                            rank = 0 if len(domain) == 1 else ranks[leaf]
                            for v in vertices:
                                actual[v] = rank
                                finite.add(v)
                    self.assertEqual(actual, expected[player])
                self.assertEqual(set(value['unresolved']), set(range(len(game))) - finite)

    def test_regression_and_asymmetric_heights(self):
        rows = (ROOT / 'examples/strpm-nonstrict-tail.pg').read_text().splitlines()[1:]
        game = [(int(p), int(o), tuple(map(int, edges.rstrip(';').split(','))))
                for _, p, o, edges in map(str.split, rows)]
        for t in (1, 2):
            self.compare(game, 2, t)
        chain = [(p, 0, (min(v + 1, 6),))
                 for v, p in enumerate((1, 1, 2, 1, 1, 3, 0))]
        self.compare(chain, 3, 1)
        self.compare(chain[:-1] + [(4, 0, (6,))], 3, 2)

    def test_small_games(self):
        rng = random.Random(713)
        for _ in range(12):
            size = rng.randrange(2, 9)
            game = [(rng.randrange(1, 6), rng.randrange(2),
                     tuple(rng.sample(range(size), rng.randrange(1, min(size, 3) + 1))))
                    for _ in range(size)]
            for k, t in ((1, 0), (1, 2), (2, 1), (2, 2), (3, 1)):
                self.compare(game, k, t)


if __name__ == '__main__':
    unittest.main()
