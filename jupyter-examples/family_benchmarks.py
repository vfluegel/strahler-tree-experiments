"""Bounded CLI experiments and offline displays for the CABP and ladder notebooks."""

import argparse
import bz2
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import gzip
import hashlib
from html import escape
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
from time import perf_counter

from fixed_pair_maps import grid_svg, legend_svg, pareto
from lexicographic_nester import BACKENDS, bounds, winner_digest

HERE = Path(__file__).resolve().parent
ORDERS = ('lexicographic', 'ratio', 'table')
FIXED_BACKENDS = ('AD-sym', 'AD-sym+iter', 'PM', 'PM+iter')
_lock = threading.Lock()


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def manifest(family):
    document = json.loads((HERE / family / 'manifest.json').read_text())
    for game in document['games']:
        assert sha256(HERE / family / game['file']) == game['sha256']
        expected = bounds(game['vertices'], game['normalized_maximum'])
        assert all(game[key] == value for key, value in expected.items())
        assert game['dense_ids'], 'Winner bitsets require IDs 0..N-1'
    return document


def bit_vertices(bits):
    for index, byte in enumerate(bits):
        while byte:
            low = byte & -byte
            yield 8 * index + low.bit_length() - 1
            byte ^= low


def add_vertices(bits, vertices, count):
    for vertex in vertices:
        if not 0 <= vertex < count:
            raise ValueError('Vertex outside the fixture ID range')
        bits[vertex // 8] |= 1 << (vertex % 8)


def check_partition(won, unresolved, count, expected=None):
    """Check every original ID, including wins from all shaving rounds."""
    for index, (even, odd, left) in enumerate(zip(*won, unresolved)):
        mask = (1 << (count % 8)) - 1 if index == len(unresolved) - 1 and count % 8 else 255
        if even & odd or (even | odd) & left or (even | odd | left) != mask:
            raise ValueError('Certified wins and unresolved IDs do not partition the fixture')
        if expected is not None and (even & ~expected[0][index] or odd & ~expected[1][index]):
            raise ValueError('Certified winner disagrees with the independent reference')


def save_winners(path, won, game):
    header = dict(vertices=game['vertices'], fixture_sha256=game['sha256'],
                  layout='two byte arrays, Even then Odd, least significant bit first; IDs 0..N-1')
    raw = json.dumps(header, sort_keys=True).encode() + b'\n' + b''.join(won)
    path = Path(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.winners-', dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(gzip.compress(raw, mtime=0))
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def publish_winners(temporary, directory):
    """Immutable reference names let different-budget recordings share safely."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    digest = sha256(temporary)
    destination = directory / (digest + '.winners.gz')
    if destination.exists():
        if sha256(destination) != digest:
            raise ValueError('Existing content-addressed reference is corrupt')
    else:
        Path(temporary).replace(destination)
    return destination, digest


def load_winners(path, game):
    header, raw = gzip.decompress(Path(path).read_bytes()).split(b'\n', 1)
    header = json.loads(header)
    size = (game['vertices'] + 7) // 8
    if (header['vertices'] != game['vertices'] or
            header['fixture_sha256'] != game['sha256'] or len(raw) != 2 * size):
        raise ValueError('Reference belongs to a different fixture')
    return raw[:size], raw[size:]


def small_shape(node, remaining=None):
    """Keep occupied shape and counts; omit all individual vertex IDs."""
    if node is None:
        return None
    if remaining is None:
        remaining = [64]
    remaining[0] -= 1
    if remaining[0] < 0:
        raise ValueError('Certificate has more than 64 nodes')
    return dict(h=node['h'], H=len(node['H']), T=len(node['T']), S=len(node['S']),
                children=[small_shape(child, remaining) for child in node['children']])


def occupied_vertices(node):
    if node is not None:
        yield from node.get('vertices') or ()
        for child in node.get('children', ()):
            yield from occupied_vertices(child.get('node', child))


def reduce_report(path, backend, game, reference=None, save_reference=None):
    """Parse selected JSON subtrees; never materialize the original game object."""
    import ijson

    def items(prefix):
        with Path(path).open('rb') as stream:
            yield from ijson.items(stream, prefix, use_float=True)

    count = game['vertices']
    size = (count + 7) // 8
    won, unresolved = [bytearray(size), bytearray(size)], bytearray(size)
    rounds, parameters, complete = 0, None, False
    shape, metrics = None, None
    if backend.startswith('PM'):
        for round_ in items('rounds.item'):
            rounds += 1
            complete = round_['complete']
            unresolved[:] = bytes(size)
            add_vertices(unresolved, round_['unresolved'], count)
            parameters = [p['parameters'] for p in round_['trees']]
            for player, result in enumerate(round_['trees']):
                # PM trees contain only occupied positions; the game was skipped.
                add_vertices(won[player], occupied_vertices(result['tree']), count)
                add_vertices(won[player], result['shaved'], count)
                add_vertices(won[player], result['attracted'], count)
    else:
        complete = next(items('complete'))
        parameters = next(items('parameters'))
        for prefix in ('regions.item', 'initial_won.item', 'shaves.item.attracted.item'):
            for index, vertices in enumerate(items(prefix)):
                add_vertices(won[index % 2], vertices, count)
        add_vertices(unresolved, items('unresolved.item'), count)
        rounds = 1 + sum(1 for _ in items('shaves.item.attracted'))
        metrics = next(items('metrics'))
        if save_reference is not None:
            try:
                shape = [small_shape(tree) for tree in items('trees.item')]
            except ValueError:
                shape = None
    expected = load_winners(reference, game) if reference is not None else None
    check_partition(won, unresolved, count, expected)
    left = sum(byte.bit_count() for byte in unresolved)
    if complete != (left == 0):
        raise ValueError('Completeness disagrees with the unresolved vertex set')
    if save_reference is not None:
        if not complete:
            raise ValueError('Independent reference returned a partial result')
        save_winners(save_reference, won, game)
    k_name = 'k' if backend.startswith('PM') else 'K'
    pair = ([max(p[k_name] for p in parameters), parameters[0]['t']]
            if all(k_name in p for p in parameters) else None)
    return dict(outcome='solved' if complete else 'unsolved', rounds=rounds,
                final_pair=pair, parameters=parameters, unresolved=left,
                won=[sum(byte.bit_count() for byte in side) for side in won],
                winner_sha256=[winner_digest(bit_vertices(side)) for side in won] if complete else None,
                native_verification=True, independent_winner_check=expected is not None,
                reference_shape=shape, metrics=metrics)


def worker(request):
    """The worker and its native child share the declared memory/time boundary."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if sys.platform.startswith('linux'):
        cap = request['memory_gib'] * 1024**3
        resource.setrlimit(resource.RLIMIT_AS, (cap, cap))
    # Bound temporary JSON reports as well as address space.
    resource.setrlimit(resource.RLIMIT_FSIZE, (2 * 1024**3, 2 * 1024**3))
    game, backend = request['game'], request['backend']
    pm = backend.startswith('PM')
    binary = Path(request['build_dir']) / ('pg2pmdot' if pm else 'pg2adot')
    command = [str(binary), '--format=json', '--stats']
    command += ['--algorithm=' + ('strahler' if pm else 'zielonka' if backend == 'reference'
                                  else 'lifting-sym' if backend.startswith('AD-sym') else 'lifting')]
    if not pm and backend != 'reference':
        command += ['--max-host-leaves=' + str(2 * sys.maxsize + 1)]
    if request.get('order'):
        command += ['--search=' + request['order']]
    if request.get('pair'):
        k, t = request['pair']
        command += [f'--k={k}', f'--t={t}'] if pm else [f'--tree-k={k}', f'--tree-t={t}']
    if backend.endswith('+iter'):
        command += ['--iter-shave']
        if pm and request.get('pair'):
            command += ['--repeat-fixed']
    env = dict(os.environ, STRAHLER_PM_BACKEND='simd', STRAHLER_BACKEND_REPORT='1')
    cache = Path(request['cache'])
    cache.mkdir(parents=True, exist_ok=True)
    stage_path = Path(request['stage_path'])
    stage_path.write_text('input')
    with tempfile.TemporaryDirectory(prefix='family-', dir=cache) as temporary:
        plain, report = Path(temporary) / 'game.pg', Path(temporary) / 'report.json'
        opener = bz2.open if request['path'].endswith('.bz2') else open
        with opener(request['path'], 'rb') as source, plain.open('wb') as destination:
            shutil.copyfileobj(source, destination)
        stage_path.write_text('native solve and JSON output')
        started = perf_counter()
        with report.open('wb') as output:
            result = subprocess.run([*command, str(plain)], stdout=output,
                                    stderr=subprocess.PIPE, env=env)
        native_seconds = perf_counter() - started
        detail = result.stderr.decode(errors='replace')
        if result.returncode:
            output_limit = (result.returncode == -signal.SIGXFSZ or
                            'file too large' in detail.lower())
            memory = 'allocat' in detail.lower() or 'out of memory' in detail.lower()
            if result.returncode < 0:
                memory = False  # A crash is a defect, never a memory-frontier claim.
            print(json.dumps(dict(outcome='output limit' if output_limit else 'memory' if memory else 'error',
                                  returncode=result.returncode, detail=detail[-400:])))
            return
        stage_path.write_text('JSON reduction and winner validation')
        summary = reduce_report(report, backend, game, request.get('reference'),
                                request.get('save_reference'))
        summary.update(native_cli_seconds=round(native_seconds, 3),
                       report_bytes=report.stat().st_size,
                       peak_rss_mib=round(max(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                                              resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss) /
                                          (1024 if sys.platform != 'darwin' else 1024**2), 2),
                       dispatch={name: detail.count(f'PM strahler backend {name}')
                                 for name in ('simd', 'scalar')} if pm else None)
        print(json.dumps(summary))


def measure(request, seconds):
    """Kill the entire process group on a deadline, including the native child."""
    started = perf_counter()
    with tempfile.TemporaryDirectory(prefix='stage-', dir=request['cache']) as temporary:
        stage_path = Path(temporary) / 'stage'
        request = dict(request, cache=temporary, stage_path=str(stage_path))
        process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), 'worker'],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, start_new_session=True)
        try:
            output, error = process.communicate(json.dumps(request), timeout=seconds)
            if process.returncode == 0:
                result = json.loads(output)
            else:
                output_limit = process.returncode == -signal.SIGXFSZ or 'File too large' in error
                result = dict(outcome='output limit' if output_limit else 'memory' if 'MemoryError' in error else 'error',
                              returncode=process.returncode, detail=error[-400:])
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
            result = dict(outcome='timeout')
        result['stage'] = stage_path.read_text() if stage_path.exists() else 'worker startup'
    if result['outcome'] == 'error':
        raise RuntimeError(result.get('detail', 'Unexpected native failure'))
    return dict(seconds=round(perf_counter() - started, 3), timeout_seconds=seconds,
                memory_gib=request['memory_gib'], **result)


def measured_grid(measured, k_max, t_max, hints=()):
    """Prune only cells that cannot be new coordinatewise Pareto minima."""
    rows = {}
    for pair in [(1, 0), *hints]:
        k, t = pair
        if 1 <= k <= k_max and 0 <= t <= t_max and pair not in rows:
            rows[pair] = measured(k, t)
    while True:
        solved = [p for p, r in rows.items() if r['outcome'] == 'solved']
        pending = [(k, t) for k in range(1, k_max + 1) for t in range(t_max + 1)
                   if (k, t) not in rows and not any(a <= k and b <= t for a, b in solved)]
        if not pending:
            return
        pair = min(pending, key=lambda p: (not any(p[0] <= a and p[1] <= b for a, b in solved),
                                           p[0] * (p[1] + 1), p))
        rows[pair] = measured(*pair)


def run(family, build_dir, seconds=60, memory_gib=2, jobs=2, output=None):
    """Resume independent order/method/relation frontiers and declared fixed maps."""
    import ijson

    if seconds <= 0 or memory_gib < 1 or jobs < 1:
        raise ValueError('Use positive time, memory and worker limits')
    info = manifest(family)
    directory = HERE / family
    output = Path(output) if output else directory / f'results-{seconds:g}s.json'
    cache = (HERE.parent / '.cache/family-benchmarks').resolve()
    cache.mkdir(parents=True, exist_ok=True)
    build_dir = Path(build_dir).resolve()
    metadata = dict(family=family, timeout_seconds=seconds, memory_gib=memory_gib, jobs=jobs,
                    reference_timeout_seconds=seconds, memory_cap_enforced=sys.platform.startswith('linux'),
                    report_size_cap_gib=2, python_version=platform.python_version(),
                    source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                    worker_sha256=sha256(__file__), ijson_version=ijson.__version__,
                    machine=f'{platform.system()} {platform.machine()}, {os.cpu_count()} CPUs',
                    fixture_sha256={g['id']: g['sha256'] for g in info['games']},
                    binary_sha256={name: sha256(build_dir / name) for name in ('pg2adot', 'pg2pmdot')},
                    version=subprocess.check_output([str(build_dir / 'pg2adot'), '--version'], text=True).strip(),
                    pm_algorithm='strahler', requested_pm_backend='simd',
                    timing='End-to-end worker: decompress, parse, native solve/verification, JSON output/reduction and winner checks; reference measured separately.',
                    orders=list(ORDERS), backends=list(BACKENDS), fixed_backends=list(FIXED_BACKENDS))
    if output.exists():
        document = json.loads(output.read_text())
        for key in ('timeout_seconds', 'memory_gib', 'jobs', 'reference_timeout_seconds',
                    'worker_sha256', 'fixture_sha256', 'binary_sha256', 'ijson_version', 'python_version'):
            assert document[key] == metadata[key], f'Resume mismatch: {key}'
    else:
        document = dict(metadata, recorded_utc=datetime.now(timezone.utc).isoformat(timespec='seconds'),
                        complete=False, references=[], rows=[], fixed_rows=[], followup_rows=[])

    def checkpoint():
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_suffix('.tmp')
        temporary.write_text(json.dumps(document, indent=1) + '\n')
        temporary.replace(output)

    references = {r['game']: r for r in document['references']}
    stopped_work = threading.Event()
    reference_dir = directory / 'references'
    reference_dir.mkdir(exist_ok=True)
    for game in info['games']:
        if game['id'] in references:
            reference = references[game['id']]
            if reference['outcome'] == 'solved':
                saved = directory / reference['bitset_file']
                if sha256(saved) != reference['bitset_sha256']:
                    raise ValueError('Checkpoint reference checksum mismatch')
                load_winners(saved, game)
            continue
        # This attempt owns only its temporary path. A timeout cannot remove
        # another recording's successful reference, even with a new --output.
        with tempfile.TemporaryDirectory(prefix='reference-', dir=cache) as temporary:
            pending = Path(temporary) / 'winners.gz'
            request = dict(game=game, path=str((directory / game['file']).resolve()),
                           backend='reference', build_dir=str(build_dir), memory_gib=memory_gib,
                           cache=str(cache), save_reference=str(pending))
            result = dict(game=game['id'], **measure(request, seconds))
            if result['outcome'] == 'solved':
                saved, digest = publish_winners(pending, reference_dir)
                result.update(bitset_file=str(saved.relative_to(directory)), bitset_sha256=digest)
        references[game['id']] = result
        document['references'].append(result)
        checkpoint()
        print(f"{game['id']} reference: {result['outcome']}, {result['seconds']:.2f} s", flush=True)

    rows = {(r['game'], r['backend'], r['order']): r for r in document['rows']}
    fixed = {(r['game'], r['backend'], r['k'], r['t']): r for r in document['fixed_rows']}
    followups = {(r['game'], r['backend'], r['k'], r['t']): r for r in document.get('followup_rows', [])}

    def measured(game, backend, order=None, pair=None, followup=False):
        if stopped_work.is_set():
            raise RuntimeError('Collection stopped after another worker failed')
        key = (game['id'], backend, order) if pair is None else (game['id'], backend, *pair)
        target = followups if followup else rows if pair is None else fixed
        if key not in target:
            reference = references[game['id']]
            request = dict(game=game, path=str((directory / game['file']).resolve()), backend=backend,
                           order=order, pair=pair, build_dir=str(build_dir), memory_gib=memory_gib,
                           cache=str(cache), reference=str((directory / reference['bitset_file']).resolve())
                           if reference['outcome'] == 'solved' else None)
            result = dict(game=game['id'], backend=backend, order=order, **measure(request, seconds))
            if pair is not None:
                result.update(k=pair[0], t=pair[1])
            with _lock:
                target[key] = result
                document.setdefault('followup_rows' if followup else 'rows' if pair is None else 'fixed_rows', []).append(result)
                checkpoint()
            print(f"{game['id']} {backend} {order or pair}: {result['outcome']}, {result['seconds']:.2f} s", flush=True)
        return target[key]

    def frontier(series, backend, order):
        stopped = None
        for game in sorted((g for g in info['games'] if g['series'] == series), key=lambda g: g['parameter']):
            if stopped_work.is_set():
                return
            key = game['id'], backend, order
            if stopped is not None:
                if key not in rows:
                    result = dict(game=game['id'], backend=backend, order=order,
                                  outcome='not measured', frontier=stopped,
                                  timeout_seconds=seconds, memory_gib=memory_gib)
                    with _lock:
                        rows[key] = result
                        document['rows'].append(result)
                        checkpoint()
                continue
            row = measured(game, backend, order)
            if row['outcome'] != 'solved':
                stopped = game['id']

    def finish(futures):
        try:
            for future in as_completed(futures):
                future.result()
        except BaseException:
            stopped_work.set()
            for future in futures:
                future.cancel()
            raise

    with ThreadPoolExecutor(jobs) as pool:
        futures = [pool.submit(frontier, series, backend, order)
                   for order in ORDERS for series in sorted({g['series'] for g in info['games']})
                   for backend in BACKENDS]
        finish(futures)
        futures = []
        for game in info['games']:
            if not game.get('fixed_map'):
                continue
            for backend in FIXED_BACKENDS:
                k_max = max(game['PM_k_max'] if backend.startswith('PM') else game['AD_k_max'])
                t_max = game['fixed_t_max']
                futures.append(pool.submit(measured_grid,
                                           lambda k, t, g=game, b=backend: measured(g, b, pair=(k, t)),
                                           k_max, t_max))
        finish(futures)
        # A small declared box may contain no plain-PM solve. A separate
        # targeted rerun supplies fixed-host evidence without claiming minima.
        for game in info['games']:
            if not game.get('fixed_map') or any(r['game'] == game['id'] and r['backend'] == 'PM' and
                                               r['outcome'] == 'solved' for r in fixed.values()):
                continue
            candidates = [r for r in rows.values() if (r['game'], r['backend'], r['outcome']) ==
                          (game['id'], 'PM', 'solved')]
            if candidates:
                candidate = min(candidates, key=lambda r: r['seconds'])
                measured(game, 'PM', pair=tuple(candidate['final_pair']), followup=True)
        document['followup_policy'] = 'Freshly repeat the fastest successful plain-PM adaptive pair for each mapped case without a fixed solve; these probes do not establish Pareto minimality.'
    document['complete'] = True
    checkpoint()
    return document


def validate_fixed_row(row, game, rectangle=True):
    assert game.get('fixed_map') and row['backend'] in FIXED_BACKENDS
    pm = row['backend'].startswith('PM')
    caps = game['PM_k_max'] if pm else game['AD_k_max']
    k, t = row['k'], row['t']
    assert 1 <= k <= max(caps) and 0 <= t <= game['t_max']
    assert not rectangle or t <= game['fixed_t_max']
    assert row['outcome'] in ('solved', 'unsolved', 'timeout', 'memory', 'output limit')
    if row['outcome'] in ('solved', 'unsolved'):
        assert row['final_pair'] == [k, t] and len(row['parameters']) == 2
        for player, parameters in enumerate(row['parameters']):
            assert parameters['t'] == t
            assert parameters['k' if pm else 'K'] == min(k, caps[player])
            assert parameters['depth' if pm else 'H'] == game['PM_depth' if pm else 'AD_height'][player]


def recorded(family, path=None):
    info = manifest(family)
    directory = HERE / family
    document = json.loads((Path(path) if path else directory / 'results-60s.json').read_text())
    assert document['complete'] and document['pm_algorithm'] == 'strahler'
    assert document['fixture_sha256'] == {g['id']: g['sha256'] for g in info['games']}
    expected = {(g['id'], b, o) for g in info['games'] for b in BACKENDS for o in ORDERS}
    assert {(r['game'], r['backend'], r['order']) for r in document['rows']} == expected
    assert len(document['rows']) == len(expected)
    assert len(document['references']) == len(info['games'])
    games_by_id = {g['id']: g for g in info['games']}
    for group, rectangle in ((document['fixed_rows'], True), (document.get('followup_rows', []), False)):
        keys = [(r['game'], r['backend'], r['k'], r['t']) for r in group]
        assert len(set(keys)) == len(keys)
        for row in group:
            validate_fixed_row(row, games_by_id[row['game']], rectangle)
    by_key = {(r['game'], r['backend'], r['order']): r for r in document['rows']}
    for order in ORDERS:
        for backend in BACKENDS:
            for series in {g['series'] for g in info['games']}:
                stopped = None
                for game in sorted((g for g in info['games'] if g['series'] == series), key=lambda g: g['parameter']):
                    row = by_key[game['id'], backend, order]
                    if stopped is not None:
                        assert row['outcome'] == 'not measured' and row['frontier'] == stopped
                    else:
                        assert row['outcome'] in ('solved', 'unsolved', 'timeout', 'memory', 'output limit')
                        if row['outcome'] != 'solved':
                            stopped = game['id']
    for game in info['games']:
        references = [r for r in document['references'] if r['game'] == game['id']]
        assert len(references) == 1
        reference = references[0]
        if reference['outcome'] == 'solved':
            assert sha256(directory / reference['bitset_file']) == reference['bitset_sha256']
            won = load_winners(directory / reference['bitset_file'], game)
            check_partition(won, bytes((game['vertices'] + 7) // 8), game['vertices'])
            assert [sum(byte.bit_count() for byte in side) for side in won] == reference['won']
            assert [winner_digest(bit_vertices(side)) for side in won] == reference['winner_sha256']
        all_rows = [*document['rows'], *document['fixed_rows'], *document.get('followup_rows', []), reference]
        complete = [r for r in all_rows
                    if r.get('game') == game['id'] and r['outcome'] == 'solved']
        assert len({tuple(r['winner_sha256']) for r in complete}) <= 1
        for row in [r for r in all_rows
                    if r['game'] == game['id']]:
            assert row['timeout_seconds'] == document['timeout_seconds']
            assert row['memory_gib'] == document['memory_gib']
            if row['outcome'] in ('solved', 'unsolved'):
                assert row['native_verification']
                if row is not reference:
                    assert row['independent_winner_check'] == (reference['outcome'] == 'solved')
                assert sum(row['won']) + row['unresolved'] == game['vertices']
                if row.get('backend', '').startswith('PM'):
                    assert sum(row['dispatch'].values()) > 0
        for backend in FIXED_BACKENDS if game.get('fixed_map') else ():
            cells = [r for r in document['fixed_rows'] if (r['game'], r['backend']) == (game['id'], backend)]
            k_max = max(game['PM_k_max'] if backend.startswith('PM') else game['AD_k_max'])
            measured_pairs = {(r['k'], r['t']) for r in cells}
            assert len(measured_pairs) == len(cells)
            solved = [p for p in measured_pairs if next(r for r in cells if (r['k'], r['t']) == p)['outcome'] == 'solved']
            assert all((k, t) in measured_pairs or any(a <= k and b <= t for a, b in solved)
                       for k in range(1, k_max + 1) for t in range(game['fixed_t_max'] + 1))
    return info, document


def result_text(row):
    if row['outcome'] == 'solved':
        return f"{tuple(row['final_pair'])}, {row['seconds']:.2f} s"
    if row['outcome'] == 'not measured':
        return 'beyond measured frontier'
    if row['outcome'] == 'memory':
        return f"{row['memory_gib']} GiB limit"
    if row['outcome'] == 'output limit':
        return '2 GiB report limit'
    if row['outcome'] == 'unsolved':
        return f"partial, {row['seconds']:.2f} s"
    return f"{row['outcome']} ({row['timeout_seconds']:g} s cap)"


def fixed_maps(info, document):
    grids, summary = [], []
    for game in info['games']:
        if not game.get('fixed_map'):
            continue
        for backend in FIXED_BACKENDS:
            rows = [r for r in document['fixed_rows'] if (r['game'], r['backend']) == (game['id'], backend)]
            k_max = max(game['PM_k_max'] if backend.startswith('PM') else game['AD_k_max'])
            grids.append('<figure style="margin:0"><figcaption>' +
                         escape(game['label']) + '</figcaption>' +
                         grid_svg(backend, rows, k_max, game['fixed_t_max']) + '</figure>')
            candidates, confirmed, _ = pareto(rows)
            summary.append([game['label'], backend, confirmed, candidates,
                            [(r['k'], r['t']) for r in rows if r['outcome'] in ('timeout', 'memory', 'output limit')]])
    return ('<div style="display:flex;flex-wrap:wrap;gap:12px">' + ''.join(grids) + '</div>' + legend_svg()), summary


def shape_html(shape):
    """Render a recorded answer's occupied branching and H/T/S counts."""
    if shape is None:
        return '<p>No small reference certificate was recorded.</p>'
    def node(value):
        if value is None:
            return '<li>empty</li>'
        label = escape(f"bound {value['h']}: H={value['H']}, T={value['T']}, S={value['S']}")
        return '<li>' + label + ('<ul>' + ''.join(node(c) for c in value['children']) + '</ul>' if value['children'] else '') + '</li>'
    return '<div style="display:flex;gap:30px">' + ''.join(
        f'<div><b>{name}</b><ul>{node(tree)}</ul></div>' for name, tree in zip(('Even', 'Odd'), shape)) + '</div>'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('worker', 'run', 'check'))
    parser.add_argument('--family', choices=('cabp', 'ladder'))
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--memory-gib', type=int, default=2)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.command == 'worker':
        worker(json.load(sys.stdin))
    else:
        if args.family is None:
            parser.error('--family is required')
        if args.command == 'run':
            run(args.family, args.build_dir, args.seconds, args.memory_gib, args.jobs, args.output)
        else:
            recorded(args.family, args.output)


if __name__ == '__main__':
    main()
