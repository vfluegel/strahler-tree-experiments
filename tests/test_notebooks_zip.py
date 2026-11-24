"""Check the release layout without building native code or fetching packages."""

from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest
from zipfile import ZipFile


class NotebooksZip(unittest.TestCase):
    def test_contents_and_rejected_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'ci').mkdir()
            script = root / 'ci/build-notebooks-zip.sh'
            shutil.copyfile(Path(__file__).resolve().parents[1] / 'ci' / script.name, script)
            notebooks = root / 'jupyter-examples'
            (notebooks / 'data').mkdir(parents=True)
            (notebooks / 'nester').mkdir()
            (notebooks / 'cabp').mkdir()
            (notebooks / 'ladder').mkdir()
            (notebooks / 'recursiveladder').mkdir()
            files = {'README.md': '# Notebooks\n', 'requirements.txt': 'jupyterlab\n',
                     'example.ipynb': '{}', '07-symmetric-lifting-kary-nester.ipynb': '{}',
                     '08-lexicographic-adaptive-nester.ipynb': '{}',
                     '09-adaptive-cabp-swp.ipynb': json.dumps({'cells': [
                         {'cell_type': 'markdown', 'source': ['See [host sizes](../HOST-ORDER.md).\n',
                             '[Proof](../docs/host-monotonicity.md#constructing-the-embedding).\n',
                             '[Results](../keiren/results.csv).']},
                         {'cell_type': 'code', 'source': ['text = "[example](../HOST-ORDER.md)"']}]}),
                     '10-adaptive-ladder.ipynb': '{}',
                     '11-adaptive-recursiveladder.ipynb': '{}',
                     'family_benchmarks.py': '# bounded streaming collector\n',
                     'cabp/manifest.json': '{}', 'cabp/results-60s.json': '{}',
                     'ladder/laddergame-100.gm.bz2': 'ladder fixture',
                     'recursiveladder/recursiveladder-100.gm.bz2': 'recursive ladder fixture',
                     'symmetric_lifting.py': '# measurement helper\n',
                     'lexicographic_nester.py': '# lexicographic experiment helper\n',
                     'nester/Nestern=1.gm.bz2': 'compressed fixture',
                     'nester/history.csv': 'game,backend\n', 'data/game.pg': '0 0 0 0;',
                     'nester/Nestern=7.gm.bz2': 'large plain fixture',
                     'nester/plain-manifest.json': '{}',
                     'nester/lexicographic-results.json': '{}',
                     'nester/lexicographic-timeouts-60s.json': '{}',
                     'nester/lexicographic-timeouts-300s.json': '{}',
                     'nester/lexicographic-results-300s.json': '{}',
                     'nester/lexicographic-symmetric-300s.json': '{}',
                     'nester/lexicographic-timeouts-600s.json': '{}',
                     'nester/lexicographic-results-600s.json': '{}',
                     'nester/lexicographic-repeat-shaving-600s.json': '{}',
                     'nester/lexicographic-results-repeat-shaving-600s.json': '{}',
                     'nester/lexicographic-simd-600s.json': '{}',
                     'nester/lexicographic-results-simd-600s.json': '{}',
                     'data/requirements.txt': '# helper data\n'}
            for name, content in files.items():
                (notebooks / name).write_text(content)
            # The release must contain the data behind a checkout symlink.
            anchor = root / 'cabp-anchor.pg'
            anchor.write_text('0 0 0 0;\n')
            (notebooks / 'cabp/anchor.pg').symlink_to('../../cabp-anchor.pg')
            files['cabp/anchor.pg'] = anchor.read_text()
            data = {'keiren/results.csv': 'archive,game,backend\nmlsolver,Nestern=1.gm,lifting\n',
                    'KEIREN.md': '# Column definitions\n',
                    'HOST-ORDER.md': '# Host sizes\n\n[Counts](experimental/host-order/sizes.csv)\n',
                    'docs/incremental-t.md': '# Technical notes\n',
                    'docs/host-monotonicity.md': '# Embedding bound\n',
                    'scripts/incremental_t.py': '# Embedding witness\n',
                    'scripts/find_disagreements.py': '# Research helper\n',
                    'examples/README.md': '# Counterexamples\n',
                    'examples/ad-nonmonotone.pg': '0 0 0 0;\n',
                    'examples/strpm-nonmonotone.pg': '0 0 0 0;\n',
                    'experimental/host-order/sizes.csv': 'h,k,t,leaves\n1,1,0,1\n',
                    'experimental/host-order/summary.json': '{}'}
            for name, content in data.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
            wheels = root / 'wheels'
            wheels.mkdir()
            names = [f'strahler-0.4-py3-none-{platform}.whl' for platform in
                     ('manylinux_2_28_x86_64', 'macosx_11_0_arm64')]
            for name in names:
                (wheels / name).write_bytes(b'wheel fixture')
            command = ['sh', str(script), '0.4', str(wheels), str(root / 'out')]
            subprocess.run(command, check=True, capture_output=True)
            prefix = 'strahler-notebooks-0.4/'
            with ZipFile(root / 'out/strahler-notebooks-0.4.zip') as bundle:
                self.assertEqual(set(bundle.namelist()),
                                 {prefix + name for name in files.keys() | data.keys()} |
                                 {prefix + 'wheels/' + name for name in names})
                for name, content in files.items():
                    if name == '09-adaptive-cabp-swp.ipynb':
                        packaged = json.loads(bundle.read(prefix + name))
                        self.assertEqual(packaged['cells'][0]['source'],
                                         ['See [host sizes](HOST-ORDER.md).\n',
                                          '[Proof](docs/host-monotonicity.md#constructing-the-embedding).\n',
                                          '[Results](keiren/results.csv).'])
                        self.assertEqual(packaged['cells'][1]['source'],
                                         ['text = "[example](../HOST-ORDER.md)"'])
                    elif name != 'requirements.txt':
                        self.assertEqual(bundle.read(prefix + name).decode(), content)
                for name, content in data.items():
                    self.assertEqual(bundle.read(prefix + name).decode(), content)
                self.assertEqual(bundle.read(prefix + 'requirements.txt').decode(),
                                 '--find-links ./wheels\n--only-binary strahler\n'
                                 'strahler==0.4\njupyterlab\n')
            for version in ('../escape', '0.5'):
                with self.subTest(version=version):
                    rejected = [*command[:2], version, *command[3:]]
                    self.assertNotEqual(subprocess.run(rejected, capture_output=True).returncode, 0)
            extra = wheels / 'strahler-0.4-py3-none-win_amd64.whl'
            extra.write_bytes(b'unsupported wheel fixture')
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            extra.unlink()
            (wheels / names[0]).unlink()
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
