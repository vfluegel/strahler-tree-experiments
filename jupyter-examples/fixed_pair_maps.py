"""Measure and draw which fixed (k,t) hosts solve Nester with symmetric AD."""

import argparse
import bz2
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from html import escape
import json
from math import cos, pi, sin
from pathlib import Path
import resource
import signal
import subprocess
import sys
import threading
from time import perf_counter

from lexicographic_nester import DATA, manifest, winner_digest

RESULTS = DATA / 'fixed-pair-maps-600s.json'
BACKENDS = ('AD-sym', 'AD-sym+iter')
MAPPED_GAMES = (4, 5)
GREEN, RED = '#0ca30c', '#d03b3b'
CELL, GAP = 24, 2
_write_lock = threading.Lock()


def worker(request):
    """Run one pair in this process, so the address-space limit covers the host."""
    from strahler import _native

    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    limit = request['memory_gib'] * 1024**3
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    game = next(g for g in manifest() if g['n'] == request['n'])
    raw = bz2.decompress((DATA / game['file']).read_bytes())
    handle = _native.call('game_parse', raw, len(raw), handle=True)
    del raw
    try:
        flags = _native.options(None, dict(
            tree_k=request['k'], tree_t=request['t'],
            iter_shave=request['backend'].endswith('+iter'),
            max_host_leaves=2 * sys.maxsize + 1), 'lifting-sym')
        document = json.loads(_native.call('decompose', handle, flags, len(flags),
                                           _native.Callback(), None))
    finally:
        _native.library().strahler_game_free(handle)
    won = [set(region) for region in document['regions']]
    for player in range(2):
        won[player].update(document['initial_won'][player])
        for shave in document['shaves']:
            won[player].update(shave['attracted'][player])
    assert not won[0] & won[1]
    assert sum(map(len, won)) + len(document['unresolved']) == game['vertices']
    parameters = document['parameters']
    assert all(p['t'] == request['t'] for p in parameters)
    assert max(p['K'] for p in parameters) == min(request['k'], max(game['AD_k_max']))
    print(json.dumps(dict(
        outcome='solved' if document['complete'] else 'unsolved',
        rounds=len(document['shaves']) + 1, won=list(map(len, won)),
        unresolved=len(document['unresolved']), parameters=parameters,
        winner_sha256=[winner_digest(w) for w in won] if document['complete'] else None)),
          flush=True)


def measure(n, backend, k, t, seconds, memory_gib):
    request = dict(n=n, backend=backend, k=k, t=t, memory_gib=memory_gib)
    started = perf_counter()
    try:
        process = subprocess.run([sys.executable, str(Path(__file__).resolve()), 'worker'],
                                 input=json.dumps(request), text=True, capture_output=True,
                                 timeout=seconds)
        if process.returncode == 0:
            result = json.loads(process.stdout)
        else:
            lines = process.stderr.strip().splitlines()
            detail = lines[-1][:300] if lines else ''
            killed = process.returncode in (-signal.SIGKILL, -signal.SIGSEGV, -signal.SIGABRT)
            memory = killed or any(w in detail.lower()
                                   for w in ('memory', 'allocat', 'failed to build'))
            result = dict(outcome='memory' if memory else 'error', detail=detail,
                          returncode=process.returncode)
    except subprocess.TimeoutExpired:
        result = dict(outcome='timeout')
    return dict(n=n, backend=backend, k=k, t=t, seconds=round(perf_counter() - started, 3),
                timeout_seconds=seconds, memory_gib=memory_gib, **result)


def caps(n):
    game = next(g for g in manifest() if g['n'] == n)
    return max(game['AD_k_max']), game['t_max']


def staircase(measured, n, backend):
    """Raise k, trying only t below the best solved t; a resource failure ends a column."""
    k_max, t_max = caps(n)
    bound = t_max + 1
    for k in range(1, k_max + 1):
        for t in range(bound):
            outcome = measured(n, backend, k, t)['outcome']
            if outcome == 'solved':
                bound = t
            if outcome != 'unsolved':
                break
        if bound == 0:
            return


def exhaustive(measured, n, backend):
    k_max, t_max = caps(n)
    for k in range(1, k_max + 1):
        for t in range(t_max + 1):
            measured(n, backend, k, t)


def run(walk, games, seconds, memory_gib, jobs, output):
    """Walk each map, appending rows to a JSON Lines checkpoint that later runs resume."""
    output = Path(output)
    rows = {}
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
            print(f"n={n} {backend} (k,t)=({k},{t}): {row['outcome']}, "
                  f"{row['seconds']:.1f} s", flush=True)
        return rows[key]

    with ThreadPoolExecutor(jobs) as pool:
        futures = [pool.submit(walk, measured, n, backend)
                   for n in games for backend in reversed(BACKENDS)]
        for future in futures:
            future.result()


def monotonicity_violations(rows):
    """Pairs where a host solves a game that a host containing it left unsolved."""
    violations = []
    for key in {(r['n'], r['backend']) for r in rows}:
        cells = [r for r in rows if (r['n'], r['backend']) == key]
        for solved in (r for r in cells if r['outcome'] == 'solved'):
            violations.extend((*key, (solved['k'], solved['t']), (r['k'], r['t']))
                              for r in cells if r['outcome'] == 'unsolved' and
                              solved['k'] <= r['k'] and solved['t'] <= r['t'])
    return violations


def recorded_maps(path=RESULTS):
    document = json.loads(Path(path).read_text(encoding='utf-8'))
    rows = document['rows']
    assert document['fixtures'] == {g['file']: g['sha256'] for g in manifest()}
    assert {r['n'] for r in rows} == set(MAPPED_GAMES)
    assert all(r['outcome'] in ('solved', 'unsolved', 'timeout', 'memory') for r in rows)
    for n in MAPPED_GAMES:
        winners = {tuple(r['winner_sha256']) for r in rows
                   if r['n'] == n and r['outcome'] == 'solved'}
        assert len(winners) == 1, f'n={n}: winners disagree'
    assert not monotonicity_violations(rows)
    assert not monotonicity_violations(document['validation_rows'])
    return document


def classify(rows, k_max, t_max):
    """Map each (k,t) to (state, glyph, description), inferring only along host inclusion."""
    measured = {(r['k'], r['t']): r for r in rows}
    solved = {at for at, r in measured.items() if r['outcome'] == 'solved'}
    small = {at for at, r in measured.items() if r['outcome'] == 'unsolved'}

    def within(inner, outer):
        return inner != outer and inner[0] <= outer[0] and inner[1] <= outer[1]

    cells = {}
    for at in ((k, t) for k in range(1, k_max + 1) for t in range(t_max + 1)):
        row = measured.get(at)
        if row is None:
            if any(within(pair, at) for pair in solved):
                cells[at] = 'implied-solved', '', 'not run; large enough, but may exceed the limits'
            elif any(within(at, pair) for pair in small):
                cells[at] = 'implied-failed', '', 'not run; inside a host that failed'
            else:
                cells[at] = 'unknown', '', 'not run'
        elif row['outcome'] == 'solved':
            least = not any(within(pair, at) for pair in solved)
            cells[at] = ('solved', '★' if least else '✓',
                         f"solved in {row['seconds']:.1f} s, {row['rounds']} round(s)")
        elif row['outcome'] == 'unsolved':
            cells[at] = ('failed', '✗',
                         f"{row['unresolved']} vertices unresolved after {row['seconds']:.1f} s")
        elif row['outcome'] == 'timeout':
            cells[at] = 'failed', 'T', f"no answer within {row['timeout_seconds']:g} s"
        else:
            cells[at] = 'failed', 'M', f"exceeded {row['memory_gib']} GiB"
    return cells


def _glyph(x, y, glyph):
    """Draw symbols as shapes, because notebook viewers may lack these font glyphs."""
    line = ('fill="none" stroke="#fff" stroke-width="2.5" stroke-linecap="round" '
            'stroke-linejoin="round"')
    if glyph == '★':
        points = ' '.join(f'{x + r * sin(i * pi / 5):.1f},{y - r * cos(i * pi / 5):.1f}'
                          for i, r in enumerate([8, 3.4] * 5))
        return f'<polygon points="{points}" fill="#fff"/>'
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
                     'implied-solved': (GREEN, .35), 'implied-failed': (RED, .35),
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
    entries = [('solved', '★', 'smallest solving pair'), ('solved', '✓', 'solved'),
               ('implied-solved', '', 'large enough, not run'),
               ('failed', '✗', 'host too small'), ('failed', 'T', '600 s limit'),
               ('unknown', '', 'not run')]
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
        k_max, t_max = caps(n)
        for backend in BACKENDS:
            rows = [r for r in document['rows'] if r['n'] == n and r['backend'] == backend]
            grids.append(grid_svg(f'Nester-{n}, {backend}', rows, k_max, t_max))
    return ('<div style="display:flex;flex-wrap:wrap;gap:12px">' + ''.join(grids) + '</div>' +
            legend_svg())


def smallest_pairs(document):
    """List each map's smallest solving pairs and the pairs whose outcome hit the limit."""
    summary = []
    for n in MAPPED_GAMES:
        k_max, t_max = caps(n)
        for backend in BACKENDS:
            rows = [r for r in document['rows'] if r['n'] == n and r['backend'] == backend]
            cells = classify(rows, k_max, t_max)
            least = sorted(at for at, cell in cells.items() if cell[1] == '★')
            seconds = {(r['k'], r['t']): r['seconds'] for r in rows}
            limits = sorted((r['k'], r['t']) for r in rows if r['outcome'] == 'timeout')
            summary.append(dict(n=n, backend=backend, least=least,
                                seconds=[seconds[at] for at in least], limits=limits))
    return summary


def collect(checkpoint, validation, output=RESULTS):
    """Publish a finished checkpoint with the provenance of the library that measured it."""
    import hashlib
    import os
    import platform
    import strahler
    from strahler import _native

    def load(path):
        return [json.loads(line) for line in Path(path).read_text().splitlines()]

    checkpoint = Path(checkpoint)
    library = Path(_native.library()._name).read_bytes()
    recorded = datetime.fromtimestamp(checkpoint.stat().st_mtime, timezone.utc)
    document = dict(
        search='fixed pair, staircase over k then t', version=strahler.version(),
        library_sha256=hashlib.sha256(library).hexdigest(),
        recorded_utc=recorded.isoformat(timespec='seconds'),
        machine=f'{platform.system()} {platform.machine()}, {os.cpu_count()} CPUs',
        timeout_seconds=600, memory_gib=2, jobs=2, backends=list(BACKENDS),
        fixtures={g['file']: g['sha256'] for g in manifest()},
        validation='exhaustive grids: Nester-1 and 2 at 10 s, Nester-3 at 5 s per pair',
        validation_rows=[{key: r[key] for key in ('n', 'backend', 'k', 't', 'outcome')}
                         for r in load(validation)],
        rows=load(checkpoint))
    Path(output).write_text(json.dumps(document, indent=1) + '\n', encoding='utf-8')
    return recorded_maps(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('worker', 'run', 'validate', 'collect'))
    parser.add_argument('--checkpoint', type=Path, default=Path('fixed-pair-maps.jsonl'))
    parser.add_argument('--validation', type=Path, default=Path('fixed-pair-validation.jsonl'))
    parser.add_argument('--games', type=int, nargs='+')
    parser.add_argument('--seconds', type=float)
    parser.add_argument('--memory-gib', type=int, default=2)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.command == 'worker':
        worker(json.load(sys.stdin))
    elif args.command == 'run':
        run(staircase, args.games or MAPPED_GAMES, args.seconds or 600, args.memory_gib,
            args.jobs, args.checkpoint)
    elif args.command == 'validate':
        run(exhaustive, args.games or (1, 2, 3), args.seconds or 5, args.memory_gib,
            1, args.validation)
    else:
        collect(args.checkpoint, args.validation)


if __name__ == '__main__':
    main()
