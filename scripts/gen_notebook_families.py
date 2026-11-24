#!/usr/bin/env python3
"""Check or rebuild the pinned CABP and ladder notebook fixtures."""

import argparse
import bz2
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import quote
from urllib.request import urlopen

from gen_keiren import open_member, remote_zip

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'jupyter-examples'))
from lexicographic_nester import bounds
from family_benchmarks import sha256


def fetch(url, path, expected):
    if not path.exists():
        with urlopen(quote(url, safe=':/='), timeout=60) as source, path.open('wb') as target:
            shutil.copyfileobj(source, target)
    if sha256(path) != expected:
        raise ValueError(f'Source checksum mismatch: {path.name}')
    return path


def extract(archive, member, destination):
    if not destination.exists():
        with remote_zip(archive) as source, open_member(source, source.getinfo(member)) as stream:
            destination.write_bytes(stream.read())


def cabp(game, document, destination, cache, knor):
    if knor is None:
        raise ValueError('Rebuilding CABP requires --knor PATH (see its manifest for the pinned source)')
    hoa = cache / game['ehoa_file']
    if game['source_in_v2026_repository']:
        fetch(game['ehoa_url'], hoa, game['ehoa_sha256'])
    elif not hoa.exists():
        original = cache / Path(game['original_member']).name
        extract(document['original_figshare_file'], game['original_member'], original)
        if sha256(original) != game['original_sha256']:
            raise ValueError('Original Keiren game checksum mismatch')
        converter = fetch(document['converter_url'], cache / 'pg2hoa.py', document['converter_sha256'])
        spec = importlib.util.spec_from_file_location('pinned_pg2hoa', converter)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with hoa.open('w') as target:
            module.ParityGame(str(original)).toHoa(target)
    if sha256(hoa) != game['ehoa_sha256']:
        raise ValueError('Reconstructed eHOA checksum mismatch')
    with tempfile.TemporaryDirectory(dir=cache) as temporary:
        pg = Path(temporary) / 'game.pg'
        with pg.open('wb') as target:
            subprocess.run([str(knor.resolve()), '--print-game', str(hoa.resolve())],
                           stdout=target, check=True)
        if sha256(pg) != game['pg_sha256']:
            raise ValueError('Knor conversion differs from the pinned fixture')
        if destination.suffix == '.bz2':
            with pg.open('rb') as source, bz2.open(destination, 'wb', compresslevel=9) as target:
                shutil.copyfileobj(source, target)
        else:
            shutil.copyfile(pg, destination)


def check(path, game):
    if sha256(path) != game['sha256']:
        raise ValueError(f'Fixture checksum mismatch: {path.name}')
    digest = hashlib.sha256()
    count = edges = 0
    priorities = set()
    opener = bz2.open if path.suffix == '.bz2' else open
    with opener(path, 'rb') as stream:
        for raw in stream:
            digest.update(raw)
            fields = raw.split(None, 4)
            if not fields or not fields[0].isdigit():
                continue
            # All supplied representations use consecutive external IDs.
            if int(fields[0]) != count:
                raise ValueError('Expected consecutive IDs beginning at zero')
            count += 1
            edges += fields[3].count(b',') + 1
            priorities.add(int(fields[1]))
    ordered = sorted(priorities)
    maximum = ordered[0] % 2
    for priority in ordered[1:]:
        maximum += 1
        maximum += maximum % 2 != priority % 2
    observed = dict(vertices=count, edges=edges, raw_priorities=ordered,
                    normalized_maximum=maximum, pg_sha256=digest.hexdigest(),
                    file_bytes=path.stat().st_size, **bounds(count, maximum))
    for key, value in observed.items():
        if game[key] != value:
            raise ValueError(f'{path.name}: metadata differs for {key}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--family', choices=('cabp', 'ladder', 'both'), default='both')
    parser.add_argument('--check', action='store_true', help='Check bundled files without any network access')
    parser.add_argument('--knor', type=Path)
    parser.add_argument('--cache', type=Path, default=ROOT / '.cache/notebook-family-sources')
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    for family in ('cabp', 'ladder') if args.family == 'both' else (args.family,):
        directory = ROOT / 'jupyter-examples' / family
        document = json.loads((directory / 'manifest.json').read_text())
        for game in document['games']:
            path = directory / game['file']
            if not path.exists() and not args.check:
                if family == 'ladder':
                    extract(document['original_figshare_file'], game['original_member'], path)
                else:
                    cabp(game, document, path, args.cache, args.knor)
            check(path, game)
            print(f'{game["id"]}: {game["vertices"]:,} vertices, checksums and bounds match')


if __name__ == '__main__':
    main()
