"""Seeded differential checks of complete documents, statistics and dispatch.

Use --cases 32 to reproduce the handoff's seed-7019 regression workload.
An optional --baseline directory compares against pre-optimization binaries.
"""

import argparse
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile


def arena(rng, n):
    rows = [f'parity {n - 1};']
    for v in range(n):
        edges = ','.join(str(rng.randrange(n)) for _ in range(rng.randrange(1, 7)))
        rows.append(f'{v} {rng.randrange(9)} {rng.randrange(2)} {edges};')
    return '\n'.join(rows) + '\n'


def run(build, tool, flags, path, backend, report=False):
    env = dict(os.environ)
    env['STRAHLER_AD_BACKEND' if tool == 'pg2adot' else 'STRAHLER_PM_BACKEND'] = backend
    if report:
        env['STRAHLER_BACKEND_REPORT'] = '1'
    else:
        env.pop('STRAHLER_BACKEND_REPORT', None)
    result = subprocess.run([str(build / tool), *flags, '--format=json', '--stats', str(path)],
                            env=env, capture_output=True, check=True, timeout=60)
    document = json.loads(result.stdout)
    document.pop('version', None)
    return document, result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--cases', type=int, default=4)
    args = parser.parse_args()
    build = args.build.resolve()
    baseline = (args.baseline or args.build).resolve()
    rng = random.Random(7019)
    ad_modes = [
        ['--algorithm=lifting', '--tree-k=1', '--iter-shave'],
        ['--algorithm=lifting', '--tree-k=2', '--iter-shave'],
        ['--algorithm=lifting', '--adaptive-t', '--iter-shave'],
        ['--algorithm=lifting-sym', '--tree-k=1'],
        ['--algorithm=lifting', '--search=lexicographic', '--iter-shave'],
        ['--algorithm=lifting-sym', '--search=lexicographic', '--iter-shave'],
        ['--algorithm=lifting-sym'],
    ]
    pm_modes = [['--k=2', '--t=0'], ['--k=2', '--t=1'], ['--k=3', '--t=2'], [],
                ['--iter-shave'], ['--search=lexicographic', '--iter-shave'],
                ['--algorithm=succinct', '--bits=4'], ['--k=10', '--t=1'],
                ['--k=2', '--t=8']]
    comparisons = 0
    recursive = False
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'arena.gm'
        for sample in range(args.cases):
            path.write_text(arena(rng, rng.randrange(64, 161)), encoding='utf-8')
            for tool, modes, backends in [
                ('pg2adot', ad_modes, ('fused', 'packed', 'dense', 'queue', 'dirty', 'byte')),
                ('pg2pmdot', pm_modes, ('packed', 'lookup', 'compare', 'both')),
            ]:
                for flags in modes:
                    expected = run(baseline, tool, flags, path, 'scalar')
                    for backend in backends:
                        assert run(build, tool, flags, path, backend) == expected, (sample, tool, flags, backend)
                        comparisons += 1
            _, diagnostics = run(build, 'pg2adot', ad_modes[3], path, 'dirty', report=True)
            recursive |= b'AD symmetric recursive rank check:' in diagnostics
    assert recursive, 'recursive symmetric path was not exercised'
    print(f'{comparisons} seeded backend comparisons agree, including JSON, stats and recursive symmetric checks')


if __name__ == '__main__':
    main()
