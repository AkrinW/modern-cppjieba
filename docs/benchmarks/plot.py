"""Render the README benchmark snapshot with Matplotlib 3.3 or newer."""

import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator


def draw(snapshot, output):
    """Plot measured throughput and min/max ranges for each corpus and implementation."""
    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "font.size": 11,
        "text.color": "#17233b",
        "axes.labelcolor": "#17233b",
        "svg.hashsalt": "modern-cppjieba-benchmarks",
    })
    figure, axes = plt.subplots(1, len(snapshot["corpora"]), figsize=(12, 6.5), sharey=True, squeeze=False)
    modes = snapshot["modes"]
    positions = list(range(len(modes)))
    all_bounds = []
    for axis, corpus in zip(axes[0], snapshot["corpora"]):
        mib_per_sample = corpus["utf8_bytes"] * corpus["rounds_per_sample"] / (1024 * 1024)
        for series in snapshot["series"]:
            timing = corpus["timings"][series["id"]]
            throughput = [mib_per_sample * 1000 / value for value in timing["median_ms"]]
            lower = [mib_per_sample * 1000 / value for value in timing["max_ms"]]
            upper = [mib_per_sample * 1000 / value for value in timing["min_ms"]]
            errors = [
                [middle - low for middle, low in zip(throughput, lower)],
                [high - middle for middle, high in zip(throughput, upper)],
            ]
            all_bounds.extend(lower + upper)
            axis.errorbar(
                positions, throughput, yerr=errors, label=series["label"],
                color=series["color"], marker=series["marker"], linewidth=2.2,
                markersize=6.5, capsize=3, elinewidth=1,
            )
            for position, value in zip(positions, throughput):
                label = "{:.2f}".format(value) if value < 10 else "{:.1f}".format(value)
                axis.annotate(label, (position, value), xytext=(0, 10),
                              textcoords="offset points", ha="center", fontsize=9, color=series["color"])
        axis.set_title(corpus["title"], loc="left", fontsize=13, fontweight="bold", pad=20)
        axis.text(
            0, 1.025,
            "{:,} bytes | {:,} rounds/sample | {} samples".format(
                corpus["utf8_bytes"], corpus["rounds_per_sample"], corpus["samples"]),
            transform=axis.transAxes, fontsize=9, color="#64748b",
        )
        axis.set_xticks(positions)
        axis.set_xticklabels(modes)
        axis.set_xlim(-0.2, len(modes) - 0.8)
        axis.set_xlabel("Segmentation mode", labelpad=10)
        axis.yaxis.set_major_locator(MaxNLocator(nbins=6, integer=True))
        axis.grid(axis="y", which="major", color="#e2e8f0", linewidth=0.8)
        axis.set_axisbelow(True)
        axis.tick_params(axis="both", which="both", length=0, pad=8, colors="#475569")
        for side in ("top", "right"):
            axis.spines[side].set_visible(False)
        for side in ("left", "bottom"):
            axis.spines[side].set_color("#cbd5e1")

    axes[0][0].set_ylim(0, max(all_bounds) * 1.15)
    axes[0][0].set_ylabel("Throughput (MiB/s)\nHigher is better", labelpad=12)
    figure.suptitle("Chinese segmentation throughput", x=0.06, y=0.96, ha="left", fontsize=21, fontweight="bold")
    environment = snapshot["environment"]
    figure.text(0.06, 0.875, "Owned string outputs | {} | CPU affinity: {}".format(
        environment["build_type"], ", ".join(str(cpu) for cpu in environment["cpu_affinity"])),
        color="#64748b", fontsize=11)
    handles, labels = axes[0][0].get_legend_handles_labels()
    figure.legend(handles, labels, loc="upper left", bbox_to_anchor=(0.05, 0.85), ncol=4, frameon=False)
    figure.text(
        0.06, 0.055,
        "{} | {} | {} / {} / {}".format(snapshot["measured_on"], environment["cpu"],
            environment["cpp_compiler"], environment["rust_compiler"], environment["python_version"]),
        fontsize=9, color="#64748b",
    )
    figure.text(0.06, 0.018, "Medians with min/max throughput whiskers. Setup and warm-up excluded.",
                fontsize=9, color="#64748b")
    figure.subplots_adjust(left=0.11, right=0.98, top=0.68, bottom=0.20, wspace=0.18)
    figure.savefig(output, dpi=160, facecolor="white", metadata={"Date": snapshot["measured_on"]})
    if output.suffix.lower() == ".svg":
        svg = output.read_text(encoding="utf-8")
        output.write_text("\n".join(line.rstrip() for line in svg.splitlines()) + "\n", encoding="utf-8")
    plt.close(figure)


def main():
    """Regenerate a chart from the committed numeric snapshot without running benchmarks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    snapshot = json.loads(args.snapshot.read_text(encoding="utf-8"))
    draw(snapshot, args.output)


if __name__ == "__main__":
    main()
