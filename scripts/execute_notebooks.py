#!/usr/bin/env python3
"""Execute the examples and save static outputs for GitHub."""

from html import escape
import json
import os
from pathlib import Path
import re
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace

import nbformat
from nbclient import NotebookClient
from jupyter_client import KernelManager
from jupyter_client.kernelspec import KernelSpecManager

from strahler.widgets import GameView, TreeView, _controls_html, _ascii

EXAMPLES = Path(__file__).resolve().parents[1] / 'jupyter-examples'
WIDGET_MIME = 'application/vnd.jupyter.widget-view+json'


def output_html(output, states):
    if output['output_type'] == 'error':
        raise ValueError(f"Notebook error: {output['ename']}: {output['evalue']}")
    data = output.get('data', {})
    if WIDGET_MIME in data:
        # Resolve containers from their final state, including Output's captured data.
        return widget_html(data[WIDGET_MIME]['model_id'], states)
    return (data.get('text/html') or data.get('image/svg+xml') or
            '<pre>' + escape(output.get('text', data.get('text/plain', ''))) + '</pre>')


def widget_html(model_id, states):
    state = states[model_id]
    kind = state.get('kind')
    if kind == 'game':
        return GameView._static_svg(SimpleNamespace(**state))
    if kind == 'tree':
        return TreeView._static_svg(SimpleNamespace(**state))
    if kind == 'controls':
        return _controls_html(state['data'])
    if state.get('_model_name') == 'OutputModel':
        return ''.join(output_html(output, states) for output in state['outputs'])
    if 'children' in state:
        children = ''.join(widget_html(child.removeprefix('IPY_MODEL_'), states)
                           for child in state['children'])
        return '<div>' + children + '</div>'
    text = str(state.get('description', ''))
    if 'value' in state:
        text += ': ' + str(state['value'])
    return '<pre>' + escape(text) + '</pre>'


def freeze_outputs(notebook, states):
    for state in states.values():
        for output in state.get('outputs', []):
            if output['output_type'] == 'error':
                output_html(output, states)
    for cell in notebook.cells:
        cell.metadata.pop('execution', None)
        for output in cell.get('outputs', []):
            if output['output_type'] == 'error':
                output_html(output, states)
            data = output.get('data', {})
            if WIDGET_MIME in data:
                if not (data.get('image/svg+xml') or data.get('text/html')):
                    data['text/html'] = output_html(output, states)
                # Saved model IDs are random and unusable without saved widget state.
                del data[WIDGET_MIME]
                output.get('metadata', {}).pop(WIDGET_MIME, None)
                data['text/plain'] = 'Static widget snapshot; rerun for interactive controls.'
            for mime in ('image/svg+xml', 'text/html'):
                if mime in data:
                    data[mime] = _ascii(data[mime])
            text = json.dumps(output, ensure_ascii=False)
            if re.search(r'0x[\da-fA-F]{6,}|/(?:home|Users|tmp|usr|opt|var)/|[A-Z]:\\', text):
                raise ValueError('Volatile output; change the cell to omit paths or memory addresses')
    notebook.metadata.pop('widgets', None)


def execute_notebook(path, kernels):
    notebook = nbformat.read(path, as_version=4)
    manager = KernelManager(kernel_name='strahler-examples', kernel_spec_manager=
                            KernelSpecManager(kernel_dirs=[str(kernels)]))
    client = NotebookClient(notebook, km=manager, timeout=60, allow_errors=False,
                            record_timing=False, resources={'metadata': {'path': str(path.parent)}})
    # Keep state only long enough to flatten containers; none is written to disk.
    client.execute(cleanup_kc=True, env=os.environ.copy())
    freeze_outputs(notebook, client.widget_state)
    temporary = path.with_suffix('.ipynb.tmp')
    try:
        nbformat.write(notebook, temporary)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    with TemporaryDirectory(prefix='strahler-notebooks-') as directory:
        kernels = Path(directory) / 'kernels'
        spec = kernels / 'strahler-examples'
        spec.mkdir(parents=True)
        (spec / 'kernel.json').write_text(json.dumps(dict(
            argv=[sys.executable, '-m', 'ipykernel_launcher', '-f', '{connection_file}'],
            display_name='strahler examples', language='python')), encoding='utf-8')
        for path in sorted(EXAMPLES.glob('*.ipynb')):
            execute_notebook(path, kernels)
            print(path.name)


if __name__ == '__main__':
    main()
