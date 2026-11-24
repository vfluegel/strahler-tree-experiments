"""Measure fixed-host certificates and compare observed Nester Pareto minima."""

import argparse
import bz2
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from html import escape
import json
import hashlib
import os
import platform
from functools import lru_cache
from math import cos, pi, sin
from pathlib import Path
import resource
import signal
import subprocess
import sys
import threading
from time import perf_counter

from lexicographic_nester import DATA, manifest, winner_digest, tree_vertices

RESULTS = DATA / 'fixed-pair-maps-600s.json'
PM_RESULTS = DATA / 'fixed-pair-pm-600s.json'
AD_BACKENDS = ('AD-sym', 'AD-sym+iter')
PM_BACKENDS = ('PM', 'PM+iter')
BACKENDS = (*AD_BACKENDS, *PM_BACKENDS)
MAPPED_GAMES = (4, 5)
GREEN, RED = '#0ca30c', '#d03b3b'
CELL, GAP = 24, 2
_write_lock = threading.Lock()


def winning_sets(document, backend):
    won = [set(), set()]
    if backend.startswith('PM'):
        for round_ in document['rounds']:
            for player, result in enumerate(round_['trees']):
                won[player].update(tree_vertices(result['tree']))
                won[player].update(result['shaved'])
                won[player].update(result['attracted'])
    else:
        for player in range(2):
            won[player].update(document['regions'][player])
            won[player].update(document['initial_won'][player])
            for shave in document['shaves']:
                won[player].update(shave['attracted'][player])
    return won


def native_provenance():
    import strahler
    from strahler import _native
    return dict(version=strahler.version(),
                library_sha256=hashlib.sha256(Path(_native.library()._name).read_bytes()).hexdigest(),
                worker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                recorded_utc=datetime.now(timezone.utc).isoformat(timespec='seconds'),
                machine=f'{platform.system()} {platform.machine()}, {os.cpu_count()} CPUs',
                pm_algorithm='strahler', requested_pm_backend='simd',
                verification=True)


@lru_cache(maxsize=32)
def reference_winners(path):
    from strahler import _native
    path = Path(path)
    raw = bz2.decompress(path.read_bytes()) if path.suffix == '.bz2' else path.read_bytes()
    handle = _native.call('game_parse', raw, len(raw), handle=True)
    try:
        flags = _native.options(None, {}, 'zielonka')
        document = json.loads(_native.call('decompose', handle, flags, len(flags),
                                           _native.Callback(), None))
        assert document['complete']
        return winning_sets(document, 'AD')
    finally:
        _native.library().strahler_game_free(handle)


def worker(request):
    """One capped native solve, shared by the fixed maps and family notebooks."""
    from strahler import _native
    import strahler

    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if sys.platform.startswith('linux'):
        limit = request['memory_gib'] * 1024**3
        resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    path = Path(request['path'])
    raw = bz2.decompress(path.read_bytes()) if path.suffix == '.bz2' else path.read_bytes()
    handle = _native.call('game_parse', raw, len(raw), handle=True)
    del raw
    backend, options = request['backend'], dict(request['options'])
    iterate = backend.endswith('+iter')
    options['iter_shave'] = iterate
    pm = backend.startswith('PM')
    if pm:
        os.environ['STRAHLER_PM_BACKEND'] = 'simd'
        os.environ['STRAHLER_BACKEND_REPORT'] = '1'
        options['algorithm'] = 'strahler'
        if 'search' not in options and iterate:
            options['repeat_fixed'] = True
        flags = _native.options(None, options)
    else:
        if 'k' in options:
            options['tree_k'] = options.pop('k')
            options['tree_t'] = options.pop('t')
        options['max_host_leaves'] = 2 * sys.maxsize + 1
        flags = _native.options(None, options, 'lifting-sym' if backend.startswith('AD-sym') else 'lifting')
    try:
        if pm:
            document = json.loads(_native.call('progress_measures', handle, flags, len(flags)))
            final = document['rounds'][-1]
            parameters = [p['parameters'] for p in final['trees']]
            rounds = len(document['rounds'])
        else:
            document = json.loads(_native.call('decompose', handle, flags, len(flags),
                                               _native.Callback(), None))
            final, parameters = document, document['parameters']
            rounds = len(document['shaves']) + 1
    finally:
        _native.library().strahler_game_free(handle)
    won = winning_sets(document, backend)
    assert not won[0] & won[1]
    assert sum(map(len, won)) + len(final['unresolved']) == request['vertices']
    if 'k' in request['options']:
        k, t = (request['options'][key] for key in ('k', 't'))
        assert all(p['t'] == t for p in parameters)
        if pm:
            assert all(p['k'] == min(k, p['depth'] + 1) for p in parameters)
        else:
            cap = (request['vertices'].bit_length() - 1) + 1
            assert all(p['K'] == min(k, p['H'], cap) for p in parameters)
    usage = resource.getrusage(resource.RUSAGE_SELF)
    print(json.dumps(dict(
        outcome='solved' if final['complete'] else 'unsolved',
        rounds=rounds, won=list(map(len, won)), unresolved=len(final['unresolved']),
        parameters=parameters, won_vertices=[sorted(w) for w in won],
        winner_sha256=[winner_digest(w) for w in won] if final['complete'] else None,
        peak_rss_mib=round(usage.ru_maxrss / (1024 if sys.platform != 'darwin' else 1024**2), 2))), flush=True)


def measure_game(path, vertices, backend, options, seconds, memory_gib):
    """Native certificates plus independent original-game winner checks."""
    path = str(Path(path).resolve())
    truth = reference_winners(path)
    request = dict(path=path, vertices=vertices, backend=backend,
                   options=options, memory_gib=memory_gib)
    started = perf_counter()
    try:
        process = subprocess.run([sys.executable, str(Path(__file__).resolve()), 'worker'],
                                 input=json.dumps(request), text=True, capture_output=True,
                                 timeout=seconds)
        if process.returncode == 0:
            result = json.loads(process.stdout)
            won = [set(w) for w in result.pop('won_vertices')]
            assert all(w <= expected for w, expected in zip(won, truth))
            if result['outcome'] == 'solved':
                assert won == truth
            result['independent_winner_check'] = True
            result['dispatch'] = {name: process.stderr.count(f'PM strahler backend {name}')
                                  for name in ('simd', 'scalar')} if backend.startswith('PM') else None
        else:
            detail = process.stderr.strip().splitlines()[-1][:300] if process.stderr.strip() else ''
            memory = 'MemoryError' in detail or 'MemoryAllocationError' in detail
            result = dict(outcome='memory' if memory else 'error', detail=detail,
                          returncode=process.returncode)
    except subprocess.TimeoutExpired:
        result = dict(outcome='timeout')
    return dict(seconds=round(perf_counter() - started, 3),
                timeout_seconds=seconds, memory_gib=memory_gib, **result)


def measure(n, backend, k, t, seconds, memory_gib):
    game = next(g for g in manifest() if g['n'] == n)
    return dict(n=n, backend=backend, k=k, t=t,
                **measure_game(DATA / game['file'], game['vertices'], backend,
                               dict(k=k, t=t), seconds, memory_gib))


def caps(n, backend='AD-sym'):
    game = next(g for g in manifest() if g['n'] == n)
    return max(game['PM_k_max'] if backend.startswith('PM') else game['AD_k_max']), game['t_max']


def minima_walk(measured, n, backend):
    """Observe candidates and predecessors; prune only already dominated cells."""
    k_max, t_max = caps(n, backend)
    k_max, t_max = min(k_max, 6), min(t_max, 6)
    rows = {}
    hints = [(4, 4)] if n == 4 else [(4, 6), (5, 5)]
    for k, t in hints:
        if k <= k_max and t <= t_max:
            rows[k, t] = measured(n, backend, k, t)
    while True:
        solved = [pair for pair, row in rows.items() if row['outcome'] == 'solved']
        pending = [(k, t) for k in range(1, k_max + 1) for t in range(t_max + 1)
                   if (k, t) not in rows and
                   not any(a <= k and b <= t for a, b in solved)]
        if not pending:
            return
        # Check all strict predecessors of observed solves first. Neither a
        # timeout nor increasing k provides a host-inclusion inference.
        def priority(pair):
            k, t = pair
            predecessor = any(k <= a and t <= b for a, b in solved)
            return not predecessor, k * (t + 1), k, t
        k, t = min(pending, key=priority)
        rows[k, t] = measured(n, backend, k, t)


def exhaustive(measured, n, backend):
    k_max, t_max = caps(n, backend)
    for k in range(1, k_max + 1):
        for t in range(t_max + 1):
            measured(n, backend, k, t)


def run(walk, games, seconds, memory_gib, jobs, output, backends=PM_BACKENDS):
    """Walk each map, appending rows to a JSON Lines checkpoint that later runs resume."""
    output = Path(output)
    rows = {}
    output.parent.mkdir(parents=True, exist_ok=True)
    metadata_path = output.with_suffix('.metadata.json')
    metadata = dict(native_provenance(), backends=list(backends), games=list(games),
                    timeout_seconds=seconds, memory_gib=memory_gib, jobs=jobs,
                    rectangle=dict(k_max=6, t_max=6),
                    fixtures={g['file']: g['sha256'] for g in manifest()},
                    fixed_pm_policy={b: 'repeat-until-stalled' if b.endswith('+iter') else 'one round' for b in backends})
    if metadata_path.exists():
        previous = json.loads(metadata_path.read_text())
        for key in ('library_sha256', 'worker_sha256', 'backends', 'games', 'timeout_seconds', 'memory_gib'):
            assert metadata[key] == previous[key], f'Resume mismatch: {key}'
    else:
        metadata_path.write_text(json.dumps(metadata, indent=2)+'\n')
    if output.exists():
        rows = {(r['n'], r['backend'], r['k'], r['t']): r
                for r in map(json.loads, output.read_text().splitlines())}

    def measured(n, backend, k, t):
        key = n, backend, k, t
        if key not in rows:
            row = measure(*key, seconds, memory_gib)
            with _write_lock:
                rows[key] = row
                with output.open('a', encoding='utf-8') as stream:
                    stream.write(json.dumps(row) + '\n')
            if row['outcome'] == 'error':
                raise RuntimeError(row.get('detail', 'unexpected solver error'))
            print(f"n={n} {backend} (k,t)=({k},{t}): {row['outcome']}, "
                  f"{row['seconds']:.1f} s", flush=True)
        return rows[key]

    with ThreadPoolExecutor(jobs) as pool:
        futures = [pool.submit(walk, measured, n, backend)
                   for n in games for backend in reversed(backends)]
        for future in futures:
            future.result()


def backend_is_ad(backend):
    return backend.startswith('AD')


def monotonicity_violations(rows):
    """Diagnostics only for the proved fixed-k AD inclusion as t increases."""
    violations = []
    for key in {(r['n'], r['backend']) for r in rows}:
        cells = [r for r in rows if (r['n'], r['backend']) == key]
        for solved in (r for r in cells if r['outcome'] == 'solved'):
            violations.extend((*key, (solved['k'], solved['t']), (r['k'], r['t']))
                              for r in cells if r['outcome'] == 'unsolved' and
                              backend_is_ad(key[1]) and solved['k'] == r['k'] and solved['t'] < r['t'])
    return violations


def recorded_maps(path=RESULTS, pm_path=PM_RESULTS):
    """Preserve the archived AD run and attach separately identified PM evidence."""
    path = Path(path)
    document = json.loads(path.read_text(encoding='utf-8'))
    sources = {path.name: {key: value for key, value in document.items()
                           if key not in ('rows', 'validation_rows')}}
    sources[path.name]['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    if path.resolve() == RESULTS.resolve() and pm_path is not None:
        pm_path = Path(pm_path)
        pm = json.loads(pm_path.read_text())
        assert pm['complete'], 'PM recording is incomplete'
        validate_pm_recording(pm)
        assert pm['fixtures'] == document['fixtures']
        sources[pm_path.name] = {key: value for key, value in pm.items()
                                 if key not in ('rows', 'validation_rows')}
        sources[pm_path.name]['sha256'] = hashlib.sha256(pm_path.read_bytes()).hexdigest()
        document['rows'] += pm['rows']
        document['validation_rows'] += pm.get('validation_rows', [])
    document['recordings'] = sources
    document['backends'] = [b for b in BACKENDS if any(r['backend'] == b for r in document['rows'])]
    rows = document['rows']
    assert document['fixtures'] == {g['file']: g['sha256'] for g in manifest()}
    assert {r['n'] for r in rows} == set(MAPPED_GAMES)
    assert all(r['outcome'] in ('solved', 'unsolved', 'timeout', 'memory', 'error') for r in rows)
    for n in MAPPED_GAMES:
        winners = {tuple(r['winner_sha256']) for r in rows
                   if r['n'] == n and r['outcome'] == 'solved'}
        assert len(winners) == 1, f'n={n}: winners disagree'
    return document


def pareto(rows):
    solved = {(r['k'], r['t']) for r in rows if r['outcome'] == 'solved'}
    candidates = sorted(pair for pair in solved if not any(
        other != pair and other[0] <= pair[0] and other[1] <= pair[1] for other in solved))
    measured = {(r['k'], r['t']): r['outcome'] for r in rows}
    missing = {pair: [(k, t) for k in range(1, pair[0] + 1) for t in range(pair[1] + 1)
                      if (k, t) != pair and measured.get((k, t)) != 'unsolved']
               for pair in candidates}
    return candidates, [pair for pair in candidates if not missing[pair]], missing


def classify(rows, k_max, t_max):
    """Only measured solves/partials are colored as answers; no across-k inference."""
    measured = {(r['k'], r['t']): r for r in rows}
    solved = {at for at, r in measured.items() if r['outcome'] == 'solved'}
    candidates, confirmed, _ = pareto(rows)
    cells = {}
    for at in ((k, t) for k in range(1, k_max + 1) for t in range(t_max + 1)):
        row = measured.get(at)
        if row is None:
            dominated = any(pair != at and pair[0] <= at[0] and pair[1] <= at[1] for pair in solved)
            cells[at] = ('dominated', '', 'not run; cannot be a Pareto minimum') if dominated else ('unknown', '', 'not run; outcome unknown')
        elif row['outcome'] == 'solved':
            mark = '★' if at in confirmed else '◇' if at in candidates else '✓'
            cells[at] = ('solved', mark, f"solved in {row['seconds']:.1f} s, {row['rounds']} round(s)" +
                         ('; all strict predecessors measured partial' if at in confirmed else '; smaller unmeasured pairs may solve'))
        elif row['outcome'] == 'unsolved':
            cells[at] = ('failed', '✗', f"stabilized with {row['unresolved']} vertices unresolved")
        elif row['outcome'] in ('timeout', 'memory', 'output limit'):
            cells[at] = ('limit', {'timeout': 'T', 'memory': 'M', 'output limit': 'O'}[row['outcome']], 'resource limit; host sufficiency unknown')
        else:
            cells[at] = ('unknown', '!', row.get('detail', 'solver error'))
    return cells


def _glyph(x, y, glyph):
    """Draw symbols as shapes, because notebook viewers may lack these font glyphs."""
    line = ('fill="none" stroke="#fff" stroke-width="2.5" stroke-linecap="round" '
            'stroke-linejoin="round"')
    if glyph == '★':
        points = ' '.join(f'{x + r * sin(i * pi / 5):.1f},{y - r * cos(i * pi / 5):.1f}'
                          for i, r in enumerate([8, 3.4] * 5))
        return f'<polygon points="{points}" fill="#fff"/>'
    if glyph == '◇':
        return f'<polygon points="{x},{y-8} {x+7},{y} {x},{y+8} {x-7},{y}" {line}/>'
    if glyph == '✓':
        return f'<polyline points="{x - 5},{y} {x - 1.5},{y + 4} {x + 5.5},{y - 4.5}" {line}/>'
    if glyph == '✗':
        return (f'<path d="M{x - 4.5} {y - 4.5}L{x + 4.5} {y + 4.5}'
                f'M{x + 4.5} {y - 4.5}L{x - 4.5} {y + 4.5}" {line}/>')
    if glyph:
        return (f'<text x="{x}" y="{y + 5}" text-anchor="middle" font-size="14" '
                f'font-weight="700" fill="#fff">{glyph}</text>')
    return ''


def _square(x, y, state, glyph, tip=''):
    fill, opacity = {'solved': (GREEN, 1), 'failed': (RED, 1),
                     'dominated': ('#94a3b8', .35), 'limit': ('#b45309', 1),
                     'unknown': ('none', 1)}[state]
    stroke = ' stroke="#94a3b8"' if state == 'unknown' else ''
    title = f'<title>{escape(tip)}</title>' if tip else ''
    label = _glyph(x + CELL / 2, y + CELL / 2, glyph)
    return (f'<g><rect x="{x}" y="{y}" width="{CELL}" height="{CELL}" rx="4" fill="{fill}" '
            f'fill-opacity="{opacity}"{stroke}/>{label}{title}</g>')


def grid_svg(title, rows, k_max, t_max):
    """Draw k upward and t rightward; red and green carry the answer, glyphs repeat it."""
    cells = classify(rows, k_max, t_max)
    pitch = CELL + GAP
    left, top = 44, 34
    width, height = left + pitch * (t_max + 1) + 8, top + pitch * k_max + 44
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
             f'width="{width}" height="{height}" role="img" aria-label="{escape(title, quote=True)}" '
             'style="background:white;font:12px system-ui,sans-serif">',
             f'<title>{escape(title)}</title><rect width="100%" height="100%" fill="white"/>',
             f'<text x="4" y="18" font-size="14" font-weight="600" fill="#0f172a">{escape(title)}</text>',
             f'<text x="12" y="{top + pitch * k_max / 2}" fill="#475569">k</text>',
             f'<text x="{left + pitch * (t_max + 1) / 2}" y="{height - 6}" text-anchor="middle" '
             'fill="#475569">t</text>']
    for k in range(1, k_max + 1):
        y = top + pitch * (k_max - k)
        parts.append(f'<text x="{left - 6}" y="{y + CELL / 2 + 4}" text-anchor="end" '
                     f'fill="#475569">{k}</text>')
        for t in range(t_max + 1):
            state, glyph, tip = cells[k, t]
            parts.append(_square(left + pitch * t, y, state, glyph, f'(k,t)=({k},{t}): {tip}'))
    for t in range(t_max + 1):
        parts.append(f'<text x="{left + pitch * t + CELL / 2}" y="{top + pitch * k_max + 14}" '
                     f'text-anchor="middle" fill="#475569">{t}</text>')
    parts.append('</svg>')
    return ''.join(parts).encode('ascii', 'xmlcharrefreplace').decode('ascii')


def legend_svg():
    entries = [('solved', '★', 'confirmed Pareto minimum'), ('solved', '◇', 'observed nondominated solve'),
               ('solved', '✓', 'measured solve'), ('failed', '✗', 'measured partial'),
               ('limit', 'T', 'resource limit: unknown'),
               ('dominated', '', 'dominated, not run'), ('unknown', '', 'not run')]
    parts, x = [], 4
    for state, glyph, label in entries:
        parts.append(_square(x, 4, state, glyph))
        parts.append(f'<text x="{x + CELL + 6}" y="21" fill="#334155">{escape(label)}</text>')
        x += CELL + 18 + 7 * len(label)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {x} 32" width="{x}" height="32" '
            'role="img" aria-label="Legend" style="background:white;font:12px system-ui,sans-serif">'
            '<rect width="100%" height="100%" fill="white"/>' + ''.join(parts) +
            '</svg>').encode('ascii', 'xmlcharrefreplace').decode('ascii')


def maps_html(document):
    grids = []
    for n in MAPPED_GAMES:
        for backend in document['backends']:
            k_max, t_max = caps(n, backend)
            rows = [r for r in document['rows'] if r['n'] == n and r['backend'] == backend]
            grids.append(grid_svg(f'Nester-{n}, {backend}', rows, k_max, t_max))
    return ('<div style="display:flex;flex-wrap:wrap;gap:12px">' + ''.join(grids) + '</div>' +
            legend_svg())


def smallest_pairs(document):
    """List each map's smallest solving pairs and the pairs whose outcome hit the limit."""
    summary = []
    for n in MAPPED_GAMES:
        for backend in document['backends']:
            k_max, t_max = caps(n, backend)
            rows = [r for r in document['rows'] if r['n'] == n and r['backend'] == backend]
            least, confirmed, missing = pareto(rows)
            seconds = {(r['k'], r['t']): r['seconds'] for r in rows}
            limits = sorted((r['k'], r['t']) for r in rows if r['outcome'] == 'timeout')
            summary.append(dict(n=n, backend=backend, least=least, confirmed=confirmed,
                                open_predecessors={str(pair): value for pair, value in missing.items() if value},
                                seconds=[seconds[at] for at in least], limits=limits))
    return summary


def validate_pm_recording(document):
    """Reject a truncated grid: every omitted cell needs a measured dominator."""
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    require(set(document['games']) == set(MAPPED_GAMES), 'Incomplete game selection')
    require(set(document['backends']) == set(PM_BACKENDS), 'Incomplete PM selection')
    require(document['rectangle'] == dict(k_max=6, t_max=6), 'Unexpected rectangle')
    require(document['fixtures'] == {g['file']: g['sha256'] for g in manifest()},
            'Fixture hashes differ')
    rows = document['rows']
    keys = [(r['n'], r['backend'], r['k'], r['t']) for r in rows]
    require(len(set(keys)) == len(keys), 'Duplicate measured pair')
    expected = {(n, b) for n in MAPPED_GAMES for b in PM_BACKENDS}
    require({key[:2] for key in keys} == expected, 'Missing PM map')
    for row in rows:
        k_max, t_max = caps(row['n'], row['backend'])
        require(1 <= row['k'] <= min(6, k_max) and 0 <= row['t'] <= min(6, t_max),
                'Pair outside the studied rectangle')
        require(row['outcome'] in ('solved', 'unsolved', 'timeout', 'memory'),
                'Unexpected solver outcome')
        require(row['timeout_seconds'] == document['timeout_seconds'] and
                row['memory_gib'] == document['memory_gib'], 'Mixed run limits')
        if row['outcome'] in ('solved', 'unsolved'):
            require(row.get('independent_winner_check') is True, 'Unchecked winner IDs')
            require(sum(row['dispatch'].values()) > 0, 'Missing PM dispatch evidence')
    coverage = []
    for n, backend in sorted(expected):
        cells = [r for r in rows if (r['n'], r['backend']) == (n, backend)]
        k_max, t_max = caps(n, backend)
        states = classify(cells, min(6, k_max), min(6, t_max))
        missing = [pair for pair, state in states.items() if state[0] == 'unknown']
        require(not missing, f'{n} {backend}: missing undominated cells {missing}')
        candidates, confirmed, open_pairs = pareto(cells)
        coverage.append(dict(n=n, backend=backend, measured=len(cells),
                             dominated_not_run=sum(s[0] == 'dominated' for s in states.values()),
                             observed_candidates=candidates, confirmed_minima=confirmed,
                             open_predecessors={str(p): v for p, v in open_pairs.items() if v}))
    return coverage


def collect(checkpoint, validation=None, output=PM_RESULTS):
    """Publish the exact checkpoint provenance; keep the original AD recording."""
    checkpoint = Path(checkpoint)
    metadata = json.loads(checkpoint.with_suffix('.metadata.json').read_text())
    rows = [json.loads(line) for line in checkpoint.read_text().splitlines()]
    validation_rows = ([json.loads(line) for line in Path(validation).read_text().splitlines()]
                       if validation is not None and Path(validation).exists() else [])
    document = dict(metadata, search='observed Pareto minima; dominated successors skipped',
                    validation_rows=validation_rows,
                    rows=sorted(rows, key=lambda r: (r['n'], r['backend'], r['k'], r['t'])))
    document['coverage'] = validate_pm_recording(document)
    document['complete'] = True
    Path(output).write_text(json.dumps(document, indent=1)+'\n')
    return recorded_maps(output, pm_path=None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('worker', 'run', 'validate', 'collect'))
    parser.add_argument('--checkpoint', type=Path, default=Path('fixed-pair-maps.jsonl'))
    parser.add_argument('--validation', type=Path, default=Path('fixed-pair-validation.jsonl'))
    parser.add_argument('--games', type=int, nargs='+')
    parser.add_argument('--backends', choices=BACKENDS, nargs='+', default=list(PM_BACKENDS))
    parser.add_argument('--output', type=Path, default=PM_RESULTS)
    parser.add_argument('--seconds', type=float)
    parser.add_argument('--memory-gib', type=int, default=2)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.command == 'worker':
        worker(json.load(sys.stdin))
    elif args.command == 'run':
        run(minima_walk, args.games or MAPPED_GAMES, args.seconds or 600, args.memory_gib,
            args.jobs, args.checkpoint, args.backends)
    elif args.command == 'validate':
        run(exhaustive, args.games or (1, 2, 3), args.seconds or 5, args.memory_gib,
            1, args.validation, args.backends)
    else:
        collect(args.checkpoint, args.validation, args.output)


if __name__ == '__main__':
    main()
