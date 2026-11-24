#!/usr/bin/env python3
"""Observer invariance, event replay, paper examples, and CLI limits."""

from collections import Counter
import json
import os
from pathlib import Path
import random
import re
import shlex
import subprocess
import sys
import tempfile
import time

from otj22 import hk
from game_cases import EXAMPLES, REGRESSIONS
from test_pg_game_property import generated_games
from trace_replay import Replay, check_input, vertices

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from gen_decomp import BACKENDS
from find_disagreements import Node, game_text, parse_dot, random_game

SLOW_PAIRS = {(game, backend + suffix)
              for game, backend in (('cabp_swp.ehoa.pg', 'lifting-adaptive'),
                                    ('thesis_f4.pg', 'lifting'))
              for suffix in ('', '+pre', '+iter', '+pre+iter')} | {
                  ('cabp_swp.ehoa.pg', 'zielonka-sym' + suffix)
                  for suffix in ('', '+pre')}


def canonical(node):
    if node is None:
        return None
    return Node(node['h'], tuple(sorted(vertices(node, False))),
                tuple(sorted(set(node['H']) | set(node['T']))),
                tuple((tuple(sorted(vertices(child))), canonical(child))
                      for child in node['children']))


def run(binary, checker, game, options, directory, counting=None):
    wrapper = shlex.split(os.environ.get('STRAHLER_TEST_WRAPPER', ''))
    trace = directory / 'trace.jsonl'
    command = [*wrapper, str(binary), '--stats', '--labels=sets',
               '--max-set-items=18446744073709551615', '--priority-mode=compact', *options,
               str(game)]
    started = time.monotonic()
    cold = subprocess.run(command, capture_output=True, timeout=600)
    cold_seconds = time.monotonic() - started
    if game.parent == ROOT / 'examples':
        print(f'tracing: {game.name} {options}', flush=True)
    started = time.monotonic()
    observed = subprocess.run([*wrapper, str(counting or binary),
                               *command[len(wrapper) + 1:-1], f'--trace={trace}',
                               '--trace-limit=18446744073709551615', command[-1]],
                              capture_output=True, timeout=600)
    if counting is not None:
        print(f'counting invariance times: {game.name} {options} '
              f'untraced={cold_seconds:.3f}s '
              f'observed={time.monotonic() - started:.3f}s', flush=True)
    assert (cold.returncode, cold.stdout, cold.stderr) == (
        observed.returncode, observed.stdout, observed.stderr), (game, options, cold.returncode, observed.returncode, cold.stderr, observed.stderr)
    if counting is not None and (game.name == 'thesis_f4.pg' or
                                (game.name == 'cabp_swp.ehoa.pg' and '--adaptive-t' in options)):
        assert cold.returncode == 0, cold.stderr
    if cold.returncode:
        print(f'matched failure: {game.name} exit={cold.returncode} {options}', flush=True)
        trace.unlink(missing_ok=True)
        return None
    if counting is not None:
        trace.unlink()
        return None
    replay = Replay()
    counts = Counter()
    checks, claims = [], []
    last = None
    with trace.open() as stream:
        for line in stream:
            event = json.loads(line)
            replay.apply(event)
            counts[event['event']] += 1
            if ('--algorithm=zielonka' in options and event['event'] == 'attractor'
                    and event['kind'] == 'side'):
                target_tree = replay.tree(event['player'], tuple(event['path']))
                assert vertices(target_tree, False) == set(event['target']), event
            if event['event'] == 'check':
                candidate = replay.tree(event['player'], tuple(event['path']),
                                         set(event['subgame']), candidate=True)
                checks.append(check_input(event, candidate))
                claims.append(event['is_attractor_decomposition'])
            last = event
    assert counts['header'] == counts['result'] == 1
    assert last['event'] == 'result'
    if checks:
        checked = subprocess.run([*wrapper, str(checker), str(game),
                                  str(int('--compress' in options))],
                                 input=''.join(checks), text=True,
                                 capture_output=True, check=True, timeout=600)
        actual = [bool(int(line.split()[0])) for line in checked.stdout.splitlines()]
        assert actual == claims, (game, options, [(i, a, b) for i, (a, b)
                                                 in enumerate(zip(actual, claims)) if a != b])
    if any(option in ('--algorithm=zielonka', '--algorithm=zielonka-sym') for option in options):
        assert counts['call'] == int(re.search(rb'calls=(\d+)', cold.stderr)[1])
    trees = last['result']['trees']
    assert tuple(canonical(t) for t in trees) == parse_dot('\n'.join(line for line in cold.stdout.decode().splitlines()
        if not re.search(r'(?:even|odd)_shav', line))), (game, options)
    trace.unlink()
    return counts, cold.stderr.decode(), trees


def figure_state(binary, directory):
    game = ROOT / 'tests/games/otj22-fig1.gm'
    rows = re.findall(r'^(\d+) \d+ ([01]) ([\d,]+)', game.read_text(), re.MULTILINE)
    graph = {int(v): (int(owner), set(map(int, edges.split(','))))
             for v, owner, edges in rows}

    def attract(event, safe):
        reached = set(event['target'])
        active = set(event['subgame'])
        assert reached <= safe <= active, event
        while True:
            added = {v for v in safe - reached
                     if (bool(graph[v][1] & active & reached)
                         if graph[v][0] == event['player']
                         else graph[v][1] & active <= reached)}
            if not added:
                return reached
            reached |= added

    for algorithm in ('zielonka-sym', 'lifting'):
        trace = directory / 'figure.jsonl'
        subprocess.run([str(binary), f'--algorithm={algorithm}',
                        f'--trace={trace}', str(game)],
                       capture_output=True, check=True, timeout=30)
        replay = Replay()
        witnessed = False
        for event in map(json.loads, trace.read_text().splitlines()):
            replay.apply(event)
            if (algorithm == 'zielonka-sym' and event['event'] == 'labels'
                    and event['player'] == 1 and event['path'] == []
                    and event['part'] == 'S' and event['vertex'] == 0):
                assert 0 in replay.tree(1)['S']
                witnessed = True
            if algorithm == 'lifting' and event['event'] == 'attractor':
                assert 'safe' in event, event
                actual = set(event['vertices'])
                assert attract(event, set(event['safe'])) == actual, event
                if actual == {0, 1, 3, 4}:
                    assert 2 in attract(event, set(event['subgame']))
                    witnessed = True
        assert witnessed, algorithm


def paper(binary, checker, directory):
    figure_state(binary, directory)
    for algorithm, expected in (
            ('zielonka', [10, 50, 218, 898, 3626, 14546]),
            ('zielonka-sym', [7, 23, 47, 83, 131, 191])):
        counts, _, trees = run(binary, checker, ROOT / 'tests/games/otj22-fig1.gm',
                                [f'--algorithm={algorithm}'], directory)
        assert vertices(trees[0], False) == set(range(10)) and trees[1] is None

        def nodes(tree):
            return 1 + sum(nodes(c) for c in tree['children'])
        assert nodes(trees[0]) == 5
        calls = []
        for k in range(2, 13):
            game = directory / 'hk.gm'
            game.write_text(hk(k))
            counts, stats, _ = run(binary, checker, game, [f'--algorithm={algorithm}'], directory)
            assert counts['call'] == int(re.search(r'calls=(\d+)', stats)[1])
            if k % 2 == 0:
                calls.append(counts['call'])
        assert calls == expected, (algorithm, calls, expected)
        if algorithm == 'zielonka':
            assert all(b > 4 * a for a, b in zip(calls, calls[1:]))
        else:
            assert all(calls[i + 2] - 2 * calls[i + 1] + calls[i] == 12
                       for i in range(1, len(calls) - 2))


def limits(binary, directory):
    game = ROOT / 'tests/games/otj22-fig1.gm'
    trace = directory / 'limit.jsonl'
    for algorithm in ('zielonka', 'zielonka-sym', 'lifting', 'lifting-sym'):
        for limit in (0, 1, 7):
            result = subprocess.run([str(binary), f'--algorithm={algorithm}',
                                     f'--trace={trace}', f'--trace-limit={limit}', str(game)],
                                    capture_output=True, timeout=30)
            assert result.returncode == 1 and b'limit exceeded' in result.stderr, result
            events = [json.loads(line) for line in trace.read_text().splitlines()]
            assert events[0]['event'] == 'header' and events[-1]['event'] == 'truncated'
            assert len(events) == limit + 2
            assert events[-1]['player'] in (0, 1) and isinstance(events[-1]['path'], list)
    result = subprocess.run(
        [str(binary), '--algorithm=zielonka-sym', f'--trace={trace}',
         str(ROOT / 'examples/cabp_swp.ehoa.pg')], capture_output=True, timeout=120)
    assert result.returncode == 1 and b'limit exceeded' in result.stderr
    with trace.open() as stream:
        assert json.loads(next(stream))['event'] == 'header'
        count, last = 1, None
        for last in stream:
            count += 1
    assert count == 1_000_002 and json.loads(last)['event'] == 'truncated'
    trace.unlink()
    result = subprocess.run(
        [str(binary), '--initial-shave=all', f'--trace={trace}',
         str(ROOT / 'tests/games/even_loop.pg')], capture_output=True, timeout=30)
    assert result.returncode == 0
    final = json.loads(trace.read_text().splitlines()[-1])
    assert final['initial_won'] == [[0], []]
    replay = Replay()
    replay.apply({'event': 'phase', 'step': 1, 'player': 0,
                  'phase': 'labels_reset'})
    replay.apply({'event': 'labels', 'step': 2, 'player': 0,
                  'vertex': 1, 'path': [0], 'part': 'S', 'h': 0})
    assert replay.tree(0) == {
        'h': 2, 'H': [], 'T': [], 'S': [], 'children': [
            {'h': 0, 'H': [], 'T': [], 'S': [1], 'children': []}]}
    replay.apply({'event': 'labels', 'step': 3, 'player': 0,
                  'vertex': 1, 'path': [], 'part': 'S', 'h': 2})
    assert replay.tree(0) == {'h': 2, 'H': [], 'T': [], 'S': [1], 'children': []}
    assert replay.materialized(0) is None
    for option in ('--trace=x', '--trace-limit=1'):
        result = subprocess.run([str(binary.parent / 'pg2pmdot'), option, str(game)],
                                capture_output=True, timeout=30)
        assert result.returncode == 2
    for option in ('--bits=2', '--format=leaves', '--algorithm=succinct'):
        result = subprocess.run([str(binary), f'--trace={trace}', option, str(game)],
                                capture_output=True, timeout=30)
        assert result.returncode == 2
    for option in ('--view=tree-relative', '--view=jurdzinski', '--player=even',
                   '--player=odd', '--labels=none', '--labels=counts',
                   '--max-set-items=1', '--priority-mode=original', '--no-verify'):
        command = [str(binary), option, str(game)]
        cold = subprocess.run(command, capture_output=True, timeout=30)
        observed = subprocess.run([*command[:-1], f'--trace={trace}', command[-1]],
                                  capture_output=True, timeout=30)
        assert (cold.returncode, cold.stdout, cold.stderr) == (observed.returncode, observed.stdout, observed.stderr)
    for option in ('--trace=', '--trace-limit=-1', '--trace-limit=x',
                   '--trace-limit=18446744073709551616'):
        result = subprocess.run([str(binary), option, str(game)], capture_output=True, timeout=30)
        assert result.returncode == 2
    copy = directory / 'input "quoted"\n.pg'
    copy.write_bytes(game.read_bytes())
    before = copy.read_bytes()
    result = subprocess.run([str(binary), f'--trace={copy}', str(copy)], capture_output=True)
    assert result.returncode == 1 and copy.read_bytes() == before
    result = subprocess.run([str(binary), '--trace=/dev/stdout', str(copy)], capture_output=True)
    assert result.returncode == 1 and not result.stdout
    result = subprocess.run([str(binary), '--trace=/dev/stderr', '--stats', str(copy)],
                            capture_output=True)
    assert result.returncode == 1 and b'Cannot open trace' in result.stderr
    log = directory / 'stderr.log'
    log.write_bytes(b'old stderr log\n')
    alias = directory / 'stderr-alias.log'
    alias.hardlink_to(log)
    with log.open('ab') as stderr:
        result = subprocess.run([str(binary), f'--trace={alias}', '--stats', str(copy)],
                                stdout=subprocess.PIPE, stderr=stderr)
    assert result.returncode == 1
    assert log.read_bytes().startswith(b'old stderr log\nCannot open trace')
    result = subprocess.run([str(binary), f'--trace={trace}', str(copy)], capture_output=True)
    assert result.returncode == 0
    header = json.loads(trace.read_text().splitlines()[0])
    assert str(copy) in header['options']
    malformed = directory / 'malformed.pg'
    malformed.write_text('not a parity game')
    failures = [([str(malformed)], 3, 'invalid game input'),
                (['--max-host-leaves=1', str(game)], 4, 'solver or verification failed'),
                ([str(directory / 'missing.pg')], 5, 'cannot open game input'),
                (['--tree-file=' + str(malformed), str(game)], 2, 'invalid host input'),
                (['--tree-file=' + str(directory / 'missing.tree'), str(game)],
                 5, 'cannot read host input')]
    for arguments, status, reason in failures:
        trace.write_text('{"event":"result","old":true}\n')
        result = subprocess.run([str(binary), f'--trace={trace}', *arguments],
                                capture_output=True, timeout=30)
        assert result.returncode == status, result
        events = [json.loads(line) for line in trace.read_text().splitlines()]
        assert events[0]['event'] == 'header'
        assert not any(event['event'] == 'result' for event in events)
        assert events[-1] == {'event': 'incomplete', 'step': len(events) - 1,
                              'reason': reason, 'exit_status': status}
    # macOS has no /dev/full; these write-failure checks are Linux-only.
    if Path('/dev/full').exists():
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([str(binary), f'--trace={trace}', str(game)],
                                    stdout=output, stderr=subprocess.PIPE, timeout=30)
        assert result.returncode == 5
        events = [json.loads(line) for line in trace.read_text().splitlines()]
        assert events[-2]['event'] == 'result'
        assert events[-1] == {'event': 'incomplete', 'step': events[-2]['step'] + 1,
                              'reason': 'cannot flush output streams', 'exit_status': 5}
        result = subprocess.run([str(binary), '--trace=/dev/full', str(game)], capture_output=True)
        assert result.returncode == 1
    result = subprocess.run([str(binary), f'--trace={directory / "missing/file"}', str(game)],
                            capture_output=True, timeout=30)
    assert result.returncode == 1


def examples(binary, checker, directory, backend_name):
    counting = binary.parent / 'trace_count_pg2adot'
    checked = 0
    for backend in BACKENDS:
        if backend.binary != 'pg2adot':
            continue
        if backend.name != backend_name:
            continue
        for game in (ROOT / 'examples' / name for name in EXAMPLES):
            name = f'{game.name}/{backend.name}'
            if (game.name, backend.name) in SLOW_PAIRS:
                print(f'delegated to slow suite: {name}', flush=True)
                continue
            print(f'counting observer invariance: {name}', flush=True)
            run(binary, checker, game, backend.options, directory, counting=counting)
            checked += 1
    assert checked > 0


def slow(binary, checker, directory, selected):
    game_name, backend_name = selected.split('/')
    assert (game_name, backend_name) in SLOW_PAIRS
    backend = next(b for b in BACKENDS if b.name == backend_name)
    game = next(path / game_name for path in (ROOT / 'examples', ROOT / 'tests/games')
                if (path / game_name).is_file())
    print(f'counting observer invariance: {selected}', flush=True)
    run(binary, checker, game, backend.options, directory,
        counting=binary.parent / 'trace_count_pg2adot')


def main():
    binary, checker = (Path(arg).resolve() for arg in sys.argv[1:3])
    mode = sys.argv[3] if len(sys.argv) > 3 else 'corpus'
    with tempfile.TemporaryDirectory(dir=binary.parent) as temp:
        directory = Path(temp)
        if mode == 'paper':
            paper(binary, checker, directory)
        elif mode == 'limits':
            limits(binary, directory)
        elif mode == 'examples':
            examples(binary, checker, directory, backend_name=sys.argv[4])
        elif mode == 'slow':
            slow(binary, checker, directory, sys.argv[4])
        else:
            backends = [b.options for b in BACKENDS if b.binary == 'pg2adot'
                        and (mode not in ('games', 'property') or b.name == sys.argv[4])]
            assert backends
            extra_hosts = (
                ('--tree-k=1',), ('--kary=1',), ('--kary=2',),
                (f'--tree-file={ROOT}/tests/trees/chain_three.tree',),
                ('--adaptive-t=incremental',), ('--adaptive-t', '--tree-k=1'),
                ('--no-verify',))
            if mode in ('hosts', 'host-errors'):
                if mode == 'host-errors':
                    extra_hosts = (('--max-host-leaves=1',),)
                backends = [
                    (f'--algorithm={algorithm}', *host, *pre, *iteration)
                    for algorithm in ('lifting', 'lifting-sym')
                    for host in extra_hosts
                    if algorithm == 'lifting' or not any('adaptive' in option for option in host)
                    for pre in ((), ('--initial-shave=loops', '--compress'))
                    for iteration in ((), ('--iter-shave',))]

            games = ([ROOT / 'tests/games' / name for name in
                      ('strahler_two.pg', 'split_winners.pg', 'iter_shave.pg')]
                     if mode in ('hosts', 'host-errors') else
                     [ROOT / 'tests/games' / name for name in REGRESSIONS] if mode == 'games' else [])
            rng = random.Random(685789)
            for case in range(24 if mode in ('property', 'corpus') else 0):
                game = directory / f'random-{case}.pg'
                game.write_text(game_text(random_game(rng, rng.randint(1, 12), 3, 3)))
                games.append(game)
            for case, text in (generated_games() if mode in ('property', 'corpus') else ()):
                game = directory / f'property-{case}.pg'
                game.write_text(text)
                games.append(game)
            incomplete = []
            for game in games:
                previous_failures = len(incomplete)
                for options in backends:
                    if mode == 'games' and (game.name, sys.argv[4]) in SLOW_PAIRS:
                        print(f'delegated to slow suite: {game.name}/{sys.argv[4]}', flush=True)
                        continue
                    effective = list(options)
                    print(f'running: {game.name} {" ".join(effective)}', flush=True)
                    try:
                        counting = (binary.parent / 'trace_count_pg2adot'
                                    if game.name == 'thesis_f4.pg' and
                                    any(opt.startswith('--algorithm=lifting') for opt in options)
                                    else None)
                        if counting is not None:
                            print('large lifting fixture: counting observer, real host budget', flush=True)
                        run(binary, checker, game, effective, directory, counting=counting)
                    except subprocess.TimeoutExpired as error:
                        phase = ('independent checker' if str(checker) in error.cmd else
                                 'traced' if any(arg.startswith('--trace=') for arg in error.cmd) else 'untraced')
                        incomplete.append((game.name, effective, phase))
                        (directory / 'trace.jsonl').unlink(missing_ok=True)
                        print(f'INCOMPLETE after 600 seconds: {game.name} {effective} ({phase})', flush=True)
                status = 'passed' if len(incomplete) == previous_failures else 'INCOMPLETE'
                print(f'{status}: {game.name}', flush=True)
            assert not incomplete, incomplete
    print(f'trace {mode}: passed')


if __name__ == '__main__':
    main()
