#!/usr/bin/env python3
"""Require the saved solver corpus to catch each deliberate backend bug."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'fuzz-solvers-replay'


def build_and_replay(source, build):
    subprocess.run(['ninja', '-C', str(build), '-j1', TARGET], check=True, capture_output=True)
    return subprocess.run([str(build / TARGET), str(source / 'tests/fuzz/corpus/solvers')],
                          capture_output=True, text=True, timeout=600)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', help='run the mutant with this name')
    args = parser.parse_args()
    mutants = json.loads((ROOT / 'tests/fuzz/mutants.json').read_text(encoding='utf-8'))
    if args.only:
        mutants = [m for m in mutants if m['name'] == args.only]
    survivors = []
    with tempfile.TemporaryDirectory(prefix='strahler-mutants-') as directory:
        source = Path(directory) / 'source'
        shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns('build*', '.git', '.venv', '.cache', '.mesonpy-*'))
        build = Path(directory) / 'build'
        # The copy has no Git metadata to derive a version from.
        subprocess.run(['meson', 'setup', str(build), str(source), '--buildtype=debug',
                        '-Dlenstree=disabled'], check=True, capture_output=True,
                       env={**os.environ, 'STRAHLER_VERSION': '0.0.0'})
        baseline = build_and_replay(source, build)
        if baseline.returncode != 0:
            sys.exit(f'The unmodified corpus failed:\n{baseline.stderr[-2000:]}')
        for mutant in mutants:
            path = source / mutant['file']
            original = path.read_text(encoding='utf-8')
            if original.count(mutant['old']) != 1:
                sys.exit(f"{mutant['name']}: expected one match in {mutant['file']}")
            path.write_text(original.replace(mutant['old'], mutant['new']), encoding='utf-8')
            try:
                result = build_and_replay(source, build)
            finally:
                path.write_text(original, encoding='utf-8')
            caught = result.returncode != 0
            print(f"{mutant['name']}: {'caught' if caught else 'SURVIVED'}", flush=True)
            if not caught:
                survivors.append(mutant['name'])
    if survivors:
        sys.exit('Survivors: ' + ', '.join(survivors))


if __name__ == '__main__':
    main()
