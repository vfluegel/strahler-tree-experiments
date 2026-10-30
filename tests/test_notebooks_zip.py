"""Check the release layout without building native code or fetching packages."""

from pathlib import Path
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
            files = {'README.md': '# Notebooks\n', 'requirements.txt': 'jupyterlab\n',
                     'example.ipynb': '{}', '07-symmetric-lifting-kary-nester.ipynb': '{}',
                     'symmetric_lifting.py': '# measurement helper\n',
                     'nester/Nestern=1.gm.bz2': 'compressed fixture',
                     'nester/history.csv': 'game,backend\n', 'data/game.pg': '0 0 0 0;',
                     'data/requirements.txt': '# helper data\n'}
            for name, content in files.items():
                (notebooks / name).write_text(content)
            data = {'keiren/results.csv': 'archive,game,backend\nmlsolver,Nestern=1.gm,lifting\n',
                    'KEIREN.md': '# Column definitions\n'}
            (root / 'keiren').mkdir()
            for name, content in data.items():
                (root / name).write_text(content)
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
                    if name != 'requirements.txt':
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
