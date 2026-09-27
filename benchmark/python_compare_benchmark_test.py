"""Behavior checks for the Python comparison adapter and reference exchange."""

import argparse
from pathlib import Path
import tempfile
import unittest

import python_compare_benchmark as benchmark


class PythonBenchmarkTest(unittest.TestCase):
    """Use a small real dictionary while sharing the production benchmark's HMM adapter."""

    @classmethod
    def setUpClass(cls):
        cls.cache = tempfile.TemporaryDirectory(prefix="jieba-benchmark-test-")
        cls.addClassCleanup(cls.cache.cleanup)
        cls.directory = Path(cls.cache.name)
        dictionary = cls.directory / "dict.txt"
        dictionary.write_text("南京 10 ns\n南京市 20 ns\n市长 5 n\n长江 10 ns\n长江大桥 20 ns\n大桥 10 n\n",
                              encoding="utf-8")
        cls.jieba = benchmark.load_jieba()
        benchmark.load_hmm(cls.jieba.finalseg, benchmark.ROOT / "dict/hmm_model.utf8")
        cls.tokenizer = cls.jieba.Tokenizer(dictionary=str(dictionary))
        cls.tokenizer.tmp_dir = str(cls.directory)
        cls.tokenizer.initialize()
        cls.methods = benchmark.cut_methods(cls.tokenizer)

    def test_corpus_loading_preserves_whitespace_nul_and_unicode(self):
        path = self.directory / "corpus.txt"
        path.write_bytes("\r\n\n南京\r\n \n🀄\0𠮷\n甲\r\r\n末尾".encode())
        self.assertEqual([line.decode() for line in benchmark.load_lines(path)],
                         ["南京", " ", "🀄\0𠮷", "甲\r", "末尾"])

    def test_model_uses_external_probabilities(self):
        self.assertEqual(self.jieba.finalseg.start_P["B"], -0.26268660809250016)
        self.assertEqual(self.jieba.finalseg.trans_P["B"]["E"], -0.510825623765990)
        self.assertEqual(self.jieba.finalseg.emit_P["B"]["耀"], -10.460283)

    def test_malformed_model_is_rejected_without_replacing_weights(self):
        path = self.directory / "invalid-model.txt"
        path.write_text("1 2 3 4\n", encoding="utf-8")
        before = self.jieba.finalseg.emit_P
        with self.assertRaisesRegex(ValueError, "Invalid HMM model"):
            benchmark.load_hmm(self.jieba.finalseg, path)
        self.assertIs(self.jieba.finalseg.emit_P, before)

    def test_nonfinite_model_probabilities_are_rejected(self):
        path = self.directory / "nonfinite-model.txt"
        path.write_text("nan 0 0 0\n" * 5 + "字:0\n" * 4, encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "finite probabilities"):
            benchmark.load_hmm(self.jieba.finalseg, path)

    def test_mix_and_mp_return_precise_words(self):
        for mode in ["MIX", "MP"]:
            with self.subTest(mode=mode):
                self.assertEqual(self.methods[mode]("南京市长江大桥"), ["南京市", "长江大桥"])

    def test_full_returns_overlapping_dictionary_matches(self):
        self.assertEqual(self.methods["FULL"]("南京市长江大桥"),
                         ["南京", "南京市", "市长", "长江", "长江大桥", "大桥"])

    def test_search_emits_short_matches_before_the_precise_word(self):
        self.assertEqual(self.methods["SEARCH"]("南京市长江大桥"),
                         ["南京", "南京市", "长江", "大桥", "长江大桥"])

    def test_empty_input_returns_no_words_in_each_mode(self):
        for name, method in self.methods.items():
            with self.subTest(mode=name):
                self.assertEqual(method(""), [])

    def test_unknown_non_bmp_characters_and_nul_are_preserved(self):
        for mode in ["MIX", "MP", "SEARCH"]:
            with self.subTest(mode=mode):
                self.assertEqual(self.methods[mode]("🀄\0𠮷"), ["🀄", "\0", "𠮷"])

    def test_reference_offsets_are_utf8_bytes_and_may_overlap(self):
        raw = "🀄南京\0".encode()
        self.assertEqual(benchmark.reference_words(raw, [[0, 4], [4, 7], [4, 10], [10, 11]]),
                         ["🀄", "南", "南京", "\0"])

    def test_reference_rejects_truncated_unicode(self):
        with self.assertRaises(UnicodeDecodeError):
            benchmark.reference_words("南京".encode(), [[0, 2]])

    def test_reference_rejects_out_of_bounds_ranges(self):
        with self.assertRaisesRegex(ValueError, "invalid UTF-8 byte range"):
            benchmark.reference_words(b"a", [[0, 2]])

    def test_verification_reports_order_differences(self):
        native = {"methods": [{"name": "MIX", "neo_tokens": 2, "neo_utf8_ranges": [[[0, 1], [1, 2]]]}]}
        result = benchmark.verify_outputs({"MIX": lambda line: list(reversed(line))}, [b"ab"], ["ab"], native)
        self.assertEqual(result["MIX"]["neo_python_mismatches"], 1)
        self.assertEqual(result["MIX"]["examples"][0]["first_token"], 0)
        self.assertEqual(result["MIX"]["tokens_per_round"], 2)

    def test_verification_rejects_missing_reference_lines(self):
        native = {"methods": [{"name": "MIX", "neo_tokens": 0, "neo_utf8_ranges": []}]}
        with self.assertRaises(ValueError):
            benchmark.verify_outputs({"MIX": list}, [b"a"], ["a"], native)

    def test_measurement_consumes_all_lines_and_rounds(self):
        result = benchmark.measure(self.methods["MIX"], ["南京市长江大桥", "🀄"], 3)
        self.assertEqual(result["tokens"], 9)
        self.assertGreaterEqual(result["milliseconds"], 0)

    def test_iteration_counts_must_be_positive(self):
        for value in ["0", "-1"]:
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                benchmark.positive_count(value)


class BenchmarkSummaryTest(unittest.TestCase):
    """Verify the displayed output contracts and HMM results using distinct synthetic timings."""

    def setUp(self):
        timings = [
            {"label": "Rust FFI views -> C++ strings", "median_ms": 99.0},
            {"label": "Neo reused token positions", "median_ms": 4.0},
            {"label": "Rust native borrowed tokens", "median_ms": 6.0},
            {"label": "Neo C++ strings", "median_ms": 2.0},
            {"label": "Old C++ strings", "median_ms": 1.0},
            {"label": "Rust native owned strings", "median_ms": 5.0},
            {"label": "Neo borrowed tokens", "median_ms": 3.0},
        ]
        self.native = {"lines": 8, "rounds": 3, "samples": 5,
                       "methods": [{"name": "MIX", "timings": timings,
                                    "old_neo_mismatches": 0, "neo_rust_mismatches": 2}],
                       "hmm": {"old_neo_mismatches": 1, "timings": [
                           {"label": "Neo C++ strings", "median_ms": 7.0},
                           {"label": "Old C++ strings", "median_ms": 12.0}]}}
        self.python = {"MIX": {"median_ms": 8.0, "neo_python_mismatches": 0}}

    def test_summary_groups_owned_and_borrowed_columns_by_implementation(self):
        summary = benchmark.format_summary(self.native, self.python)
        header = next(line for line in summary.splitlines() if line.startswith("| Mode |"))
        self.assertEqual([cell.strip() for cell in header.split("|")[1:-1]],
                         ["Mode", "Old C++ owned", "Neo C++ owned", "Neo C++ borrowed", "Neo C++ reused",
                          "Rust owned", "Rust borrowed", "Python owned"])
        self.assertIn("| MIX | 1.000 | 2.000 | 3.000 | 4.000 | 5.000 | 6.000 | 8.000 |", summary)

    def test_summary_includes_hmm_owned_timings_and_output_differences(self):
        summary = benchmark.format_summary(self.native, self.python)
        self.assertIn("| HMM | 12.000 | 7.000 | 1/8 |", summary)
        self.assertIn("Rust and Python HMM are not measured", summary)

    def test_summary_reports_sampling_units(self):
        summary = benchmark.format_summary(self.native, self.python)
        self.assertIn("8 lines, 3 rounds/sample, 5 samples", summary)
        self.assertIn("Median milliseconds per sample", summary)

    def test_summary_shows_python_ratio_for_matching_output(self):
        summary = benchmark.format_summary(self.native, self.python)
        self.assertIn("| MIX | 0/8 | 2/8 | 0/8 | 4.00x |", summary)

    def test_summary_suppresses_python_ratio_for_different_output(self):
        self.python["MIX"]["neo_python_mismatches"] = 1
        summary = benchmark.format_summary(self.native, self.python)
        self.assertIn("| MIX | 0/8 | 2/8 | 1/8 | — |", summary)
        self.assertNotIn("4.00x", summary)

    def test_summary_rejects_a_native_report_without_hmm_results(self):
        del self.native["hmm"]
        with self.assertRaisesRegex(ValueError, "rebuild cut_compare_benchmark"):
            benchmark.format_summary(self.native, self.python)


if __name__ == "__main__":
    unittest.main()
