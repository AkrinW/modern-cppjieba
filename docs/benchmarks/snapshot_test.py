"""Check publication data integrity and preservation of hand-written documentation."""

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

import snapshot


def make_report(name):
    """Provide distinct owned and borrowed measurements with known output differences."""
    methods = []
    for index, mode in enumerate(snapshot.MODES):
        timings = []
        for label, median in [("Old C++ strings", 12), ("Neo C++ strings", 6),
                              ("Rust native owned strings", 9), ("Neo borrowed tokens", 2)]:
            timings.append({"label": label, "median_ms": median + index,
                            "min_ms": median + index - 1, "max_ms": median + index + 1})
        methods.append({"name": mode, "old_neo_mismatches": 0,
                        "neo_rust_mismatches": index, "timings": timings})
    return {
        "environment": {
            "repo_revision": "a" * 40, "cpu_affinity": [1], "platform": "Linux",
            "python": "3.12.10 (fixture)", "implementation": "CPython",
            "jieba_version": "0.42.1", "jieba_revision": "b" * 40,
        },
        "inputs": {key: {"path": f"{name}.utf8" if key == "text" else key, "sha256": key * 4}
                   for key in ("dictionary", "model", "native_executable", "text")},
        "native": {"schema_version": 1, "rounds": 3, "samples": 5, "lines": 8,
                   "utf8_bytes": 255, "methods": methods},
        "python": {mode: {"median_ms": 20, "min_ms": 19, "max_ms": 21,
                          "neo_python_mismatches": index}
                   for index, mode in enumerate(snapshot.MODES)},
    }


class SnapshotTest(unittest.TestCase):
    """Publish only consistent measurements and retain the README outside its generated section."""

    def setUp(self):
        self.reports = {name: make_report(name) for name in snapshot.CORPORA}
        self.metadata = {
            "measured_on": "2026-10-08", "revision": "a" * 40,
            "environment": {"cpu": "Fixture CPU", "cpp_compiler": "GCC 14.2.0",
                            "cpp_flags": ["-O3", "-DNDEBUG"], "build_type": "Release",
                            "rust_compiler": "rustc 1.94.0"},
            "dependencies": {"cppjieba_revision": "c" * 40, "jieba_rs_version": "0.11.0"},
            "ci": {"run_url": "https://github.com/example/project/actions/runs/123",
                   "repository_url": "https://github.com/example/project",
                   "runner_image": "ubuntu-24.04"},
        }

    def test_owned_timings_and_token_differences_are_preserved(self):
        result = snapshot.build_snapshot(self.reports, self.metadata)
        corpus = result["corpora"][0]
        self.assertEqual(corpus["timings"]["modern"]["median_ms"], [6, 7, 8, 9])
        self.assertEqual(corpus["timings"]["modern"]["min_ms"], [5, 6, 7, 8])
        self.assertEqual(corpus["timings"]["modern"]["max_ms"], [7, 8, 9, 10])
        self.assertEqual(corpus["different_lines_vs_modern"]["rust"], [0, 1, 2, 3])
        self.assertEqual(corpus["different_lines_vs_modern"]["python"], [0, 1, 2, 3])
        self.assertEqual(result["ci"]["run_url"], self.metadata["ci"]["run_url"])

    def test_method_order_does_not_change_the_plotted_modes(self):
        self.reports["testlines"]["native"]["methods"].reverse()
        result = snapshot.build_snapshot(self.reports, self.metadata)
        self.assertEqual(result["corpora"][0]["timings"]["modern"]["median_ms"], [6, 7, 8, 9])

    def test_reports_from_another_revision_are_rejected(self):
        self.metadata["revision"] = "d" * 40
        with self.assertRaisesRegex(ValueError, "revision"):
            snapshot.build_snapshot(self.reports, self.metadata)

    def test_mixed_environments_are_rejected(self):
        self.reports["weicheng"]["environment"]["cpu_affinity"] = [2]
        with self.assertRaisesRegex(ValueError, "environments"):
            snapshot.build_snapshot(self.reports, self.metadata)

    def test_unpinned_measurements_are_rejected(self):
        for report in self.reports.values():
            report["environment"]["cpu_affinity"] = [0, 1]
        with self.assertRaisesRegex(ValueError, "one CPU"):
            snapshot.build_snapshot(self.reports, self.metadata)

    def test_mixed_input_or_executable_hashes_are_rejected(self):
        for name in ("dictionary", "model", "native_executable"):
            with self.subTest(name=name):
                reports = deepcopy(self.reports)
                reports["weicheng"]["inputs"][name]["sha256"] = "different"
                with self.assertRaisesRegex(ValueError, name):
                    snapshot.build_snapshot(reports, self.metadata)

    def test_incomplete_owned_measurements_are_rejected(self):
        self.reports["testlines"]["native"]["methods"][0]["timings"].pop(1)
        with self.assertRaisesRegex(ValueError, "owned-string"):
            snapshot.build_snapshot(self.reports, self.metadata)

    def test_swapped_corpus_reports_are_rejected(self):
        self.reports["testlines"] = deepcopy(self.reports["weicheng"])
        with self.assertRaisesRegex(ValueError, "different corpus"):
            snapshot.build_snapshot(self.reports, self.metadata)

    def test_nonfinite_nonpositive_and_inverted_times_are_rejected(self):
        for value in [0, -1, float("nan"), float("inf"), 100]:
            with self.subTest(value=value):
                reports = deepcopy(self.reports)
                reports["testlines"]["python"]["MP"]["median_ms"] = value
                with self.assertRaisesRegex(ValueError, "elapsed times"):
                    snapshot.build_snapshot(reports, self.metadata)

    def test_readme_updates_only_the_marked_section(self):
        result = snapshot.build_snapshot(self.reports, self.metadata)
        original = f"# Project\n\n{snapshot.BEGIN}\nold data\n{snapshot.END}\n\n## Development\n"
        updated = snapshot.update_readme(original, result, "docs/benchmarks/snapshot.json")
        self.assertTrue(updated.startswith(f"# Project\n\n{snapshot.BEGIN}\n"))
        self.assertTrue(updated.endswith(f"{snapshot.END}\n\n## Development\n"))
        self.assertNotIn("old data", updated)
        self.assertIn(result["ci"]["run_url"], updated)
        self.assertIn("docs/benchmarks/snapshot.json", updated)
        self.assertEqual(snapshot.update_readme(updated, result, "docs/benchmarks/snapshot.json"), updated)

    def test_missing_duplicate_and_reversed_markers_are_rejected(self):
        result = snapshot.build_snapshot(self.reports, self.metadata)
        for readme in ["no markers", snapshot.BEGIN * 2 + snapshot.END, snapshot.END + snapshot.BEGIN]:
            with self.subTest(readme=readme):
                with self.assertRaisesRegex(ValueError, "marker"):
                    snapshot.update_readme(readme, result, "snapshot.json")

    def test_invalid_reports_leave_existing_documents_intact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "environment.json").write_text(json.dumps(self.metadata), encoding="utf-8")
            for name, report in self.reports.items():
                (root / f"{name}.json").write_text(json.dumps(report), encoding="utf-8")
            readme = root / "README.md"
            readme.write_text("missing markers", encoding="utf-8")
            output = root / "snapshot.json"
            output.write_text("existing snapshot", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "marker"):
                snapshot.generate(root, output, readme)
            self.assertEqual(readme.read_text(encoding="utf-8"), "missing markers")
            self.assertEqual(output.read_text(encoding="utf-8"), "existing snapshot")

    def test_generated_snapshot_records_exact_report_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "environment.json").write_text(json.dumps(self.metadata), encoding="utf-8")
            for name, report in self.reports.items():
                (root / f"{name}.json").write_text(json.dumps(report), encoding="utf-8")
            readme = root / "README.md"
            readme.write_text(f"{snapshot.BEGIN}\nold\n{snapshot.END}", encoding="utf-8")
            output = root / "docs/benchmarks/snapshot.json"
            snapshot.generate(root, output, readme)
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(result["raw_environment_sha256"], snapshot.fingerprint(root / "environment.json"))
            for name in self.reports:
                self.assertEqual(result["raw_reports"][name]["sha256"],
                                 snapshot.fingerprint(root / f"{name}.json"))
            self.assertIn("[测试数据](docs/benchmarks/snapshot.json)", readme.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
