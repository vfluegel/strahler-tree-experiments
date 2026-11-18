"""Interleaved CLI timings with exact-output checks; large logs stay outside git.

Run from a release build. --samples excludes one warm-up per arm. Every timed
run verifies certificates normally; --no-verify is deliberately not added.
"""

import argparse
import bz2
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
MEASURE = """import json, resource, subprocess, sys, time
start = time.perf_counter()
try:
    child = subprocess.run(sys.argv[3:], capture_output=True, timeout=float(sys.argv[2]))
except subprocess.TimeoutExpired:
    sys.exit(124)
wall = time.perf_counter() - start
sys.stdout.buffer.write(child.stdout)
sys.stderr.buffer.write(child.stderr)
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
with open(sys.argv[1], 'w') as output:
    json.dump(dict(wall=wall, cpu=usage.ru_utime + usage.ru_stime,
                   peak_kib=int(usage.ru_maxrss / (1024 if sys.platform == 'darwin' else 1))), output)
sys.exit(child.returncode)
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    build = args.build.resolve()
    report = dict(recorded_utc=datetime.now(timezone.utc).isoformat(timespec='seconds'),
                  baseline='3d63d4ffb1fe49e916f162e490516b8e7951a569',
                  head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  machine=platform.platform(), cpu=platform.processor(),
                  compiler=subprocess.check_output(['cc', '--version'], text=True).splitlines()[0],
                  build_options=json.loads(subprocess.check_output(
                      ['meson', 'introspect', str(build), '--buildoptions'], text=True)),
                  executables={name: hashlib.sha256((build / name).read_bytes()).hexdigest()
                               for name in ('pg2adot', 'pg2pmdot')}, rows=[])
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        paths = {}
        for name in ('Nestern=3_compact', 'Nestern=4_compact', 'Nestern=3', 'Nestern=4'):
            path = temporary / (name + '.gm')
            path.write_bytes(bz2.decompress((ROOT / 'jupyter-examples/nester' / (name + '.gm.bz2')).read_bytes()))
            paths[name] = path
        paths['FairScheduler2'] = ROOT / 'examples/FairSchedulern=2_compact.gm'
        paths['tiny'] = temporary / 'tiny.gm'
        paths['tiny'].write_text('parity 1;\n0 0 0 0;\n1 1 1 1;\n')
        cases = []
        for name in paths:
            # Plain Nester4 is the notebook's expensive frontier. The full
            # 600-second experiment records it separately; benchmark Nester3.
            if name == 'Nestern=4':
                continue
            for algorithm in ('lifting', 'lifting-sym'):
                cases.append((name, 'pg2adot', ['--algorithm=' + algorithm,
                              '--search=lexicographic', '--iter-shave'],
                              ('scalar', 'fused', 'packed', 'dense', 'queue', 'dirty', 'byte')))
            pm_pair = ['--k=2', '--t=1'] if name == 'FairScheduler2' else ['--k=3', '--t=2']
            cases.append((name, 'pg2pmdot', ['--algorithm=strahler', *pm_pair],
                          ('scalar', 'packed', 'lookup', 'compare', 'both')))
        for name, tool, flags, backends in cases:
            rows = {backend: [] for backend in backends}
            expected = None
            for sample in range(args.samples + 1):
                order = backends if sample % 2 == 0 else tuple(reversed(backends))
                for backend in order:
                    env = dict(os.environ)
                    env['STRAHLER_AD_BACKEND' if tool == 'pg2adot' else 'STRAHLER_PM_BACKEND'] = backend
                    env.pop('STRAHLER_BACKEND_REPORT', None)
                    usage = temporary / 'usage.txt'
                    command = [str(build / tool), *flags, '--stats', '--format=json', str(paths[name])]
                    timed = [sys.executable, '-c', MEASURE, str(usage), str(args.timeout), *command]
                    process = subprocess.run(timed, env=env, capture_output=True, check=True,
                                             timeout=args.timeout + 5)
                    document = json.loads(process.stdout)
                    document.pop('version', None)
                    digest = hashlib.sha256(json.dumps(document, sort_keys=True).encode() + process.stderr).hexdigest()
                    if expected is None:
                        expected = digest
                    assert digest == expected, (name, flags, backend)
                    if sample:
                        rows[backend].append(json.loads(usage.read_text()))
            # Collect dispatch/layout counters outside the timed measurements.
            for backend in backends:
                env = {**os.environ, 'STRAHLER_BACKEND_REPORT': '1',
                       ('STRAHLER_AD_BACKEND' if tool == 'pg2adot' else 'STRAHLER_PM_BACKEND'): backend}
                diagnostic = subprocess.run([str(build / tool), *flags, '--format=json', str(paths[name])],
                                            env=env, capture_output=True, check=True, timeout=args.timeout)
                samples = rows[backend]
                row = dict(case=name, tool=tool, flags=flags, backend=backend, samples=samples,
                           median_wall=statistics.median(s['wall'] for s in samples),
                           spread_wall=max(s['wall'] for s in samples) - min(s['wall'] for s in samples),
                           median_cpu=statistics.median(s['cpu'] for s in samples),
                           peak_kib=max(s['peak_kib'] for s in samples), digest=expected,
                           diagnostics=diagnostic.stderr.decode().splitlines())
                report['rows'].append(row)
                print(f'{name} {flags[0]} {backend}: {row["median_wall"]:.4f}s', flush=True)
            args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
