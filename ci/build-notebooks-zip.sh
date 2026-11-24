#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 VERSION WHEELS_DIR OUTPUT_DIR" >&2
    exit 1
fi
repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
python3 - "$repo_root" "$@" <<'PY'
from pathlib import Path
import json
import re
import sys
from zipfile import ZIP_DEFLATED, ZipFile

root, version, wheels, output = sys.argv[1:]
if not re.fullmatch(r'[0-9][0-9A-Za-z._+-]*', version):
    sys.exit('Invalid version')
root, wheels, output = Path(root), Path(wheels), Path(output)
names = {f'strahler-{version}-py3-none-{platform}.whl' for platform in
         ('manylinux_2_28_x86_64', 'macosx_11_0_arm64')}
if {p.name for p in wheels.glob('*.whl')} != names:
    sys.exit(f'Expected exactly these two wheels: {sorted(names)}')
notebooks = root / 'jupyter-examples'
requirements = (notebooks / 'requirements.txt').read_text()
output.mkdir(parents=True, exist_ok=True)
name = f'strahler-notebooks-{version}'
archive = output / f'{name}.zip'
shared = ('keiren/results.csv', 'KEIREN.md', 'HOST-ORDER.md', 'docs/incremental-t.md', 'docs/host-monotonicity.md',
          'experimental/host-order/sizes.csv', 'experimental/host-order/summary.json',
          'scripts/incremental_t.py', 'scripts/find_disagreements.py',
          'examples/README.md', 'examples/ad-nonmonotone.pg', 'examples/strpm-nonmonotone.pg')
with ZipFile(archive, 'w', compression=ZIP_DEFLATED) as bundle:
    for path in sorted(notebooks.rglob('*')):
        if path.is_file() and path != notebooks / 'requirements.txt':
            destination = f'{name}/{path.relative_to(notebooks)}'
            if path.suffix == '.ipynb':
                document = json.loads(path.read_text(encoding='utf-8'))
                changed = False
                for cell in document.get('cells', []):
                    if cell['cell_type'] != 'markdown':
                        continue
                    source = ''.join(cell['source'])
                    rewritten = source
                    for asset in shared:
                        rewritten = re.sub(r'\]\(\.\./' + re.escape(asset) + r'(?=[)#])',
                                           '](' + asset, rewritten)
                    if rewritten != source:
                        cell['source'] = (rewritten.splitlines(keepends=True)
                                          if isinstance(cell['source'], list) else rewritten)
                        changed = True
                if changed:
                    bundle.writestr(destination, json.dumps(document, indent=1, ensure_ascii=False) + '\n')
                    continue
            bundle.write(path, destination)
    for path in shared:
        bundle.write(root / path, f'{name}/{path}')
    bundle.writestr(f'{name}/requirements.txt',
                    f'--find-links ./wheels\n--only-binary strahler\n'
                    f'strahler=={version}\n{requirements}')
    for wheel in sorted(names):
        bundle.write(wheels / wheel, f'{name}/wheels/{wheel}')
print(archive)
PY
