"""Offline Nester experiments with the same lexicographic search in AD and PM."""

import argparse
import bz2
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import hashlib
from html import escape
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
from time import perf_counter

HERE = Path(__file__).resolve().parent
DATA = HERE / 'nester'
MANIFEST = DATA / 'plain-manifest.json'
RESULTS = DATA / 'lexicographic-results.json'
TIMEOUT_RESULTS = DATA / 'lexicographic-timeouts-60s.json'
FIVE_MINUTE_RESULTS = DATA / 'lexicographic-timeouts-300s.json'
FINAL_RESULTS = DATA / 'lexicographic-results-300s.json'
BACKENDS = ('AD', 'AD+iter', 'PM', 'PM+iter')


def bounds(vertices, maximum):
    """Use the normalized priority bound and count tree heights in nodes."""
    if vertices < 1 or maximum < 0:
        raise ValueError('Expected a nonempty game and nonnegative priorities')
    t_max = vertices.bit_length() - 1
    ad_height = [(maximum + maximum % 2) // 2 + 1,
                 (maximum + (maximum % 2 == 0) - 1) // 2 + 1]
    depth = [(maximum + 1) // 2, maximum // 2 + 1]
    return dict(t_max=t_max, AD_height=ad_height, PM_depth=depth,
                AD_k_max=[min(h, t_max + 1) for h in ad_height],
                PM_k_max=[min(d + 1, t_max + 1) for d in depth])


def pairs(vertices, height):
    """The requested order: t runs to its cap before k changes."""
    t_max = vertices.bit_length() - 1
    for k in range(1, min(height, t_max + 1) + 1):
        for t in range(t_max + 1):
            yield k, t


def manifest():
    return json.loads(MANIFEST.read_text(encoding='utf-8'))['games']


def build_manifest():
    """Read the original PGSolver fixtures without retaining their graphs."""
    games = []
    for n in range(1, 8):
        path = DATA / f'Nestern={n}.gm.bz2'
        count = edges = 0
        priorities = set()
        with bz2.open(path, 'rt', encoding='utf-8') as stream:
            for line in stream:
                fields = line.split(None, 4)
                if not fields or not fields[0].isdigit():
                    continue
                count += 1
                priorities.add(int(fields[1]))
                edges += fields[3].rstrip(';').count(',') + 1
        ordered = sorted(priorities)
        maximum = ordered[0] % 2
        for priority in ordered[1:]:
            maximum += 1
            maximum += maximum % 2 != priority % 2
        games.append(dict(n=n, file=path.name, vertices=count, edges=edges,
                          priorities=len(priorities), normalized_maximum=maximum,
                          sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                          **bounds(count, maximum)))
    document = dict(source='https://doi.org/10.6084/m9.figshare.6004130.v1',
                    archive='mlsolver', figshare_file=10792904, games=games)
    MANIFEST.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
    return games


def tree_vertices(tree):
    if tree is None:
        return set()
    vertices = set(tree.get('vertices') or [])
    for child in tree.get('children', []):
        vertices.update(tree_vertices(child.get('node', child)))
    return vertices


def tree_strahler(tree):
    if tree is None:
        return 0
    children = [tree_strahler(c.get('node', c)) for c in tree.get('children', [])]
    largest = max(children, default=1)
    return largest + (children.count(largest) > 1)


def host_svg(k, t, height, title):
    """Draw the complete host with compact nodes so Nester-1 stays readable."""
    import strahler

    # Notebook U^k_{t,h}: genstree's argument order is (k, h, t).
    tree = strahler.genstree(k, height, t)['tree']
    nodes, edges = [], []
    leaves = 0

    def visit(node, level=0, path=()):
        nonlocal leaves
        children = []
        for i, child in enumerate(node.get('children', [])):
            child_node = child.get('node', child)
            placed = visit(child_node, level + 1, path + (i,))
            children.append(placed)
            edges.append((placed, child.get('label', '')))
        if children:
            x = (children[0]['x'] + children[-1]['x']) / 2
        else:
            x = 65 + 25 * leaves
            leaves += 1
        placed = dict(x=x, y=65 + 70 * level, level=level,
                      path='root' + ''.join(f'/{p + 1}' for p in path))
        nodes.append(placed)
        for child in children:
            child['parent'] = placed
        return placed

    visit(tree)
    width, svg_height = max(450, 90 + 25 * leaves), 110 + 70 * (height - 1)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {svg_height}" '
             f'width="{width}" height="{svg_height}" role="img" '
             f'aria-label="{escape(title, quote=True)}" style="background:white;font:12px system-ui,sans-serif">',
             f'<title>{escape(title)}</title><rect width="100%" height="100%" fill="white"/>',
             f'<text x="12" y="19" font-size="14">{escape(title)}</text>',
             f'<text x="12" y="38" fill="#475569">{len(nodes)} nodes, {leaves} leaves, '
             f'{height} node levels (root included)</text>']
    for child, label in edges:
        parent = child['parent']
        parts.append(f'<line x1="{parent["x"]}" y1="{parent["y"]}" '
                     f'x2="{child["x"]}" y2="{child["y"]}" stroke="#94a3b8"/>')
        parts.append(f'<text x="{child["x"] + 4}" y="{child["y"] - 12}" '
                     f'font-size="10" fill="#475569">{escape(label or "ε")}</text>')
    for node in nodes:
        parts.append(f'<circle cx="{node["x"]}" cy="{node["y"]}" r="4" fill="#1d4ed8">'
                     f'<title>{escape(node["path"])}, level {node["level"]}</title></circle>')
    for level in range(height):
        parts.append(f'<text x="8" y="{69 + 70 * level}" fill="#475569">{level}</text>')
    parts.append('</svg>')
    return ''.join(parts).encode('ascii', 'xmlcharrefreplace').decode('ascii')


def winner_digest(vertices):
    digest = hashlib.sha256()
    for vertex in sorted(vertices):
        digest.update(f'{vertex}\n'.encode('ascii'))
    return digest.hexdigest()


def summarize(document, backend, info):
    """Keep final-tree metrics separate from wins accumulated by shaving."""
    if backend.startswith('AD'):
        # U^k_{t,h}: AD reports k as K and h as H.
        parameters = document['parameters']
        won = [set(v) for v in document['regions']]
        for p in range(2):
            won[p].update(document['initial_won'][p])
            for shave in document['shaves']:
                won[p].update(shave['attracted'][p])
        numbers = [m['strahler'] for m in document['metrics']['trees']]
        complete = document['complete']
        unresolved = len(document['unresolved'])
        k, t = max(p['K'] for p in parameters), parameters[0]['t']
        attempts = (k - 1) * (info['t_max'] + 1) + t + 1
        rounds = len(document['shaves']) + 1
        budget = document['budget_exhausted']
        example = dict(parameters=parameters, trees=document['trees'],
                       shaves=document['shaves'])
    else:
        # PM reports k and t directly; its host height is h = depth + 1.
        last = document['rounds'][-1]
        parameters = [p['parameters'] for p in last['trees']]
        won = [set(), set()]
        for round_ in document['rounds']:
            for p, player in enumerate(round_['trees']):
                won[p].update(tree_vertices(player['tree']))
                won[p].update(player['attracted'])
                won[p].update(player['shaved'])
        numbers = [tree_strahler(p['tree']) for p in last['trees']]
        complete = last['complete']
        unresolved = len(last['unresolved'])
        k, t = max(p['k'] for p in parameters), parameters[0]['t']
        attempts = last['round']
        rounds = len(document['rounds'])
        budget = False
        example = dict(parameters=parameters, rounds=document['rounds'])
    assert not won[0] & won[1]
    assert sum(map(len, won)) + unresolved == info['vertices']
    result = dict(outcome='complete' if complete else 'host limit' if budget else 'partial',
                  k=k, t=t, attempts=attempts, rounds=rounds, parameters=parameters,
                  tree_strahler=numbers, won=list(map(len, won)), unresolved=unresolved,
                  winner_sha256=[winner_digest(w) for w in won])
    if info['n'] == 1:
        result['example'] = example
    return result


def worker(request):
    """Use the ABI directly so a million-vertex input needs no Python graph copy."""
    import resource
    import strahler
    from strahler import _native

    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if sys.platform.startswith('linux'):
        limit = request['memory_gib'] * 1024**3
        resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    info = request['game']
    backend = request['backend']
    raw = bz2.decompress((DATA / info['file']).read_bytes())
    handle = _native.call('game_parse', raw, len(raw), handle=True)
    del raw
    try:
        options = dict(search='lexicographic', iter_shave=backend.endswith('+iter'))
        if backend.startswith('AD'):
            options['max_host_leaves'] = 2 * sys.maxsize + 1
            flags = _native.options(None, options, 'lifting')
            text = _native.call('decompose', handle, flags, len(flags),
                                _native.Callback(), None)
        else:
            flags = _native.options(None, dict(algorithm='strahler', **options))
            text = _native.call('progress_measures', handle, flags, len(flags))
        document = json.loads(text)
        del text
        result = summarize(document, backend, info)
        result['version'] = strahler.version()
        print(json.dumps(result), flush=True)
    finally:
        _native.library().strahler_game_free(handle)


def measure_case(game, backend, seconds=30, memory_gib=2):
    if backend not in BACKENDS:
        raise ValueError(f'Unknown backend: {backend}')
    request = dict(game=game, backend=backend, memory_gib=memory_gib)
    started = perf_counter()
    try:
        process = subprocess.run([sys.executable, str(Path(__file__).resolve()), 'worker'],
                                 input=json.dumps(request), text=True, capture_output=True,
                                 timeout=seconds)
        if process.returncode == 0:
            result = json.loads(process.stdout)
        else:
            detail = process.stderr.strip().splitlines()[-1] if process.stderr.strip() else ''
            memory = any(w in detail.lower() for w in ('memory', 'allocat', 'failed to build'))
            killed = process.returncode in (-signal.SIGKILL, -signal.SIGSEGV)
            result = dict(outcome='out of memory' if memory else 'killed' if killed else 'error',
                          detail=detail[:300], returncode=process.returncode)
    except subprocess.TimeoutExpired:
        result = dict(outcome='timeout', detail=f'{seconds:g} s wall deadline')
    return dict(n=game['n'], backend=backend, seconds=round(perf_counter() - started, 4),
                timeout_seconds=seconds, memory_gib=memory_gib, **result)


def check_agreement(rows):
    """Every completed backend must agree on both original winning regions."""
    for n in range(1, 8):
        solved = [r for r in rows if r['n'] == n and r['outcome'] == 'complete']
        assert len({tuple(r['winner_sha256']) for r in solved}) <= 1, f'n={n}: winners disagree'


def recorded_results():
    document = json.loads(RESULTS.read_text(encoding='utf-8'))
    expected = {(n, backend) for n in range(1, 8) for backend in BACKENDS}
    assert len(document['rows']) == len(expected)
    assert {(r['n'], r['backend']) for r in document['rows']} == expected
    assert document['fixtures'] == {g['file']: g['sha256'] for g in manifest()}
    check_agreement(document['rows'])
    return document


def recorded_timeout_results(path=None, baseline_path=None):
    path = Path(path) if path is not None else TIMEOUT_RESULTS
    baseline_path = Path(baseline_path) if baseline_path is not None else RESULTS
    original = recorded_results()
    baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
    document = json.loads(path.read_text(encoding='utf-8'))
    expected = {(r['n'], r['backend']) for r in baseline['rows'] if r['outcome'] == 'timeout'}
    if document.get('stop_after_n') is not None:
        expected = {key for key in expected if key[0] <= document['stop_after_n']}
    assert len(document['rows']) == len(expected)
    assert document['cases'] == len(expected)
    assert {(r['n'], r['backend']) for r in document['rows']} == expected
    assert document['baseline_sha256'] == hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    assert document['fixtures'] == baseline['fixtures'] == original['fixtures']
    check_agreement([*original['rows'], *baseline['rows'], *document['rows']])
    return document


def _consolidated_results():
    sources = [(RESULTS, recorded_results()),
               (TIMEOUT_RESULTS, recorded_timeout_results()),
               (FIVE_MINUTE_RESULTS,
                recorded_timeout_results(FIVE_MINUTE_RESULTS, TIMEOUT_RESULTS))]
    rows = {}
    recordings = {}
    for path, document in sources:
        recordings[path.name] = dict(
            sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            **{key: document[key] for key in ('version', 'library_sha256', 'recorded_utc',
                                             'machine', 'memory_cap_enforced')},
            jobs=document.get('jobs', 1))
        for row in document['rows']:
            rows[row['n'], row['backend']] = dict(row, recording=path.name)
    final = sources[-1][1]
    stop_after_n = final.get('stop_after_n')
    if stop_after_n is not None:
        for key in rows:
            if key[0] > stop_after_n and rows[key]['outcome'] != 'complete':
                rows[key] = dict(n=key[0], backend=key[1], outcome='not measured',
                                 detail=f'Beyond the Nester-{stop_after_n} frontier',
                                 timeout_seconds=final['timeout_seconds'], memory_gib=final['memory_gib'],
                                 recording=sources[-1][0].name)
    ordered = [rows[n, backend] for n in range(1, 8) for backend in BACKENDS]
    assert all(r['timeout_seconds'] <= final['timeout_seconds'] for r in ordered)
    assert all(r['timeout_seconds'] == final['timeout_seconds']
               for r in ordered if r['outcome'] == 'timeout')
    assert all(r['memory_gib'] == final['memory_gib'] for r in ordered)
    check_agreement(ordered)
    document = dict(search='lexicographic', timeout_seconds=final['timeout_seconds'],
                    memory_gib=final['memory_gib'], fixtures=final['fixtures'],
                    recordings=recordings, rows=ordered)
    if stop_after_n is not None:
        document['stop_after_n'] = stop_after_n
        document['stop_reason'] = final.get('stop_reason', f'Stopped after Nester-{stop_after_n}')
    return document


def collect_results(output=None):
    """Publish one result per case, retaining the original measurement provenance."""
    output = Path(output) if output is not None else FINAL_RESULTS
    document = _consolidated_results()
    temporary = output.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
    temporary.replace(output)
    return document


def recorded_final_results():
    document = json.loads(FINAL_RESULTS.read_text(encoding='utf-8'))
    assert document == _consolidated_results()
    return document


def _run_metadata(games):
    import strahler
    from strahler import _native

    return dict(search='lexicographic', version=strahler.version(),
                library_sha256=hashlib.sha256(Path(_native.library()._name).read_bytes()).hexdigest(),
                recorded_utc=datetime.now(timezone.utc).isoformat(timespec='seconds'),
                machine=f'{platform.system()} {platform.machine()}, {os.cpu_count()} CPUs',
                memory_cap_enforced=sys.platform.startswith('linux'),
                fixtures={g['file']: g['sha256'] for g in games}, rows=[])


def run_all(seconds=30, memory_gib=2, output=RESULTS):
    games = manifest()
    document = _run_metadata(games)
    for game in games:
        for backend in BACKENDS:
            row = measure_case(game, backend, seconds, memory_gib)
            document['rows'].append(row)
            check_agreement(document['rows'])
            output.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
            print(f"n={game['n']} {backend}: {row['outcome']}, "
                  f"(k,t)=({row.get('k', '?')},{row.get('t', '?')}), "
                  f"{row['seconds']:.2f} s", flush=True)
    return document


def run_timeouts(seconds=60, memory_gib=2, output=None, baseline_path=None, jobs=1, stop_after_n=None):
    """Rerun just the baseline timeouts; resume completed cases in this output."""
    games = manifest()
    original = recorded_results()
    baseline_path = Path(baseline_path) if baseline_path is not None else RESULTS
    output = Path(output) if output is not None else TIMEOUT_RESULTS
    baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
    document = json.loads(output.read_text(encoding='utf-8')) if output.exists() else None
    if document is not None:
        if stop_after_n is None:
            stop_after_n = document.get('stop_after_n')
        else:
            assert stop_after_n == document.get('stop_after_n')
    if jobs < 1:
        raise ValueError('Use at least one worker')
    assert baseline['fixtures'] == original['fixtures']
    selected = [r for r in baseline['rows'] if r['outcome'] == 'timeout']
    planned_cases = len(selected)
    if stop_after_n is not None:
        if not 1 <= stop_after_n <= max(g['n'] for g in games):
            raise ValueError('The stopping instance must be in the manifest')
        selected = [r for r in selected if r['n'] <= stop_after_n]
    if selected and seconds <= max(r['timeout_seconds'] for r in selected):
        raise ValueError('Use a larger deadline than the baseline timeout')
    baseline_sha256 = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    expected = {(r['n'], r['backend']) for r in selected}
    if document is not None:
        assert document['baseline_sha256'] == baseline_sha256
        assert document['timeout_seconds'] == seconds
        assert document['memory_gib'] == memory_gib
        assert document.get('jobs', 1) == jobs
        assert document['fixtures'] == baseline['fixtures']
        done = {(r['n'], r['backend']) for r in document['rows']}
        assert len(done) == len(document['rows']) and done <= expected
    else:
        document = dict(_run_metadata(games), baseline_file=baseline_path.name,
                        baseline_sha256=baseline_sha256,
                        timeout_seconds=seconds, memory_gib=memory_gib,
                        jobs=jobs, cases=len(selected), planned_cases=planned_cases)
        if stop_after_n is not None:
            document['stop_after_n'] = stop_after_n
        done = set()

    def measure(old):
        print(f"Starting n={old['n']} {old['backend']} ({seconds:g} s)", flush=True)
        return measure_case(games[old['n'] - 1], old['backend'], seconds, memory_gib)

    def checkpoint():
        temporary = output.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
        temporary.replace(output)

    def record(row):
        document['rows'].append(row)
        check_agreement([*original['rows'], *baseline['rows'], *document['rows']])
        checkpoint()
        print(f"n={row['n']} {row['backend']}: {row['outcome']}, "
              f"(k,t)=({row.get('k', '?')},{row.get('t', '?')}), "
              f"{row['seconds']:.2f} s", flush=True)

    pending = [r for r in selected if (r['n'], r['backend']) not in done]
    if not pending and not output.exists():
        checkpoint()
    if jobs == 1:
        for old in pending:
            record(measure(old))
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(measure, old) for old in pending]
            for future in as_completed(futures):
                record(future.result())
    return document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('manifest', 'run', 'rerun-timeouts', 'collect', 'worker'))
    parser.add_argument('--seconds', type=float)
    parser.add_argument('--memory-gib', type=int, default=2)
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--stop-after-n', type=int)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.command == 'manifest':
        build_manifest()
    elif args.command == 'collect':
        collect_results(args.output or FINAL_RESULTS)
    elif args.command == 'worker':
        worker(json.load(sys.stdin))
    elif args.command == 'rerun-timeouts':
        run_timeouts(args.seconds if args.seconds is not None else 60,
                     args.memory_gib, args.output or TIMEOUT_RESULTS,
                     baseline_path=args.baseline, jobs=args.jobs, stop_after_n=args.stop_after_n)
    else:
        run_all(args.seconds if args.seconds is not None else 30,
                args.memory_gib, args.output or RESULTS)


if __name__ == '__main__':
    main()
