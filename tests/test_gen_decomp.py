#!/usr/bin/env python3
"""Small wrapper checks, including a real two-pass generation when built."""

from contextlib import redirect_stdout
from hashlib import sha256
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tracemalloc
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import gen_decomp as decomp  # noqa: E402

BUILD_DIR = (Path(sys.argv.pop(1)).resolve()
             if len(sys.argv) > 1 and not sys.argv[1].startswith("-")
             else ROOT / "build")
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108000000003a7e9b55"
    "0000000a49444154789c636000000002000148afa4710000000049454e44ae426082")


def fake_dot(directory):
    program = directory / "dot"
    program.write_text(
        f"#!{sys.executable}\n"
        "import sys\n"
        "from pathlib import Path\n"
        "assert sys.argv[1:] == ['-Tpng']\n"
        "with Path(__file__).with_suffix('.calls').open('ab') as calls:\n"
        "    calls.write(sys.stdin.buffer.read() + b'\\0')\n"
        f"sys.stdout.buffer.write({PNG!r})\n")
    program.chmod(0o755)
    return program


class PngTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.root = self.directory / "repo"
        self.generated = self.directory / "generated"
        self.path = Path("decomp/game/tree.dot")
        (self.generated / self.path.parent).mkdir(parents=True)
        self.dot = b"digraph { a -> b }\n"
        (self.generated / self.path).write_bytes(self.dot)
        self.program = fake_dot(self.directory)
        self.calls = self.program.with_suffix(".calls")
        self.png = self.root / self.path.with_suffix(".png")
        self.stdout = redirect_stdout(io.StringIO())
        self.stdout.__enter__()
        self.addCleanup(self.stdout.__exit__, None, None, None)

    def test_stamp_and_reuse_without_comparing_png_bytes(self):
        decomp.sync_outputs(self.root, self.generated, False, self.program)
        png = self.png.read_bytes()
        digest = sha256(self.dot).hexdigest()
        chunk = b"tEXtdot-sha256\0" + digest.encode("ascii")
        end = 37 + len(chunk)
        self.assertEqual(png[:33], PNG[:33])
        self.assertEqual(int.from_bytes(png[33:37], "big"), len(chunk) - 4)
        self.assertEqual(png[37:end], chunk)
        self.assertEqual(int.from_bytes(png[end:end + 4], "big"),
                         zlib.crc32(chunk))
        self.assertEqual(png[end + 4:], PNG[33:])
        self.assertEqual(decomp.png_dot_hash(png), digest)
        self.assertEqual(self.calls.read_bytes(), self.dot + b"\0")

        extra = b"tEXtComment\0another Graphviz version"
        png = (png[:-12] + (len(extra) - 4).to_bytes(4, "big") + extra
               + zlib.crc32(extra).to_bytes(4, "big") + png[-12:])
        self.png.write_bytes(png)
        modified = self.png.stat().st_mtime_ns
        missing_dot = self.directory / "missing-dot"
        self.assertFalse(decomp.sync_outputs(
            self.root, self.generated, True, missing_dot))
        decomp.sync_outputs(self.root, self.generated, False, missing_dot)
        self.assertEqual(self.png.read_bytes(), png)
        self.assertEqual(self.png.stat().st_mtime_ns, modified)
        self.assertEqual(self.calls.read_bytes(), self.dot + b"\0")

    def test_dot_change_rerenders(self):
        decomp.sync_outputs(self.root, self.generated, False, self.program)
        changed = b"digraph { a -> c }\n"
        (self.generated / self.path).write_bytes(changed)
        decomp.sync_outputs(self.root, self.generated, False, self.program)
        self.assertEqual(self.calls.read_bytes(),
                         self.dot + b"\0" + changed + b"\0")
        self.assertEqual(decomp.png_dot_hash(self.png.read_bytes()),
                         sha256(changed).hexdigest())
        self.assertEqual((self.root / self.path).read_bytes(), changed)

    def test_truncated_png_is_stale_and_rerendered(self):
        decomp.sync_outputs(self.root, self.generated, False, self.program)
        png = self.png.read_bytes()
        for length in (120, len(png) - 12, len(png) - 1):
            with self.subTest(length=length):
                self.png.write_bytes(png[:length])
                self.assertIsNone(decomp.png_dot_hash(png[:length]))
                with patch.object(decomp, "render_png") as render:
                    self.assertTrue(decomp.sync_outputs(
                        self.root, self.generated, True, self.program))
                    render.assert_not_called()
                self.assertEqual(self.png.read_bytes(), png[:length])
                decomp.sync_outputs(self.root, self.generated, False,
                                    self.program)
                self.assertEqual(self.png.read_bytes(), png)
        self.assertEqual(self.calls.read_bytes(), (self.dot + b"\0") * 4)

    def test_hash_rejects_corrupt_chunks_and_missing_required_chunks(self):
        png = decomp.render_png(self.dot, sha256(self.dot).hexdigest(),
                                self.program)
        for offset in (29, 116, len(png) - 13, len(png) - 1):
            with self.subTest(offset=offset):
                corrupt = bytearray(png)
                corrupt[offset] ^= 1
                self.assertIsNone(decomp.png_dot_hash(corrupt))
        for corrupt in (png[:8] + png[33:], png[:120] + png[-12:],
                        png + b"trailing data"):
            with self.subTest(content=corrupt):
                self.assertIsNone(decomp.png_dot_hash(corrupt))

    def test_check_reports_stale_pngs_without_rendering_or_writing(self):
        decomp.sync_outputs(self.root, self.generated, False, self.program)
        mismatched = decomp.render_png(self.dot, "0" * 64, self.program)
        for content in (None, PNG, mismatched, mismatched[:50], b"bad PNG"):
            with self.subTest(content=content):
                if content is None:
                    self.png.unlink()
                else:
                    self.png.write_bytes(content)
                before = decomp.output_files(self.root)
                report = io.StringIO()
                with (redirect_stdout(report),
                      patch.object(decomp, "render_png") as render):
                    self.assertTrue(decomp.sync_outputs(
                        self.root, self.generated, True,
                        self.directory / "missing-dot"))
                    render.assert_not_called()
                self.assertIn("stale: decomp/game/tree.png", report.getvalue())
                self.assertEqual(decomp.output_files(self.root), before)

    def test_stale_png_removal(self):
        decomp.sync_outputs(self.root, self.generated, False, self.program)
        (self.generated / self.path).unlink()
        report = io.StringIO()
        with redirect_stdout(report):
            self.assertTrue(decomp.sync_outputs(
                self.root, self.generated, True, self.program))
        self.assertIn("stale: decomp/game/tree.png", report.getvalue())
        decomp.sync_outputs(self.root, self.generated, False,
                            self.directory / "missing-dot")
        self.assertFalse(self.png.exists())
        self.assertFalse((self.root / self.path).exists())

    def test_render_errors_preserve_outputs(self):
        decomp.sync_outputs(self.root, self.generated, False, self.program)
        (self.generated / self.path).write_bytes(b"digraph { changed }\n")
        before = decomp.output_files(self.root)
        with self.assertRaisesRegex(SystemExit, r"Graphviz.*--dot PATH"):
            decomp.sync_outputs(self.root, self.generated, False,
                                self.directory / "missing-dot")
        for result, message in (
            (subprocess.CalledProcessError(1, "dot", stderr=b"bad graph"),
             "Graphviz.*failed: bad graph"),
            (subprocess.CompletedProcess([], 0, b"not a PNG", b""),
             "invalid PNG"),
        ):
            with self.subTest(message=message):
                with patch.object(decomp.subprocess, "run") as run:
                    if isinstance(result, Exception):
                        run.side_effect = result
                    else:
                        run.return_value = result
                    with self.assertRaisesRegex(SystemExit, message):
                        decomp.sync_outputs(self.root, self.generated, False,
                                            self.program)
                self.assertEqual(decomp.output_files(self.root), before)

    def test_dot_argument_and_environment(self):
        for options, environment, expected in (
            ([], {}, "dot"),
            ([], {"STRAHLER_DOT": "env-dot"}, "env-dot"),
            (["--dot", "custom-dot"], {"STRAHLER_DOT": "env-dot"}, "custom-dot"),
        ):
            with (self.subTest(options=options, environment=environment),
                  patch.dict(decomp.os.environ, environment, clear=True),
                  patch.object(sys, "argv", ["gen_decomp.py", *options]),
                  patch.object(decomp.os, "access", return_value=True),
                  patch.object(decomp, "generate"),
                  patch.object(decomp, "sync_outputs", return_value=False) as sync):
                self.assertFalse(decomp.main())
                self.assertEqual(sync.call_args.args[-1], expected)


class DecompTests(unittest.TestCase):
    def test_fixed_host_fixtures_are_discovered_but_not_in_the_gallery(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            examples = root / "examples"
            examples.mkdir()
            regular = examples / "ordinary.pg"
            regular.write_text("parity 0;\n0 0 0 0;\n")
            for name in decomp.FIXED_HOST_EXAMPLES:
                (examples / name).write_text(regular.read_text())
            self.assertEqual(len(decomp.discover_games([examples])), 3)
            self.assertEqual(decomp.gallery_games(root), [regular])

    def test_content_discovery_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            examples = root / "examples"
            (examples / "nested").mkdir(parents=True)
            game = examples / "nested/game.gm"
            game.write_text('\nparity 80;\nstart 4;\n'
                            '4 9 0 4,20 "name; with back\\slash";\n'
                            '20 2 1 4;\n')
            (examples / "fake.pg").write_text("// not a parity game\n")
            (examples / "tree.hpp").write_text("int tree[] = {0};\n")
            self.assertEqual(decomp.discover_games([examples]), [game])
            self.assertEqual(decomp.game_info(game), (2, 3, 2, 9))
            (examples / "game.pg").write_text("parity 0;\n0 0 0 0;\n")
            with self.assertRaisesRegex(ValueError, "unique stems"):
                decomp.discover_games([examples])

    def test_metadata_uses_last_vertex_declaration(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / "replaced.pg"
            for records, expected in (
                ("0 1 1 0;\n0 0 0 0;\n", (1, 1, 1, 0)),
                ("0 9 1 0,20;\n20 2 1 0;\n00 0 0 0;\n", (2, 2, 2, 2)),
            ):
                with self.subTest(records=records):
                    game.write_text("parity 20;\n" + records)
                    self.assertEqual(decomp.game_info(game), expected)

    def test_metadata_names_use_literal_backslashes(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / "named.pg"
            game.write_text('parity 0;\n0 0 0 0 "name\\";\n')
            self.assertEqual(decomp.game_info(game), (1, 1, 1, 0))
            game.write_text('parity 0;\n0 0 0 0 "escaped\\"quote";\n')
            with self.assertRaisesRegex(ValueError, "invalid PGSolver vertex record"):
                decomp.game_info(game)

    def test_metadata_chunk_boundaries_and_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / "chunked.pg"
            for headers in ("", "parity 20;", "start 4;", "parity 20; start 4;"):
                game.write_text(headers + '\n4 9 0 4,20 "é; name\\";\n20 2 1 4;\n')
                for chunk_size in (1, 2, 7, 64):
                    with self.subTest(headers=headers, chunk_size=chunk_size):
                        self.assertEqual(decomp.game_info(game, chunk_size), (2, 3, 2, 9))
            for invalid in ('0 1 0 0', '0 1 2 0;', '0 1 0 0 "unfinished;',
                            'parity 1; parity 1;', 'start 0; parity 1;',
                            '0 1 0 0; garbage;', ';'):
                with self.subTest(invalid=invalid):
                    game.write_text(invalid)
                    with self.assertRaisesRegex(ValueError, "invalid PGSolver vertex record"):
                        decomp.game_info(game, chunk_size=2)
            with self.assertRaisesRegex(ValueError, "chunk_size"):
                decomp.game_info(game, chunk_size=0)
            for empty in ("", " \n", "parity 0; start 0;\n"):
                game.write_text(empty)
                self.assertEqual(decomp.game_info(game, chunk_size=1), (0, 0, 0, 0))

    def test_metadata_rejects_invalid_record_before_reading_rest(self):
        for record in ("bad;", 'bad "name; with semicolon";'):
            stream = io.StringIO()
            with (self.subTest(record=record),
                  patch.object(Path, "open", return_value=stream),
                  patch.object(stream, "read", side_effect=[record, AssertionError("read past invalid record")]),
                  self.assertRaisesRegex(ValueError, "invalid PGSolver vertex record")):
                decomp.game_info(Path("invalid.pg"))

    def test_metadata_sparse_ids_and_duplicate_storage_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / "sparse.pg"
            huge = 10 ** 30
            game.write_text(f"{huge} 9 0 {huge},0; 0 1 0 0; "
                            f"0 {huge} 0 0,0; {huge} 2 0 0; 0 3 0 0; "
                            "1025 7 0 0;\n" + "".join(
                                f"{vertex} 0 0 0;\n" for vertex in range(1, 1025))
                            + "1025 0 0 0;\n")
            self.assertEqual(decomp.game_info(game, chunk_size=7), (1027, 1027, 3, 3))
            game.write_text(f"0 {huge} 0 0; {huge} 1 0 0;\n")
            self.assertEqual(decomp.game_info(game), (2, 2, 2, huge))

    def test_metadata_discards_generated_trailing_whitespace(self):
        class PaddedStream(io.StringIO):
            remaining = 16 * 1024 ** 2

            def read(self, size):
                if text := super().read(size):
                    return text
                length = min(size, self.remaining)
                self.remaining -= length
                return " " * length

        with patch.object(Path, "open", return_value=PaddedStream("parity 0; 0 0 0 0;")):
            tracemalloc.start()
            try:
                self.assertEqual(decomp.game_info(Path("padded.pg")), (1, 1, 1, 0))
                _, peak = tracemalloc.get_traced_memory()
            finally:
                tracemalloc.stop()
        self.assertLess(peak, 2 * 1024 ** 2)

    @unittest.skipIf(sys.platform == "darwin", "macOS does not enforce RLIMIT_AS")
    def test_metadata_million_vertices_under_memory_cap(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / "million.pg"
            with game.open("w") as stream:
                stream.write("parity 999999;\n")
                stream.writelines(f"{vertex} {vertex % 4} {vertex % 2} {vertex};\n"
                                  for vertex in range(1_000_000))
            # Match the runner's locale so system locale mappings do not consume the cap.
            result = subprocess.run([
                sys.executable, "-c",
                "import resource, sys; from pathlib import Path; "
                "sys.path.insert(0, sys.argv[1]); import gen_decomp; "
                "cap = 96 * 1024 ** 2; "
                "resource.setrlimit(resource.RLIMIT_AS, (cap, cap)); "
                "assert gen_decomp.game_info(Path(sys.argv[2])) == (1000000, 1000000, 4, 3)",
                str(ROOT / "scripts"), str(game),
            ], capture_output=True, text=True, timeout=60,
                env={**os.environ, "LC_ALL": "C", "LANG": "C"})
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_stable_stats(self):
        self.assertEqual(decomp.stable_stats(
            "lifting: result=complete lifts=3 elapsed=0.03 time_ms=4\n"
            "CPU time: 0.2 s\nwall-time: 0.3 s\ntiming: 2\n"
            "Solver failed: /repo/build/file\n", Path("/repo"),
            Path("/repo/build")),
            "lifting: result=complete lifts=3\nSolver failed: build/file\n")

    def test_tree_metrics_exclude_placeholders(self):
        ad = (ROOT / "tests/golden/pg2adot_sets.out").read_text()
        pm = (ROOT / "tests/golden/pm_strahler_sets.out").read_text()
        fields = ("nodes", "leaves", "height", "strahler")

        def original_info(dot, progress_measures):
            return [(tuple(metrics[name] for name in fields), shape)
                    for metrics, shape in decomp.tree_info(dot, progress_measures)]

        self.assertEqual(original_info(ad, False),
                         [((0, 0, 0, 0), ()), ((3, 2, 2, 2), (2, 0, 0))])
        self.assertEqual(original_info(pm, True),
                         [((5, 2, 3, 2), (2, 1, 0, 1, 0)),
                          ((0, 0, 0, 0), ())])

    def test_tree_metrics(self):
        children = dict(root=["a", "b", "c"], a=["d", "e"], b=[],
                        c=["f"], d=[], e=[], f=["g", "h"], g=[], h=[])
        expected = dict(nodes=9, leaves=5, internal_nodes=4, height=4, width=3,
                        max_arity=3, mean_internal_arity=2, branching_nodes=3,
                        min_leaf_depth=2, mean_leaf_depth=3.2, strahler=3)
        metrics, shape = decomp.tree_metrics(children, "root")
        self.assertEqual(metrics, expected)
        self.assertEqual(shape, (3, 2, 0, 0, 0, 1, 2, 0, 0))
        self.assertEqual(decomp.tree_metrics(children, None),
                         (dict.fromkeys(expected, 0), ()))
        self.assertEqual(decomp.tree_metrics(dict(root=[]), "root"), (
            dict(nodes=1, leaves=1, internal_nodes=0, height=1, width=1,
                 max_arity=0, mean_internal_arity=0, branching_nodes=0,
                 min_leaf_depth=1, mean_leaf_depth=1, strahler=1), (0,)))
        path, _ = decomp.tree_metrics(dict(root=["a"], a=["b"], b=[]), "root")
        self.assertEqual((path["height"], path["min_leaf_depth"],
                          path["mean_leaf_depth"]), (3, 3, 3))
        # Progress measures retain the extra root above the occupied prefix tree.
        children["extra"] = ["root"]
        self.assertEqual(decomp.tree_metrics(children, "extra"), (
            {**expected, "nodes": 10, "internal_nodes": 5, "height": 5,
             "mean_internal_arity": 1.8, "min_leaf_depth": 3,
             "mean_leaf_depth": 4.2}, (1, *shape)))

    def test_limits_errors_and_timeouts_discard_output(self):
        game = ROOT / "examples/Button.tlsf.ehoa.pg"
        with patch.object(decomp.subprocess, "run") as run:
            for code, stderr, outcome in (
                (4, "Solver failed: more than the limit of 100\n",
                 "host limit"),
                (0, "Warning: more than 100 leaves\nresult=partial\n",
                 "host limit"),
                (4, "bad game\nmore detail\n", "error: bad game"),
            ):
                run.return_value = subprocess.CompletedProcess(
                    [], code, "partial DOT", stderr)
                result = decomp.invoke(["tool"], game, ROOT, BUILD_DIR, 1)
                self.assertEqual(result.outcome, outcome)
                self.assertFalse(result.dot)
            run.side_effect = subprocess.TimeoutExpired(
                "tool", 1, output=b"partial DOT", stderr=b"partial stats")
            self.assertEqual(decomp.invoke(["tool"], game, ROOT, BUILD_DIR, 1),
                             decomp.Run("timeout (1 s)"))
        self.assertEqual(decomp.invoke(
            [sys.executable, "-c", "import time; time.sleep(1)"],
            game, ROOT, BUILD_DIR, 0.01), decomp.Run("timeout (0.01 s)"))

    def test_timeout_aborts_generation_without_syncing_outputs(self):
        with (patch.object(sys, "argv", ["gen_decomp.py"]),
              patch.object(decomp.os, "access", return_value=True),
              patch.object(decomp.subprocess, "run", side_effect=
                           subprocess.TimeoutExpired("tool", 600)) as run,
              patch.object(decomp, "sync_outputs") as sync,
              redirect_stdout(io.StringIO())):
            with self.assertRaisesRegex(SystemExit, "safety timeout after 600 s"):
                decomp.main()
            self.assertEqual(run.call_args.kwargs["timeout"], 600)
            sync.assert_not_called()

    def test_backends_skip_only_listed_pairs_and_cap_labels(self):
        for name in ("cabp_swp.ehoa.pg", "FairSchedulern=2_compact.gm"):
            game = ROOT / "examples" / name
            for backend in decomp.BACKENDS:
                with (self.subTest(game=name, backend=backend.name),
                      patch.object(decomp, "invoke",
                                   return_value=decomp.Run("host limit")) as invoke):
                    result = decomp.run_backend(
                        backend, game, ROOT, BUILD_DIR, 1, 100, skip_runs=True)
                    if (name, backend.name) in decomp.SKIP_RUNS:
                        self.assertEqual(
                            result, decomp.Run("skipped (> 60 s locally)"))
                        invoke.assert_not_called()
                    else:
                        self.assertEqual(result.outcome, "host limit")
                        command = invoke.call_args.args[0]
                        self.assertIn("--labels=sets", command)
                        self.assertIn("--max-set-items=8", command)
                        self.assertEqual("--initial-shave=all" in command,
                                         "+pre" in backend.name)
                        self.assertEqual("--iter-shave" in command,
                                         "+iter" in backend.name)
                        if backend.name.startswith("lifting"):
                            self.assertIn("--max-host-leaves=100", command)
                    invoke.reset_mock()
                    decomp.run_backend(backend, game, ROOT, BUILD_DIR, 1, 100)
                    invoke.assert_called_once()

    def test_adaptive_equality_is_enforced(self):
        backend = next(b for b in decomp.BACKENDS if b.name == "lifting-adaptive")
        game = ROOT / "examples/Button.tlsf.ehoa.pg"
        with patch.object(decomp, "invoke", side_effect=[
            decomp.Run("complete", dot="cold"),
            decomp.Run("complete", dot="different"),
        ]):
            with self.assertRaisesRegex(AssertionError,
                                        "adaptive trees differ"):
                decomp.run_backend(backend, game, ROOT, BUILD_DIR,
                                   1, 100)

    @unittest.skipUnless(all((BUILD_DIR / name).is_file()
                             for name in ("pg2adot", "pg2pmdot")),
                         "build the CLI tools to run the integration check")
    def test_determinism_and_stale_files(self):
        with (tempfile.TemporaryDirectory() as directory,
              redirect_stdout(io.StringIO())):
            root = Path(directory) / "repo"
            (root / "examples").mkdir(parents=True)
            shutil.copyfile(ROOT / "examples/Button.tlsf.ehoa.pg",
                            root / "examples/small.gm")
            shutil.copyfile(ROOT / "tests/games/shave_single.pg",
                            root / "examples/all-shaved.pg")
            outputs = [Path(directory) / name for name in ("one", "two")]
            program = fake_dot(Path(directory))
            for output in outputs:
                output.mkdir()
                decomp.generate(root, output, BUILD_DIR, 5, 10_000)
            self.assertEqual(decomp.output_files(outputs[0]),
                             decomp.output_files(outputs[1]))
            self.assertEqual(len(list(outputs[0].rglob("*.dot"))), 48)
            self.assertEqual(len(list(outputs[0].rglob("*.leaves"))), 16)
            self.assertEqual(len(list(outputs[0].rglob("*.metrics"))), 48)
            inventory = (outputs[0] / "DECOMP.md").read_text()
            self.assertIn("[ALGOS.md](ALGOS.md)", inventory)
            self.assertIn("docs/developing.md#regenerating-example-trees",
                          inventory)
            self.assertNotIn("## Methods", inventory)
            self.assertNotIn("```", inventory)
            self.assertLess(inventory.index("## Inventory"),
                            inventory.index("### all-shaved.pg"))
            self.assertLess(inventory.index("### small.gm"),
                            inventory.index("## Tree disagreements"))
            self.assertEqual(inventory.count("[PNG](decomp/"), 48)
            self.assertEqual(inventory.count("[metrics](decomp/"), 48)
            self.assertIn("Height E / O | Parameters E / O | Tree |", inventory)
            self.assertNotIn("Strahler E / O", inventory)
            for dot in (outputs[0] / "decomp/all-shaved").glob("*+pre.dot"):
                info = decomp.tree_info(dot.read_text(),
                                        dot.name.startswith("pm-"))
                self.assertEqual(info,
                                 [(dict.fromkeys(decomp.TREE_METRICS, 0), ())] * 2)
            self.assertTrue(decomp.sync_outputs(root, outputs[0], True))
            decomp.sync_outputs(root, outputs[0], False, program)
            self.assertEqual(len(list(root.rglob("*.png"))), 48)
            self.assertFalse(decomp.sync_outputs(root, outputs[1], True))
            stale = root / "decomp/small/obsolete.dot"
            stale.write_text("old")
            stale_metrics = stale.with_suffix(".metrics")
            stale_metrics.write_text("old")
            (root / "DECOMP.md").write_text("stale inventory")
            (root / "decomp/small/lifting.dot").unlink()
            (root / "decomp/small/lifting.metrics").unlink()
            before = decomp.output_files(root)
            report = io.StringIO()
            with redirect_stdout(report):
                self.assertTrue(decomp.sync_outputs(root, outputs[1], True))
            for name in ("DECOMP.md", "obsolete.dot", "lifting.dot",
                         "obsolete.metrics", "lifting.metrics"):
                self.assertIn(name, report.getvalue())
            self.assertEqual(before, decomp.output_files(root))
            decomp.sync_outputs(root, outputs[1], False, program)
            self.assertFalse(decomp.sync_outputs(root, outputs[1], True))


if __name__ == "__main__":
    unittest.main()
