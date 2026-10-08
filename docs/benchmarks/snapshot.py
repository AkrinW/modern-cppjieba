"""Build the published benchmark snapshot and README section from one Actions run."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
MODES = ["MIX", "MP", "FULL", "SEARCH"]
CORPORA = {"testlines": "Short sentences", "weicheng": "Weicheng"}
SERIES = [
    {"id": "cppjieba", "label": "CppJieba", "color": "#64748b", "marker": "s",
     "native_label": "Old C++ strings"},
    {"id": "modern", "label": "modern-cppjieba", "color": "#2563eb", "marker": "o",
     "native_label": "Neo C++ strings"},
    {"id": "rust", "label": "jieba-rs", "color": "#c77714", "marker": "D",
     "native_label": "Rust native owned strings"},
    {"id": "python", "label": "jieba (Python)", "color": "#13866f", "marker": "^"},
]
BEGIN = "<!-- benchmark:start -->"
END = "<!-- benchmark:end -->"


def command(*args):
    """Read build provenance without changing the checkout."""
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def fingerprint(path):
    """Hash the exact report bytes retained in the workflow artifact."""
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def record_environment(build_directory, output):
    """Capture compiler and runner metadata on the machine that measures the queries."""
    if os.environ.get("GITHUB_ACTIONS") != "true":
        raise ValueError("Published benchmark metadata must come from GitHub Actions")
    cache = {}
    for line in (build_directory / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if cache["CMAKE_BUILD_TYPE"] != "Release":
        raise ValueError("Published benchmarks require a Release build")
    revision = command("git", "rev-parse", "HEAD")
    if revision != os.environ["GITHUB_SHA"]:
        raise ValueError("The benchmark checkout differs from the workflow revision")
    compiler = cache["CMAKE_CXX_COMPILER"]
    lock = tomllib.loads((ROOT / "deps/rust-jieba/Cargo.lock").read_text(encoding="utf-8"))
    run_url = (f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}"
               f"/actions/runs/{os.environ['GITHUB_RUN_ID']}")
    metadata = {
        "measured_on": datetime.now(timezone.utc).date().isoformat(),
        "revision": revision,
        "environment": {
            "cpu": next(line.split(":", 1)[1].strip() for line in
                        Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")),
            "cpp_compiler": "GCC " + command(compiler, "-dumpfullversion"),
            "cpp_flags": shlex.split(cache["CMAKE_CXX_FLAGS"] + " " + cache["CMAKE_CXX_FLAGS_RELEASE"]),
            "build_type": cache["CMAKE_BUILD_TYPE"],
            "rust_compiler": " ".join(command("rustc", "--version").split()[:2]),
            "rust_profile": {"name": "release", "opt_level": 3},
        },
        "dependencies": {
            "cppjieba_revision": command("git", "-C", "deps/cppjieba", "rev-parse", "HEAD"),
            "jieba_rs_revision": command("git", "-C", "deps/jieba-rs", "rev-parse", "HEAD"),
            "jieba_rs_version": next(package["version"] for package in lock["package"]
                                     if package["name"] == "jieba-rs"),
        },
        "ci": {
            "repository_url": f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}",
            "run_url": run_url,
            "run_attempt": os.environ["GITHUB_RUN_ATTEMPT"],
            "runner_environment": os.environ["BENCHMARK_RUNNER_ENVIRONMENT"],
            "runner_image": os.environ["BENCHMARK_RUNNER_IMAGE"],
            "artifact": "benchmarks-linux-x64-release",
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def summarize_corpus(name, report):
    """Select owned-string timings and retain token differences without declaring a ranking."""
    if Path(report["inputs"]["text"]["path"]).name != f"{name}.utf8":
        raise ValueError(f"Report contains a different corpus: {name}")
    native = report["native"]
    if native["schema_version"] != 1 or any(native[key] <= 0 for key in
                                          ("lines", "utf8_bytes", "rounds", "samples")):
        raise ValueError(f"Invalid benchmark dimensions: {name}")
    if native["samples"] < 2:
        raise ValueError("Published error bars require multiple samples")
    methods = {method["name"]: method for method in native["methods"]}
    if len(native["methods"]) != len(MODES) or set(methods) != set(MODES):
        raise ValueError(f"Incomplete segmentation modes: {name}")
    timings = {}
    for series in SERIES:
        selected = []
        for mode in MODES:
            if series["id"] == "python":
                timing = report["python"][mode]
            else:
                matches = [entry for entry in methods[mode]["timings"]
                           if entry["label"] == series["native_label"]]
                if len(matches) != 1:
                    raise ValueError(f"Missing or duplicated owned-string timing: {name}/{mode}")
                timing = matches[0]
            values = [timing[key] for key in ("min_ms", "median_ms", "max_ms")]
            if not all(math.isfinite(value) and value > 0 for value in values) or values != sorted(values):
                raise ValueError(f"Invalid elapsed times: {name}/{mode}")
            selected.append(timing)
        timings[series["id"]] = {key: [timing[key] for timing in selected]
                                 for key in ("median_ms", "min_ms", "max_ms")}
    differences = {
        "cppjieba": [methods[mode]["old_neo_mismatches"] for mode in MODES],
        "rust": [methods[mode]["neo_rust_mismatches"] for mode in MODES],
        "python": [report["python"][mode]["neo_python_mismatches"] for mode in MODES],
    }
    if any(count < 0 or count > native["lines"] for counts in differences.values() for count in counts):
        raise ValueError(f"Invalid token difference counts: {name}")
    return {
        "id": name, "title": f"{CORPORA[name]} ({native['lines']} lines)",
        "source_path": f"test/testdata/{name}.utf8", "source_sha256": report["inputs"]["text"]["sha256"],
        "lines": native["lines"], "utf8_bytes": native["utf8_bytes"],
        "rounds_per_sample": native["rounds"], "samples": native["samples"],
        "timings": timings, "different_lines_vs_modern": differences,
    }


def build_snapshot(reports, metadata):
    """Reject reports mixed across revisions, inputs, binaries, or execution environments."""
    first = reports["testlines"]
    environment = first["environment"]
    if environment["repo_revision"] != metadata["revision"]:
        raise ValueError("Report revision differs from the measured checkout")
    if len(environment["cpu_affinity"]) != 1:
        raise ValueError("Published benchmarks must be pinned to one CPU")
    for report in reports.values():
        if report["environment"] != environment:
            raise ValueError("Reports use different revisions or execution environments")
        for name in ("dictionary", "model", "native_executable"):
            if report["inputs"][name]["sha256"] != first["inputs"][name]["sha256"]:
                raise ValueError(f"Reports use different {name} inputs")
    return {
        "measured_on": metadata["measured_on"],
        "repository_revision_at_measurement": metadata["revision"],
        "environment": {
            **metadata["environment"], "platform": environment["platform"],
            "cpu_affinity": environment["cpu_affinity"], "python": environment["python"],
            "python_version": f"{environment['implementation']} {environment['python'].split()[0]}",
        },
        "dependencies": {
            **metadata["dependencies"], "python_jieba_version": environment["jieba_version"],
            "python_jieba_revision": environment["jieba_revision"],
        },
        "ci": metadata["ci"],
        "measurement": {
            "output_contract": "independent owned strings, including materialization and per-call destruction",
            "excluded": ["input reading", "engine initialization", "correctness checks", "warm-up"],
            "elapsed_time_unit": "milliseconds per sample", "plotted_unit": "UTF-8 MiB/s",
            "segmentation_style": "CPP", "user_dictionary": False,
        },
        "input_sha256": {name: first["inputs"][name]["sha256"] for name in ("dictionary", "model")},
        "native_executable_sha256": first["inputs"]["native_executable"]["sha256"],
        "modes": MODES, "series": SERIES,
        "corpora": [summarize_corpus(name, reports[name]) for name in CORPORA],
    }


def update_readme(readme, snapshot, snapshot_path):
    """Replace only the marked measurement section, preserving the surrounding documentation."""
    if readme.count(BEGIN) != 1 or readme.count(END) != 1 or readme.index(BEGIN) > readme.index(END):
        raise ValueError("README must contain exactly one ordered benchmark marker pair")
    revision = snapshot["repository_revision_at_measurement"]
    env = snapshot["environment"]
    ci = snapshot["ci"]
    deps = snapshot["dependencies"]
    lines = [
        f"数据来自 [GitHub Actions]({ci['run_url']})，测试日期：{snapshot['measured_on']}。",
        f"测量提交：[`{revision[:7]}`]({ci['repository_url']}/commit/{revision})。",
        "各实现使用同一主词典和 HMM 模型，关闭用户词典；modern-cppjieba 使用 `CPP` 规则。", "",
        "![CppJieba、modern-cppjieba、jieba-rs 和 Python jieba 的分词吞吐量，线性坐标](docs/benchmarks/comparison.svg)", "",
        "吞吐量单位为 MiB/s，越高越好。曲线取各样本的中位数，误差线表示最小、最大吞吐量。",
        "各路径均返回独立字符串，计时覆盖分词、字符串构造和释放；语料读取、词典加载和预热在计时前完成。", "",
        f"- 环境：GitHub 托管 `{ci['runner_image']}`，{env['cpu']}，绑定 CPU {env['cpu_affinity'][0]}。",
        f"- 工具链：{env['cpp_compiler']}（`{' '.join(env['cpp_flags'])}`）、"
        f"{env['rust_compiler']}（`opt-level=3`）、{env['python_version']}。",
        f"- 比较版本：jieba-rs {deps['jieba_rs_version']}、Python jieba {deps['python_jieba_version']}。",
    ]
    for corpus in snapshot["corpora"]:
        lines.append(f"- 语料 `{corpus['id']}`：{corpus['lines']:,} 行、{corpus['utf8_bytes']:,} 字节，"
                     f"每样本 {corpus['rounds_per_sample']:,} 轮，共 {corpus['samples']} 个样本。")
    lines.extend(["", "不同实现的分词规则存在差异，词序列差异计数保存在数据快照中。",
                  "共享 runner 的耗时会有波动，图表仅代表这次测量。", "",
                  f"[测试数据]({snapshot_path}) · [复现与 CI 更新方法](benchmark/README.md)"])
    prefix, remaining = readme.split(BEGIN)
    _, suffix = remaining.split(END)
    return prefix + BEGIN + "\n\n" + "\n".join(lines) + "\n\n" + END + suffix


def generate(reports_directory, snapshot_path, readme_path):
    """Validate complete reports before writing the numeric snapshot or README."""
    metadata_path = reports_directory / "environment.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    reports = {name: json.loads((reports_directory / f"{name}.json").read_text(encoding="utf-8"))
               for name in CORPORA}
    snapshot = build_snapshot(reports, metadata)
    snapshot["raw_reports"] = {name: {"path": f"{name}.json",
                                      "sha256": fingerprint(reports_directory / f"{name}.json")}
                               for name in CORPORA}
    snapshot["raw_environment_sha256"] = fingerprint(metadata_path)
    relative_path = Path(os.path.relpath(snapshot_path, readme_path.parent)).as_posix()
    readme = update_readme(readme_path.read_text(encoding="utf-8"), snapshot, relative_path)
    snapshot_path.parent.mkdir(parents=True, exist_ok=True)
    snapshot_path.write_text(json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    readme_path.write_text(readme, encoding="utf-8")


def main():
    """Separate runner metadata capture from rendering already completed measurements."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    record = commands.add_parser("record-environment")
    record.add_argument("--build-directory", type=Path, required=True)
    record.add_argument("--output", type=Path, required=True)
    render = commands.add_parser("generate")
    render.add_argument("--reports", type=Path, required=True)
    render.add_argument("--snapshot", type=Path, required=True)
    render.add_argument("--readme", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "record-environment":
        record_environment(args.build_directory, args.output)
    else:
        generate(args.reports, args.snapshot, args.readme)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Benchmark snapshot failed: {error}", file=sys.stderr)
        sys.exit(1)
