"""Bounded, offline experiments for notebook 07; all workers run sequentially."""

import bz2
import csv
import json
from math import log10
from pathlib import Path
import subprocess
import sys
from time import perf_counter

HERE = Path(__file__).resolve().parent
EVENTS = ('labels', 'attractor', 'call', 'check')
NESTER_BACKENDS = ('pm-strahler', 'lifting-adaptive', 'lifting', 'lifting-sym', 'zielonka')


# Measured on main 6d4d24c, same solver sources as this sprint; seconds are descriptive.
KARY_MEASUREMENTS = {
    'version': '0.4.2.dev2+g6d4d24c',
    'rows': [
        {'k': 2, 'algorithm': 'lifting', 'kary': 2, 'labels': 76, 'attractor': 32, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.0021},
        {'k': 2, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 96, 'attractor': 26, 'call': 7, 'check': 10, 'complete': True, 'seconds': 0.0008},
        {'k': 2, 'algorithm': 'lifting', 'kary': 3, 'labels': 90, 'attractor': 45, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.001},
        {'k': 2, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 110, 'attractor': 37, 'call': 8, 'check': 12, 'complete': True, 'seconds': 0.0009},
        {'k': 2, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 19, 'call': 10, 'check': 0, 'complete': True, 'seconds': 0.0008},
        {'k': 2, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 85, 'attractor': 29, 'call': 7, 'check': 16, 'complete': True, 'seconds': 0.0008},
        {'k': 4, 'algorithm': 'lifting', 'kary': 2, 'labels': 302, 'attractor': 149, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.0033},
        {'k': 4, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 326, 'attractor': 113, 'call': 20, 'check': 31, 'complete': True, 'seconds': 0.0023},
        {'k': 4, 'algorithm': 'lifting', 'kary': 3, 'labels': 444, 'attractor': 250, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.005},
        {'k': 4, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 357, 'attractor': 108, 'call': 24, 'check': 34, 'complete': True, 'seconds': 0.0023},
        {'k': 4, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 111, 'call': 50, 'check': 0, 'complete': True, 'seconds': 0.0037},
        {'k': 4, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 325, 'attractor': 115, 'call': 23, 'check': 54, 'complete': True, 'seconds': 0.0024},
        {'k': 6, 'algorithm': 'lifting', 'kary': 2, 'labels': 822, 'attractor': 438, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.0098},
        {'k': 6, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 791, 'attractor': 301, 'call': 44, 'check': 73, 'complete': True, 'seconds': 0.0059},
        {'k': 6, 'algorithm': 'lifting', 'kary': 3, 'labels': 1554, 'attractor': 974, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.0199},
        {'k': 6, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 900, 'attractor': 283, 'call': 56, 'check': 88, 'complete': True, 'seconds': 0.0057},
        {'k': 6, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 491, 'call': 218, 'check': 0, 'complete': True, 'seconds': 0.0165},
        {'k': 6, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 783, 'attractor': 271, 'call': 47, 'check': 118, 'complete': True, 'seconds': 0.0054},
        {'k': 8, 'algorithm': 'lifting', 'kary': 2, 'labels': 1940, 'attractor': 1168, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.0263},
        {'k': 8, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 1621, 'attractor': 663, 'call': 81, 'check': 140, 'complete': True, 'seconds': 0.0129},
        {'k': 8, 'algorithm': 'lifting', 'kary': 3, 'labels': 4932, 'attractor': 3555, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.0749},
        {'k': 8, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 1869, 'attractor': 588, 'call': 106, 'check': 176, 'complete': True, 'seconds': 0.0117},
        {'k': 8, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 2023, 'call': 898, 'check': 0, 'complete': True, 'seconds': 0.0702},
        {'k': 8, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 1583, 'attractor': 535, 'call': 83, 'check': 218, 'complete': True, 'seconds': 0.011},
        {'k': 10, 'algorithm': 'lifting', 'kary': 2, 'labels': 4264, 'attractor': 2890, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.0668},
        {'k': 10, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 2982, 'attractor': 1316, 'call': 132, 'check': 233, 'complete': True, 'seconds': 0.026},
        {'k': 10, 'algorithm': 'lifting', 'kary': 3, 'labels': 15114, 'attractor': 12272, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.2693},
        {'k': 10, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 3394, 'attractor': 1043, 'call': 174, 'check': 298, 'complete': True, 'seconds': 0.0215},
        {'k': 10, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 8163, 'call': 3626, 'check': 0, 'complete': True, 'seconds': 0.3009},
        {'k': 10, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 2815, 'attractor': 927, 'call': 131, 'check': 354, 'complete': True, 'seconds': 0.0226},
        {'k': 12, 'algorithm': 'lifting', 'kary': 2, 'labels': 9010, 'attractor': 6778, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.1633},
        {'k': 12, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 5116, 'attractor': 2486, 'call': 197, 'check': 352, 'complete': True, 'seconds': 0.0522},
        {'k': 12, 'algorithm': 'lifting', 'kary': 3, 'labels': 45708, 'attractor': 41057, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.9604},
        {'k': 12, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 5605, 'attractor': 1666, 'call': 260, 'check': 454, 'complete': True, 'seconds': 0.0362},
        {'k': 12, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 32735, 'call': 14546, 'check': 0, 'complete': True, 'seconds': 1.2106},
        {'k': 12, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 4569, 'attractor': 1465, 'call': 191, 'check': 526, 'complete': True, 'seconds': 0.0325},
        {'k': 14, 'algorithm': 'lifting', 'kary': 2, 'labels': 18610, 'attractor': 15344, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.3878},
        {'k': 14, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 8417, 'attractor': 4651, 'call': 276, 'check': 497, 'complete': True, 'seconds': 0.1019},
        {'k': 14, 'algorithm': 'lifting', 'kary': 3, 'labels': 137538, 'attractor': 134946, 'call': 0, 'check': 0, 'complete': True, 'seconds': 3.3268},
        {'k': 14, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 8632, 'attractor': 2475, 'call': 364, 'check': 644, 'complete': True, 'seconds': 0.0572},
        {'k': 14, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 131035, 'call': 58234, 'check': 0, 'complete': True, 'seconds': 5.0031},
        {'k': 14, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 6935, 'attractor': 2167, 'call': 263, 'check': 734, 'complete': True, 'seconds': 0.0505},
        {'k': 16, 'algorithm': 'lifting', 'kary': 2, 'labels': 37928, 'attractor': 33932, 'call': 0, 'check': 0, 'complete': True, 'seconds': 0.9039},
        {'k': 16, 'algorithm': 'lifting-sym', 'kary': 2, 'labels': 13583, 'attractor': 8833, 'call': 369, 'check': 668, 'complete': True, 'seconds': 0.2043},
        {'k': 16, 'algorithm': 'lifting', 'kary': 3, 'labels': 413076, 'attractor': 438767, 'call': 0, 'check': 0, 'complete': True, 'seconds': 11.4776},
        {'k': 16, 'algorithm': 'lifting-sym', 'kary': 3, 'labels': 12605, 'attractor': 3488, 'call': 486, 'check': 868, 'complete': True, 'seconds': 0.0875},
        {'k': 16, 'algorithm': 'zielonka', 'kary': None, 'labels': 0, 'attractor': 524247, 'call': 232994, 'check': 0, 'complete': True, 'seconds': 20.7305},
        {'k': 16, 'algorithm': 'zielonka-sym', 'kary': None, 'labels': 10003, 'attractor': 3051, 'call': 347, 'check': 978, 'complete': True, 'seconds': 0.0743},
    ],
}


def nester_formula(n):
    """NesterSpec's phi; cases/mlsolver/formulas/__init__.py in Keiren's generator.

    https://github.com/jkeiren/paritygame-generator/blob/master/cases/mlsolver/formulas/__init__.py
    MLSolver checks validity of (phi | !phi), using --validity lmmc.
    """
    if not isinstance(n, int) or n < 1:
        raise ValueError('n must be a positive integer')
    body = f'q_{n} | <a>X_{n}'
    for i in range(n - 1, 0, -1):
        body = f'q_{i} | <a>(X_{i} & ({body}))'
    return ''.join(f"{'nu' if i % 2 else 'mu'} X_{i}. " for i in range(1, n + 1)) + f'({body})'


def history():
    """Read the repository CSV, or its full copy in the release zip."""
    path = HERE.parent / 'keiren/results.csv'
    if not path.is_file():
        path = HERE / 'keiren/results.csv'
    if not path.is_file():
        print('Full keiren/results.csv absent; using nester/history.csv (Nester rows only). '
              'Use a repository checkout or the notebooks zip for the full experiment.')
        path = HERE / 'nester/history.csv'
    with path.open(newline='') as stream:
        return [r for r in csv.DictReader(stream) if r['game'].startswith('mlsolver/games/Nestern=')]


def historical_maxima(rows=None):
    rows = history() if rows is None else rows
    result = []
    for backend in sorted({r['backend'] for r in rows}):
        for compact in (False, True):
            solved = [r for r in rows if r['backend'] == backend and
                      ('_compact' in r['game']) == compact and r['outcome'] == 'complete']
            if solved:
                result.append(max(solved, key=lambda r: int(r['game'].split('Nestern=')[1].split('.')[0].split('_')[0])))
    return result


def bounded_worker(request, seconds):
    """One worker at a time; preserve finished attempts if its deadline expires."""
    started = perf_counter()
    try:
        process = subprocess.run([sys.executable, str(Path(__file__).resolve())],
                    input=json.dumps(request), text=True, capture_output=True, timeout=seconds)
        output = process.stdout
        status = 'error' if process.returncode else 'finished'
        detail = process.stderr.strip() or (f'worker exited with status {process.returncode}' if process.returncode else '')
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b''
        if isinstance(output, bytes):
            output = output.decode('utf-8')
        status, detail = 'timeout', f'{seconds:g} s deadline'
    rows = [json.loads(line) for line in output.splitlines() if line.startswith('{') and line.endswith('}')]
    result = rows[-1] if rows else {}
    if status != 'finished':
        result = dict(result, outcome=status, detail=detail, stopped_by=detail)
    result['seconds'] = round(perf_counter() - started, 4)
    return result


def measure_kary(ks=(4, 8, 12, 16)):
    rows = []
    for expected in KARY_MEASUREMENTS['rows']:
        if expected['k'] not in ks:
            continue
        row = bounded_worker(dict(kind='count', k=expected['k'], algorithm=expected['algorithm'],
                                  kary=expected['kary']), 40)
        assert row.get('outcome') == 'complete', row
        for key in ('labels', 'attractor', 'call', 'check', 'complete'):
            assert row[key] == expected[key], (expected, row)
        rows.append(row)
    return rows


def measure_nester(seconds=1.5):
    rows = []
    for path in sorted((HERE / 'nester').glob('*.gm.bz2')):
        for backend in NESTER_BACKENDS:
            row = bounded_worker(dict(kind='nester', game=path.name, backend=backend), seconds)
            rows.append(dict(game=path.name.removesuffix('.bz2'), backend=backend, **row))
    return rows


def growth(rows, algorithm, arity, event='labels'):
    values = [(r['k'], r[event]) for r in rows if r['algorithm'] == algorithm and r['kary'] == arity]
    return [(k, value, (value / previous) ** (2 / (k - last_k)))
            for (last_k, previous), (k, value) in zip(values, values[1:]) if previous and value]


def count_plot(rows):
    """Use the examples' inline SVG plot style; no plotting dependency is needed."""
    from IPython.display import HTML, display
    colours = ('#2563eb', '#b45309', '#15803d', '#c026d3', '#64748b', '#dc2626')
    groups = list(dict.fromkeys((r['algorithm'], r['kary']) for r in rows))
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 900 440" role="img" aria-label="Trace event counts, logarithmic y axis"><rect width="900" height="440" fill="white"/>']
    for power in range(7):
        y = 340 - power * 43
        svg.append(f'<path d="M65 {y}H650" stroke="#cbd5e1"/><text x="5" y="{y+4}">10^{power}</text>')
    for i, (algorithm, arity) in enumerate(groups):
        event = 'call' if arity is None else 'labels'
        values = [(r['k'], r[event]) for r in rows if r['algorithm'] == algorithm and r['kary'] == arity]
        points = ' '.join(f'{65+(k-4)*48},{340-log10(max(1,v))*43}' for k,v in values)
        svg.append(f'<polyline fill="none" stroke="{colours[i]}" stroke-width="3" points="{points}"/>')
        svg.append(f'<text x="660" y="{95+i*35}" fill="{colours[i]}">{algorithm}/{arity or "calls"}</text>')
    for k in sorted({r['k'] for r in rows}):
        svg.append(f'<text x="{65+(k-4)*48}" y="370">{k}</text>')
    svg.append('<text x="280" y="405">k (5k vertices); labels for lifting, calls for Zielonka</text></svg>')
    display(HTML(''.join(svg)))


def _strahler(tree):
    if tree is None:
        return 0
    values = [_strahler(child.get('node', child)) for child in tree.get('children', [])]
    largest = max(values, default=1)
    return largest + (values.count(largest) > 1)


def _worker(request):
    import strahler
    if request['kind'] == 'count':
        from otj22 import hk, count_run
        counts, result, phases = count_run(hk(request['k']), request['algorithm'],
                                **({'kary': request['kary']} if request['kary'] else {}))
        print(json.dumps(dict(request, **counts, complete=result['complete'],
                              outcome='complete' if result['complete'] else 'partial')), flush=True)
        return
    # Linux enforces a 512 MiB address-space cap; wall deadlines also apply on macOS.
    if sys.platform.startswith('linux'):
        import resource
        resource.setrlimit(resource.RLIMIT_AS, (512 * 1024**2, 512 * 1024**2))
    text = bz2.open(HERE / 'nester' / request['game'], 'rt', encoding='utf-8').read()
    game = strahler.Game.from_text(text)
    backend = request['backend']
    attempts = [(k, t) for k in range(1, 5) for t in range(4)] if backend == 'pm-strahler' else [(k, None) for k in range(1, 5)] if backend in ('lifting', 'lifting-sym') else [(None, None)]
    row = {}
    for k, t in attempts:
        try:
            if backend == 'pm-strahler':
                document = strahler.progress_measures(game, algorithm='strahler', k=k, t=t)
                result = document['rounds'][-1]
                numbers = [_strahler(p['tree']) for p in result['trees']]
                parameters = [p['parameters'] for p in result['trees']]
                tree = None
            else:
                algorithm = 'lifting' if backend == 'lifting-adaptive' else backend
                if backend == 'lifting-adaptive':
                    options = dict(adaptive_t=True, tree_k=4, max_host_leaves=4096)
                else:
                    options = dict(tree_k=k) if k else {}
                document = strahler.decompose(game, algorithm, **options).data
                result = document
                numbers = [p['strahler'] for p in document['metrics']['trees']]
                parameters = document['parameters']
                tree = document['trees'][0] if request['game'] == 'Nestern=2.gm.bz2' and result['complete'] else None
            row = dict(outcome='complete' if result['complete'] else 'partial', strahler=numbers,
                       parameters=parameters, vertices=len(game.vertices), priorities=len(set(game.priority.values())),
                       version=strahler.version(), stopped_by='—' if result['complete'] else
                       '4096-leaf smoke budget' if document.get('budget_exhausted') else
                       'K cap 4 exhausted' if backend == 'lifting-adaptive' else 'host too small; searching grid')
            if tree:
                row['tree'] = tree
                row['region'] = document['regions'][0]
            print(json.dumps(row), flush=True)
            if result['complete']:
                return
        except (strahler.StrahlerError, MemoryError) as error:
            detail = str(error).splitlines()[0] if str(error) else type(error).__name__
            leaf_limit = 'more than' in detail and ('leaves' in detail or 'limit' in detail)
            allocation = isinstance(error, MemoryError) or 'allocat' in detail
            print(json.dumps(dict(row, outcome='host limit' if leaf_limit else 'out of memory' if allocation else 'error',
                                  detail=detail, stopped_by=detail if leaf_limit else
                                  ('allocation failure under 512 MiB address-space budget' if sys.platform.startswith('linux')
                                   else 'allocation failure (address-space cap unavailable)') if allocation else detail,
                                  version=strahler.version())), flush=True)
            return
    if backend != 'lifting-adaptive':
        row['stopped_by'] = 'search grid exhausted'
        print(json.dumps(row), flush=True)


if __name__ == '__main__':
    _worker(json.load(sys.stdin))
