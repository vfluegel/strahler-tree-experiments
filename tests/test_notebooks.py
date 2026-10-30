"""Check saved outputs and execute copies with this interpreter's kernel."""

import json
import os
from pathlib import Path
import re
import sys
from time import perf_counter

import pytest

EXAMPLES = Path(__file__).resolve().parents[1] / 'jupyter-examples'
NOTEBOOKS = sorted(EXAMPLES.glob('*.ipynb'))


@pytest.fixture(autouse=True)
def examples_path(monkeypatch):
    monkeypatch.syspath_prepend(str(EXAMPLES))


@pytest.fixture
def executor():
    import importlib.util
    pytest.importorskip('nbclient')
    pytest.importorskip('anywidget')
    spec = importlib.util.spec_from_file_location('execute_notebooks',
                EXAMPLES.parent / 'scripts/execute_notebooks.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_save_is_deterministic_without_a_kernel(executor, tmp_path, monkeypatch):
    import nbformat
    import strahler
    from ipywidgets import VBox, Output, IntSlider, Textarea
    from strahler.widgets import Stepper

    path = tmp_path / 'example.ipynb'
    notebook = nbformat.v4.new_notebook(cells=[nbformat.v4.new_code_cell('example()')])
    notebook.metadata.widgets = {'stale': 'state'}
    notebook.cells[0].metadata.execution = {'old': 'timing'}
    nbformat.write(notebook, path)
    kernels = tmp_path / 'kernels'
    spec = kernels / 'strahler-examples'
    spec.mkdir(parents=True)
    argv = [sys.executable, '-m', 'ipykernel_launcher', '-f', '{connection_file}']
    (spec / 'kernel.json').write_text(json.dumps(dict(argv=argv, display_name='test', language='python')))
    model_ids = []

    class Client:
        def __init__(self, notebook, **kwargs):
            self.notebook = notebook
            assert kwargs['timeout'] == 60 and kwargs['record_timing'] is False
            assert kwargs['allow_errors'] is False
            assert kwargs['resources']['metadata']['path'] == str(tmp_path)
            assert kwargs['km'].kernel_spec.argv == argv

        def execute(self, **kwargs):
            assert kwargs['cleanup_kc'] is True and kwargs['env']['PATH'] == os.environ['PATH']
            with strahler.Game.from_text('0 2 0 0,1 "<a&b>"; 1 1 1 1;') as game:
                stepper = Stepper(strahler.trace(game, 'zielonka-sym'))
                stepper.next('check')
                captured = Output(outputs=[nbformat.v4.new_output('stream', name='stdout', text='<captured>')])
                panel = VBox([stepper, stepper.call_tree(), captured,
                              IntSlider(value=4, description='k'), Textarea(value='<input>')])
                self.widget_state = {}
                def collect(widget):
                    self.widget_state[widget.model_id] = widget.get_state()
                    for child in getattr(widget, 'children', ()):
                        collect(child)
                collect(panel)
                model_ids.append(panel.model_id)
                self.notebook.cells[0].execution_count = 1
                self.notebook.cells[0].outputs = [nbformat.v4.new_output(
                    'display_data', data=panel._repr_mimebundle_())]

    monkeypatch.setattr(executor, 'NotebookClient', Client)
    executor.execute_notebook(path, kernels)
    first = path.read_bytes()
    saved = nbformat.read(path, as_version=4)
    assert 'widgets' not in saved.metadata and 'execution' not in saved.cells[0].metadata
    assert saved.cells[0].execution_count == 1
    data = saved.cells[0].outputs[0].data
    assert executor.WIDGET_MIME not in data
    html = data['text/html']
    assert html.count('<svg') == 4
    for text in ('&lt;a&amp;b&gt;', '&lt;captured&gt;', 'k: 4', '&lt;input&gt;', 'algorithm claim:', 'Independent:'):
        assert text in html
    executor.execute_notebook(path, kernels)
    assert model_ids[0] != model_ids[1]
    assert first == path.read_bytes()
    assert not path.with_suffix('.ipynb.tmp').exists()

    def fail(*args, **kwargs):
        raise RuntimeError('execution failed')
    monkeypatch.setattr(Client, 'execute', fail)
    with pytest.raises(RuntimeError, match='execution failed'):
        executor.execute_notebook(path, kernels)
    assert first == path.read_bytes()


def test_freeze_rejects_errors_and_volatile_output(executor):
    import nbformat
    error = nbformat.v4.new_output('error', ename='ValueError', evalue='bad input', traceback=[])
    notebook = nbformat.v4.new_notebook(cells=[nbformat.v4.new_code_cell('')])
    notebook.cells[0].outputs = [error]
    with pytest.raises(ValueError, match='Notebook error'):
        executor.freeze_outputs(notebook, {})
    notebook.cells[0].outputs = []
    with pytest.raises(ValueError, match='Notebook error'):
        executor.freeze_outputs(notebook, {'output-widget': {'outputs': [error]}})
    for text in ('/home/someone/build', '<Game at 0x12345678>'):
        notebook.cells[0].outputs = [nbformat.v4.new_output('stream', name='stdout', text=text)]
        with pytest.raises(ValueError, match='Volatile output'):
            executor.freeze_outputs(notebook, {})


def test_seven_notebooks_and_version_mismatch(monkeypatch, capsys):
    assert len(NOTEBOOKS) == 7
    import importlib.metadata
    from types import SimpleNamespace
    monkeypatch.setitem(sys.modules, 'strahler', SimpleNamespace(version=lambda: '0.0.1'))
    monkeypatch.setattr(importlib.metadata, 'version', lambda name: '0.0.2')
    setup = ''.join(json.loads(NOTEBOOKS[0].read_text())['cells'][0]['source'])
    with pytest.raises(RuntimeError, match='version mismatch'):
        exec(compile(setup, str(NOTEBOOKS[0]), 'exec'), {})
    assert not capsys.readouterr().out


@pytest.mark.parametrize('path', NOTEBOOKS, ids=lambda path: path.name)
def test_saved_outputs(path):
    from html import unescape
    notebook = json.loads(path.read_text(encoding='utf-8'))
    assert 'widgets' not in notebook['metadata']
    counts = []
    for cell in notebook['cells']:
        if cell['cell_type'] == 'code':
            counts.append(cell['execution_count'])
            assert isinstance(cell['execution_count'], int) and cell['execution_count'] > 0, (
                f'{path}: rerun scripts/execute_notebooks.py')
            assert 'execution' not in cell['metadata']
            assert not list(errors(cell)), path
            text = unescape(json.dumps(cell['outputs']))
            assert 'residual' not in text, path
            if '<th>dominion</th>' in text:
                assert '<th>dominion</th><th>won</th>' in text, path
                assert text.count('<th>dominion</th>') == text.count(
                    "dominion: vertices covered by this player's attractor decomposition (its tree); "
                    "won: every vertex this player wins, including vertices removed by preprocessing (+pre) "
                    "or earlier shaving rounds (+iter); unresolved: vertices neither player's search certified."), path
            for output in cell['outputs']:
                data = output.get('data', {})
                if ('application/vnd.jupyter.widget-view+json' in data or
                    ''.join(data.get('text/plain', [])).startswith('Static widget snapshot;')):
                    assert data.get('image/svg+xml') or data.get('text/html'), path
                for mime in ('image/svg+xml', 'text/html'):
                    if mime in data:
                        assert ''.join(data[mime]).isascii(), (path, mime)
                if output['output_type'] == 'execute_result':
                    assert output['execution_count'] == cell['execution_count']
            compile(''.join(cell['source']), str(path), 'exec')
    assert counts and all(a < b for a, b in zip(counts, counts[1:])), path
    assert any(cell.get('outputs') for cell in notebook['cells']), path
    first = ''.join(notebook['cells'][0]['source'])
    assert 'import strahler' in first and 'package_version' in first and 'strahler.version()' in first


@pytest.mark.parametrize('path', NOTEBOOKS, ids=lambda path: path.name)
def test_saved_outputs_hide_versions(path):
    notebook = json.loads(path.read_text(encoding='utf-8'))
    for cell in notebook['cells']:
        text = json.dumps(cell.get('outputs', []))
        assert not re.search(r'(?:strahler(?: package)?|libstrahler)\s+[0-9]+\.[0-9]+', text), path
    first = notebook['cells'][0]
    text = ''.join(''.join(output.get('text', [])) for output in first['outputs'])
    assert text == 'strahler package and library versions match\n'


def errors(value):
    if isinstance(value, dict):
        if value.get('output_type') == 'error':
            yield value
        for child in value.values():
            yield from errors(child)
    elif isinstance(value, list):
        for child in value:
            yield from errors(child)


@pytest.mark.parametrize('path', NOTEBOOKS, ids=lambda path: path.name)
def test_execute_notebook(path, tmp_path):
    nbformat = pytest.importorskip('nbformat', reason='nbformat is needed for headless notebooks')
    nbclient = pytest.importorskip('nbclient', reason='nbclient is needed for headless notebooks')
    pytest.importorskip('ipykernel')
    pytest.importorskip('anywidget')
    from jupyter_client import KernelManager
    from jupyter_client.kernelspec import KernelSpecManager

    kernels = tmp_path / 'kernels'
    spec = kernels / 'strahler-test'
    spec.mkdir(parents=True)
    (spec / 'kernel.json').write_text(json.dumps(dict(
        argv=[sys.executable, '-m', 'ipykernel_launcher', '-f', '{connection_file}'],
        display_name='strahler test venv', language='python')))
    manager = KernelManager(kernel_name='strahler-test',
                            kernel_spec_manager=KernelSpecManager(kernel_dirs=[str(kernels)]))
    notebook = nbformat.read(path, as_version=4)
    client = nbclient.NotebookClient(notebook, km=manager, timeout=60, allow_errors=False,
                                    resources={'metadata': {'path': str(EXAMPLES)}})
    started = perf_counter()
    # Test kernels inherit STRAHLER_LIB/PYTHONPATH from Meson or the wheel job.
    client.execute(cleanup_kc=True, env={**os.environ, 'IPYTHONDIR': str(tmp_path / 'ipython'),
                        'JUPYTER_CONFIG_DIR': str(tmp_path / 'jupyter')})
    elapsed = perf_counter() - started
    assert not list(errors(notebook)), list(errors(notebook))
    print(f'{path.name}: {elapsed:.2f}s')


def test_freeze_makes_all_static_mime_ascii(executor):
    import nbformat
    from html import unescape
    notebook = nbformat.v4.new_notebook(cells=[nbformat.v4.new_code_cell('')])
    data = {'image/svg+xml': '<svg>ε</svg>', 'text/html': '<p>ε μ</p>'}
    notebook.cells[0].outputs = [nbformat.v4.new_output('display_data', data=data)]
    executor.freeze_outputs(notebook, {})
    for mime, text in notebook.cells[0].outputs[0].data.items():
        assert text.isascii() and 'ε' in unescape(text)


def test_metrics_table_counts_all_wins(monkeypatch, capsys):
    import strahler
    import notebook_support as support

    result = strahler.Result(json.dumps(dict(
        trees=[None, None], regions=[[0], []], initial_won=[[1], [10]],
        shaves=[dict(attracted=[[2, 3], [11]]), dict(attracted=[[3, 4], [12]])],
        complete=False, unresolved=[20],
        metrics=dict(trees=[dict(nodes=1, leaves=1, height=1, strahler=1),
                            dict(nodes=0, leaves=0, height=0, strahler=0)]))))
    assert support.won_vertices(result, 0) == {0, 1, 2, 3, 4}
    assert support.won_vertices(result, 1) == {10, 11, 12}
    tables = []
    monkeypatch.setattr(support, 'table', lambda headers, rows: tables.append((headers, rows)))
    support.metrics_table({'partial': result})
    headers, rows = tables[0]
    assert headers == ['backend', 'player', 'nodes', 'leaves', 'height', 'Strahler',
                       'dominion', 'won', 'complete', 'unresolved']
    assert rows == [['partial', 'Even', 1, 1, 1, 1, 1, 5, False, 1],
                    ['partial', 'Odd', 0, 0, 0, 0, 0, 3, False, 1]]
    assert capsys.readouterr().out == (
        "dominion: vertices covered by this player's attractor decomposition (its tree); "
        "won: every vertex this player wins, including vertices removed by preprocessing (+pre) "
        "or earlier shaving rounds (+iter); unresolved: vertices neither player's search certified.\n")
    assert result.data['shaves'][1]['attracted'][0] == [3, 4]


def test_notebook07_helpers():
    import strahler
    import runpy
    helper = runpy.run_path(str(EXAMPLES / 'otj22.py'))
    hk, count_events = helper['hk'], helper['count_events']
    from symmetric_lifting import nester_formula, historical_maxima, history, _strahler, KARY_MEASUREMENTS
    assert nester_formula(2) == 'nu X_1. mu X_2. (q_1 | <a>(X_1 & (q_2 | <a>X_2)))'
    assert nester_formula(3).startswith('nu X_1. mu X_2. nu X_3.')
    with pytest.raises(ValueError):
        nester_formula(0)
    counts, complete = count_events(hk(4), 'lifting-sym', kary=2)
    row = next(r for r in KARY_MEASUREMENTS['rows'] if r['k'] == 4 and r['algorithm'] == 'lifting-sym' and r['kary'] == 2)
    assert complete and counts == {key: row[key] for key in counts}
    assert historical_maxima() and {r['solver_build'] for r in history()}
    assert len(list((EXAMPLES / 'nester').glob('*.gm.bz2'))) == 8
    assert _strahler({'children': [{'node': {}}, {'node': {}}]}) == 2


def test_worker_deadline_keeps_the_last_finished_attempt(monkeypatch):
    import subprocess
    import symmetric_lifting as experiment
    def expire(*args, **kwargs):
        raise subprocess.TimeoutExpired('worker', 1, output=b'{"outcome":"partial","strahler":[1,0],"parameters":[{"H":3,"K":1,"t":2}]}\n{"unfinished"')
    monkeypatch.setattr(experiment.subprocess, 'run', expire)
    row = experiment.bounded_worker(dict(kind='nester'), 1)
    assert row['outcome'] == 'timeout' and row['strahler'] == [1, 0]
    assert row['stopped_by'] == '1 s deadline'
    assert row['parameters'] == [{'H': 3, 'K': 1, 't': 2}]


def test_nester_history_sources(tmp_path, monkeypatch, capsys):
    import symmetric_lifting as experiment
    examples = tmp_path / 'jupyter-examples'
    (examples / 'nester').mkdir(parents=True)
    extract = (EXAMPLES / 'nester/history.csv').read_bytes()
    (examples / 'nester/history.csv').write_bytes(extract)
    monkeypatch.setattr(experiment, 'HERE', examples)
    fallback = experiment.history()
    assert 'Full keiren/results.csv absent' in capsys.readouterr().out
    # The release zip and repository layouts both supply the full CSV.
    for base in (examples, tmp_path):
        (base / 'keiren').mkdir()
        (base / 'keiren/results.csv').write_bytes(extract + b'pgsolver,pgsolver/games/other.gm\n')
        assert experiment.history() == fallback
        assert not capsys.readouterr().out


@pytest.mark.parametrize('backend', ['lifting-adaptive', 'lifting', 'lifting-sym', 'zielonka'])
def test_nester_uses_untraced_solver_parameters(backend, monkeypatch, capsys):
    import resource
    from types import SimpleNamespace
    import strahler
    import symmetric_lifting as experiment
    calls = []
    parameters = [{'H': 9, 'K': 2, 't': 7}, {'H': 8, 'K': 2, 't': 6}]
    def solve(game, algorithm, **options):
        calls.append((algorithm, options))
        return SimpleNamespace(data=dict(complete=True, parameters=parameters,
            metrics=dict(trees=[dict(strahler=2), dict(strahler=1)]), trees=[None, None]))
    monkeypatch.setattr(strahler, 'decompose', solve)
    monkeypatch.setattr(resource, 'setrlimit', lambda *args: None)
    experiment._worker(dict(kind='nester', game='Nestern=1.gm.bz2', backend=backend))
    row = json.loads(capsys.readouterr().out)
    assert row['parameters'] == parameters and row['strahler'] == [2, 1]
    options = (dict(adaptive_t=True, tree_k=4, max_host_leaves=4096) if backend == 'lifting-adaptive'
               else dict(tree_k=1) if backend in ('lifting', 'lifting-sym') else {})
    assert calls == [('lifting' if backend == 'lifting-adaptive' else backend, options)]
