#!/usr/bin/env python3
"""Small checks for the research harness, plus the checked witness goldens."""

import json
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from find_disagreements import Runner, differences, embeds, parse_dot, read_game, shrink, random_game
from incremental_t import (Host, compact, experiment, host_labels,
                           embed_larger_k, leaf_stream, lift, materialize,
                           nonnested_example, utree)

BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else ROOT / 'build/pg2adot'


class ResearchTests(unittest.TestCase):
    def setUp(self):
        self.runner = Runner(BINARY, 90)

    def test_goldens(self):
        paths = sorted((ROOT / 'tests/games').glob('disagree_*.pg'))
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(game=path.name):
                game = compact(read_game(path, BINARY))
                left, right = self.runner.compare(game)
                actual = {algorithm: [n.canonical() if n else None for n in nodes]
                          for algorithm, nodes in [('lifting', left), ('zielonka', right)]}
                # JSON normalizes immutable tuples to arrays.
                actual = json.loads(json.dumps(actual))
                golden = ROOT / 'tests/golden' / (path.stem + '.json')
                self.assertEqual(actual, json.loads(golden.read_text()))
                self.assertTrue(any(differences(a, b) for a, b in zip(left, right)))
                t = len(game).bit_length() - 1
                maximum = max(p for p, _, _ in game)
                for player in range(2):
                    if left[player]:
                        bound = maximum + (maximum % 2 != player)
                        h = (bound - player) // 2 + 1
                        host = Host(utree(min(h, t + 1), t, h), player, bound)
                        labels, _ = lift(game, set(range(len(game))), host)
                        self.assertEqual(materialize(host, labels), left[player])
                        other = host_labels(right[player], host, game)
                        self.assertTrue(all(labels[v] <= other[v] for v in other))

    def test_host_shapes_match_c(self):
        for k, t, h in [(1, 0, 1), (1, 3, 4), (2, 0, 3),
                        (2, 1, 5), (3, 2, 4), (4, 3, 4)]:
            dot = subprocess.run(
                [*self.runner.prefix, str(BINARY.parent / 'genstree'),
                 f'--strahler={k}', f'--log-leaves={t}', f'--height={h}', '--dot'],
                capture_output=True, text=True, check=True, timeout=90).stdout
            children = {}
            for parent, child in re.findall(r'(\d+) -- (\d+)', dot):
                children.setdefault(parent, []).append(child)
            def shape(node):
                return tuple(shape(child) for child in children.get(node, []))
            self.assertEqual(shape('1'), utree(k, t, h))

    def test_shrinker_keeps_totality_and_reaches_single_edit_minimum(self):
        game = ((0, 0, (0, 1)), (2, 1, (1,)))
        result = shrink(game, lambda candidate: any(p >= 1 for p, _, _ in candidate))
        self.assertEqual(result, ((1, 1, (0,)),))
        with self.assertRaises(ValueError):
            parse_dot('digraph {}')

    def test_model_and_thesis_order(self):
        rng = random.Random(23)
        for _ in range(12):
            game = compact(random_game(rng, 8, 3, 2))
            experiment(game, self.runner)  # exact C/model trees AND lift counts
            left, right = self.runner.compare(game)
            maximum = max(p for p, _, _ in game)
            for player in range(2):
                bound = maximum + (maximum % 2 != player)
                h = (bound - player) // 2 + 1
                host = Host(utree(min(h, 4), 3, h), player, bound)
                labels, _ = lift(game, set(range(len(game))), host)
                self.assertEqual(materialize(host, labels), left[player])
                if right[player]:
                    other = host_labels(right[player], host, game)
                    self.assertTrue(all(labels[v] <= other[v] for v in other))

    def test_adaptive_nonnesting_in_c(self):
        game, counts = nonnested_example()
        self.assertEqual(counts, [70, 0])
        with tempfile.TemporaryDirectory() as directory:
            for k, t, expected in [(3, 2, 70), (4, 3, 0)]:
                path = Path(directory) / f'host-{k}-{t}.tree'
                path.write_text(leaf_stream(utree(k, t, 4)))
                nodes, _ = self.runner.run(game, extra=(f'--tree-file={path}',))
                self.assertEqual(len(nodes[0].region) if nodes[0] else 0, expected)

    def test_cross_k_embedding_of_native_hosts(self):
        def native_leaves(k, t, h):
            result = subprocess.run(
                [*self.runner.prefix, str(BINARY.parent / 'genstree'),
                 f'--strahler={k}', f'--log-leaves={t}', f'--height={h}', '--json'],
                capture_output=True, text=True, check=True, timeout=90)
            def visit(node, path=()):
                if not node['children']:
                    yield path
                for child in node['children']:
                    yield from visit(child['node'], path + (child['label'],))
            return list(visit(json.loads(result.stdout)['tree']))

        # Check actual generated labels, including t=0, k=1, k'=h, and jumps
        # across several k values. Consecutive leaves also test all prefixes.
        for h in range(2, 6):
            for k in range(1, h):
                for t in range(3):
                    source = native_leaves(k, t, h)
                    for target_k in range(k + 1, h + 1):
                        target = native_leaves(target_k, t + k - 1, h)
                        index = {leaf: i for i, leaf in enumerate(target)}
                        mapped = [embed_larger_k(leaf, target_k) for leaf in source]
                        with self.subTest(h=h, k=k, t=t, target_k=target_k):
                            self.assertTrue(all(leaf in index for leaf in mapped))
                            self.assertEqual(sorted(index[leaf] for leaf in mapped),
                                             [index[leaf] for leaf in mapped])
                            for left, right, a, b in zip(source, source[1:], mapped, mapped[1:]):
                                for depth in range(h):
                                    self.assertEqual(left[:depth] == right[:depth],
                                                     a[:depth] == b[:depth])

    def test_cross_k_bound_is_sharp_at_full_height(self):
        for h in range(3, 7):
            for k in range(2, h):
                for t in (1, 2):
                    with self.subTest(h=h, k=k, t=t):
                        self.assertFalse(embeds(utree(k, t, h),
                                                utree(h, t + k - 2, h)))

    def test_adjoints_do_not_preserve_validity(self):
        old = Host(((),), 0, 2)
        new = Host(((), (), ()), 0, 2)
        second_regular = new.root[3][1][0]
        old_plus = old.root[3][0][1]
        game = ((0, 0, (0,)),)
        _, count = lift(game, {0}, new, initial={0: second_regular})
        self.assertEqual(count, 0)
        _, count = lift(game, {0}, old, initial={0: old_plus})
        self.assertGreater(count, 0)  # floor projection breaks a lazy condition
        game = ((2, 0, (1,)), (0, 0, (1,)))
        _, count = lift(game, {0, 1}, new, initial={0: 0, 1: second_regular})
        self.assertEqual(count, 0)
        _, count = lift(game, {0, 1}, old, initial={0: 0, 1: old.top})
        self.assertGreater(count, 0)  # ceiling projection breaks a regular one


if __name__ == '__main__':
    unittest.main()
