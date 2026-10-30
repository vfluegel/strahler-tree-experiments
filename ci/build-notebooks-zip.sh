#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 VERSION WHEELS_DIR OUTPUT_DIR" >&2
    exit 1
fi
repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
python3 - "$repo_root" "$@" <<'PY'
from pathlib import Path
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
with ZipFile(archive, 'w', compression=ZIP_DEFLATED) as bundle:
    for path in sorted(notebooks.rglob('*')):
        if path.is_file() and path != notebooks / 'requirements.txt':
            bundle.write(path, f'{name}/{path.relative_to(notebooks)}')
    for path in ('keiren/results.csv', 'KEIREN.md'):
        bundle.write(root / path, f'{name}/{path}')
    bundle.writestr(f'{name}/requirements.txt',
                    f'--find-links ./wheels\n--only-binary strahler\n'
                    f'strahler=={version}\n{requirements}')
    for wheel in sorted(names):
        bundle.write(wheels / wheel, f'{name}/wheels/{wheel}')
print(archive)
PY
