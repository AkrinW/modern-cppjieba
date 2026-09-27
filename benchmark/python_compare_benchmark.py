#!/usr/bin/env python3
"""Compare native C++/Rust paths with pinned Python jieba using shared inputs."""

import argparse
from functools import partial
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
PYTHON_SOURCE = ROOT / "deps/python-jieba"


def load_lines(path):
    """Match BenchmarkUtils.hpp: split on LF, remove one final CR, skip empty lines."""
    return [line for raw in path.read_bytes().split(b"\n") if (line := raw.removesuffix(b"\r"))]


def load_jieba():
    """Import the checked-out submodule without installing a system package."""
    if not (PYTHON_SOURCE / "jieba/__init__.py").is_file():
        raise RuntimeError("Run: git submodule update --init deps/python-jieba")
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(PYTHON_SOURCE))
    import jieba

    if Path(jieba.__file__).resolve().parent != PYTHON_SOURCE / "jieba":
        raise RuntimeError("A different jieba package was imported before the benchmark")
    return jieba


def load_hmm(finalseg, path):
    """Adapt the shared CppJieba B/E/M/S text model to Python's probability maps."""
    rows = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")]
    states = "BEMS"
    try:
        if len(rows) != 9:
            raise ValueError("expected 9 data rows")
        matrix = [list(map(float, row.split())) for row in rows[:5]]
        if any(len(row) != 4 or not all(map(math.isfinite, row)) for row in matrix):
            raise ValueError("expected four finite probabilities per start/transition row")
        emissions = []
        for row in rows[5:]:
            weights = {}
            for entry in row.split(","):
                rune, value = entry.rsplit(":", 1)
                probability = float(value)
                if len(rune) != 1 or not math.isfinite(probability):
                    raise ValueError("invalid emission entry")
                weights[rune] = probability
            emissions.append(weights)
    except ValueError as error:
        raise ValueError(f"Invalid HMM model {path}: {error}") from error
    finalseg.start_P = dict(zip(states, matrix[0], strict=True))
    finalseg.trans_P = {state: dict(zip(states, row, strict=True))
                       for state, row in zip(states, matrix[1:], strict=True)}
    finalseg.emit_P = dict(zip(states, emissions, strict=True))


def cut_methods(tokenizer):
    """Materialize every public generator into a list of owned Python strings."""
    return {
        "MIX": partial(tokenizer.lcut, cut_all=False, HMM=True, use_paddle=False),
        "MP": partial(tokenizer.lcut, cut_all=False, HMM=False, use_paddle=False),
        "FULL": partial(tokenizer.lcut, cut_all=True, HMM=False, use_paddle=False),
        "SEARCH": partial(tokenizer.lcut_for_search, HMM=True),
    }


def reference_words(line, ranges):
    """Reconstruct Neo words from UTF-8 byte coordinates, including overlapping search hits."""
    words = []
    for begin, end in ranges:
        if not 0 <= begin < end <= len(line):
            raise ValueError("Native report contains an invalid UTF-8 byte range")
        words.append(line[begin:end].decode("utf-8"))
    return words


def verify_outputs(methods, raw_lines, lines, native):
    """Count exact sequence differences and retain two bounded examples per mode."""
    if {result["name"] for result in native["methods"]} != methods.keys():
        raise ValueError("Native report contains unexpected segmentation modes")
    results = {}
    for result in native["methods"]:
        name = result["name"]
        mismatches = tokens = neo_tokens = 0
        examples = []
        for index, (raw, line, ranges) in enumerate(
                zip(raw_lines, lines, result["neo_utf8_ranges"], strict=True), 1):
            expected = reference_words(raw, ranges)
            words = methods[name](line)
            neo_tokens += len(expected)
            tokens += len(words)
            if words == expected:
                continue
            mismatches += 1
            if len(examples) < 2:
                first = next((i for i, (a, b) in enumerate(zip(expected, words)) if a != b),
                             min(len(expected), len(words)))
                begin = max(0, first - 3)
                examples.append({"line": index, "first_token": first,
                                 "neo": expected[begin:first + 5], "python": words[begin:first + 5]})
        if neo_tokens != result["neo_tokens"]:
            raise ValueError("Native reference token count differs from its timed correctness pass")
        results[name] = {"tokens_per_round": tokens, "neo_python_mismatches": mismatches,
                         "examples": examples, "samples_ms": []}
    return results


def measure(fn, lines, rounds):
    """Include list/string materialization and per-line destruction in Python's native timer."""
    tokens = 0
    start = time.perf_counter_ns()
    for _ in range(rounds):
        for line in lines:
            words = fn(line)
            tokens += len(words)
            del words
    return {"milliseconds": (time.perf_counter_ns() - start) / 1_000_000, "tokens": tokens}


def benchmark_python(methods, lines, results, rounds, samples):
    """Warm up once, rotate mode order, and validate every timed sample's token count."""
    names = list(methods)
    for name in names:
        measure(methods[name], lines, 1)
    for sample in range(samples):
        for offset in range(len(names)):
            name = names[(sample + offset) % len(names)]
            measurement = measure(methods[name], lines, rounds)
            result = results[name]
            if measurement["tokens"] != result["tokens_per_round"] * rounds:
                raise RuntimeError(f"Python {name} token count differs from correctness pass")
            result["samples_ms"].append(measurement["milliseconds"])
        print(f"Python sample {sample + 1}/{samples} complete", flush=True)
    for result in results.values():
        values = result["samples_ms"]
        result.update(median_ms=statistics.median(values), min_ms=min(values), max_ms=max(values))


def fingerprint(path):
    """Record the exact input or native executable used for this measurement."""
    with path.open("rb") as source:
        return {"path": str(path), "sha256": hashlib.file_digest(source, "sha256").hexdigest()}


def git_revision(path):
    """Record the actual checkout revision, including submodule overrides."""
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def positive_count(value):
    """Reject nonpositive iteration counts before launching either benchmark process."""
    count = int(value)
    if count <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return count


def format_summary(native, python):
    """Show raw median times; only give a Python/Neo ratio when their output sequences agree."""
    if "hmm" not in native:
        raise ValueError("Native report lacks HMM results; rebuild cut_compare_benchmark")
    labels = ["Old C++ strings", "Neo C++ strings", "Neo borrowed tokens", "Neo reused token positions",
              "Rust native owned strings", "Rust native borrowed tokens"]
    rows = [f"{native['lines']} lines, {native['rounds']} rounds/sample, {native['samples']} samples.", "",
            "Median milliseconds per sample:", "",
            "| Mode | Old C++ owned | Neo C++ owned | Neo C++ borrowed | Neo C++ reused | "
            "Rust owned | Rust borrowed | Python owned |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    differences = ["| Mode | Old/Neo diff | Neo/Rust diff | Neo/Python diff | Python/Neo time |",
                   "| --- | ---: | ---: | ---: | ---: |"]
    for method in native["methods"]:
        name = method["name"]
        timings = {entry["label"]: entry["median_ms"] for entry in method["timings"]}
        result = python[name]
        values = " | ".join(f"{timings[label]:.3f}" for label in labels)
        rows.append(f"| {name} | {values} | {result['median_ms']:.3f} |")
        ratio = (f"{result['median_ms'] / timings['Neo C++ strings']:.2f}x"
                 if result["neo_python_mismatches"] == 0 else "—")
        differences.append(f"| {name} | {method['old_neo_mismatches']}/{native['lines']} | "
                           f"{method['neo_rust_mismatches']}/{native['lines']} | "
                           f"{result['neo_python_mismatches']}/{native['lines']} | {ratio} |")
    hmm = native["hmm"]
    hmm_timings = {entry["label"]: entry["median_ms"] for entry in hmm["timings"]}
    rows.extend(["", "Owned outputs materialize strings; borrowed outputs reference the input. "
                 "Neo reused retains caller-owned workspace and output buffers.", "",
                 "Differences count lines with different token sequences. "
                 "Python/Neo ratios use owned strings and are omitted when those sequences differ.", "",
                 *differences, "", "**HMM only (old/neo C++)**", "",
                 "| Mode | Old C++ owned | Neo C++ owned | Old/Neo diff |",
                 "| --- | ---: | ---: | ---: |",
                 f"| HMM | {hmm_timings['Old C++ strings']:.3f} | {hmm_timings['Neo C++ strings']:.3f} | "
                 f"{hmm['old_neo_mismatches']}/{native['lines']} |", "",
                 "HMM uses the same rounds and samples; Rust and Python HMM are not measured.", ""])
    return "\n".join(rows)


def run():
    """Run processes sequentially and keep initialization, file I/O, and verification outside timers."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path, help="Release cut_compare_benchmark executable")
    parser.add_argument("--dict", dest="dictionary", type=Path, default=ROOT / "dict/jieba.dict.utf8")
    parser.add_argument("--model", type=Path, default=ROOT / "dict/hmm_model.utf8")
    parser.add_argument("--text", type=Path, required=True)
    parser.add_argument("--rounds", type=positive_count, required=True)
    parser.add_argument("--samples", type=positive_count, required=True)
    parser.add_argument("--output", type=Path, required=True, help="JSON report, preferably in benchmark/results/")
    args = parser.parse_args()
    args.native, args.dictionary, args.model, args.text, args.output = (
        path.resolve() for path in (args.native, args.dictionary, args.model, args.text, args.output))
    if args.output.suffix != ".json":
        parser.error("--output must end in .json")
    raw_lines = load_lines(args.text)
    if not raw_lines:
        parser.error("text contains no nonempty lines")
    lines = [line.decode("utf-8") for line in raw_lines]
    jieba = load_jieba()
    load_hmm(jieba.finalseg, args.model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    native_report = args.output.with_suffix(".native.json")
    native_log = args.output.with_suffix(".native.log")
    command = [str(args.native), str(args.dictionary), str(args.model), "", str(args.text),
               str(args.rounds), str(args.samples), str(native_report)]
    print(f"Running C++/Rust benchmark; log: {native_log}", flush=True)
    with native_log.open("w", encoding="utf-8") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    native = json.loads(native_report.read_text(encoding="utf-8"))
    expected = {"schema_version": 1, "rounds": args.rounds, "samples": args.samples,
                "lines": len(lines), "utf8_bytes": sum(map(len, raw_lines))}
    if any(native.get(key) != value for key, value in expected.items()):
        raise ValueError("Native report and Python input/sample configuration differ")
    print(f"Python {platform.python_version()}, jieba {jieba.__version__}; "
          f"{len(lines)} lines, {args.rounds} rounds/sample, {args.samples} samples", flush=True)
    # A fresh cache directory prevents an older dictionary cache from changing this run's input.
    with tempfile.TemporaryDirectory(prefix="jieba-benchmark-") as cache:
        tokenizer = jieba.Tokenizer(dictionary=str(args.dictionary))
        tokenizer.tmp_dir = cache
        tokenizer.initialize()
        methods = cut_methods(tokenizer)
        python = verify_outputs(methods, raw_lines, lines, native)
        benchmark_python(methods, lines, python, args.rounds, args.samples)
    summary = format_summary(native, python)
    print(f"\n{summary}")
    for result in native["methods"]:
        del result["neo_utf8_ranges"]
    report = {"environment": {"python": sys.version, "implementation": platform.python_implementation(),
                              "platform": platform.platform(), "jieba_version": jieba.__version__,
                              "jieba_revision": git_revision(PYTHON_SOURCE), "repo_revision": git_revision(ROOT),
                              "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else []},
              "inputs": {"dictionary": fingerprint(args.dictionary), "model": fingerprint(args.model),
                         "text": fingerprint(args.text), "native_executable": fingerprint(args.native)},
              "native_command": command, "native_log": str(native_log), "native_report": str(native_report),
              "native": native, "python": python}
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    args.output.with_suffix(".md").write_text(summary, encoding="utf-8")
    print(f"Report: {args.output}", flush=True)


if __name__ == "__main__":
    try:
        run()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Benchmark failed: {error}", file=sys.stderr)
        sys.exit(1)
