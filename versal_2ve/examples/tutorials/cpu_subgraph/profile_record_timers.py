#!/usr/bin/env python3
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
"""Summarize FlexML/VART record_timer JSON files in milliseconds.

Usage:
    python3 profile_record_timers.py [run_dir]

The script expects these files in the run directory:
    - record_timer_inference*.json
    - record_timer_subgraph_cpu_ts.json
    - record_timer_ts.json
"""

from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path
from statistics import mean
from typing import Any


NS_PER_MS = 1_000_000.0


def parse_ns(value: Any) -> int:
    """Parse values like '123 ns' or 123 into integer nanoseconds."""
    if isinstance(value, int):
        return value
    match = re.search(r"-?\d+", str(value))
    if not match:
        raise ValueError(f"Cannot parse nanosecond value: {value!r}")
    return int(match.group(0))


def ns_to_ms(value_ns: int | float) -> float:
    return value_ns / NS_PER_MS


def fmt_ms(value_ms: float) -> str:
    return f"{value_ms:,.2f} ms"


def print_table(title: str, headers: list[str], rows: list[list[str]]) -> None:
    print(f"\n{title}")
    widths = [len(header) for header in headers]
    for row in rows:
        for idx, cell in enumerate(row):
            widths[idx] = max(widths[idx], len(cell))

    def line(char: str = "-") -> str:
        return "+" + "+".join(char * (width + 2) for width in widths) + "+"

    print(line("-"))
    print("| " + " | ".join(header.ljust(widths[idx]) for idx, header in enumerate(headers)) + " |")
    print(line("-"))
    for row in rows:
        print("| " + " | ".join(cell.ljust(widths[idx]) for idx, cell in enumerate(row)) + " |")
    print(line("-"))


def load_required_json(run_dir: Path) -> tuple[list[Path], Path, Path]:
    inference_files = sorted(run_dir.glob("record_timer_inference*.json"))
    subgraph_cpu = run_dir / "record_timer_subgraph_cpu_ts.json"
    device_ts = run_dir / "record_timer_ts.json"

    missing = []
    if not inference_files:
        missing.append("record_timer_inference*.json")
    if not subgraph_cpu.exists():
        missing.append(subgraph_cpu.name)
    if not device_ts.exists():
        missing.append(device_ts.name)
    if missing:
        raise FileNotFoundError(f"Missing required timer file(s): {', '.join(missing)}")

    return inference_files, subgraph_cpu, device_ts


def iter_timer_entries(inference_data: Any):
    """Yield (parent_name, entry) pairs from record_timer_inference JSON."""
    for block in inference_data:
        if not isinstance(block, dict):
            continue
        for parent_name, entries in block.items():
            if not isinstance(entries, list):
                continue
            for entry in entries:
                if isinstance(entry, dict):
                    yield parent_name, entry


def _file_has_execute_runner(data: Any) -> bool:
    """Return True if a parsed inference JSON contains any ExecuteRunner entries."""
    for block in data:
        if not isinstance(block, dict):
            continue
        for parent_name in block:
            if parent_name.startswith("ExecuteRunner"):
                return True
    return False


def collect_inference_timings(inference_files: list[Path]) -> dict[str, Any]:
    totals: dict[str, int] = defaultdict(int)
    cpu_ops: dict[str, int] = defaultdict(int)
    execute_runner_parent_total_ns = 0
    execute_runner_section_total_ns = 0
    cpu_op_parent_total_ns = 0
    earliest_start = None
    latest_end = None
    iteration_count = 0

    for timer_file in inference_files:
        data = json.loads(timer_file.read_text())
        # Only accumulate CPU Op timings from files that also contain ExecuteRunner
        # entries (subgraph index 0). Files for secondary CPU subgraphs record full
        # subgraph wall time including NPU sync wait and would double-count CPU ops.
        include_cpu_ops = _file_has_execute_runner(data)

        for parent_name, entry in iter_timer_entries(data):
            duration_ns = parse_ns(entry["duration"])
            start_ns = parse_ns(entry["start"])
            end_ns = parse_ns(entry["end"])
            earliest_start = start_ns if earliest_start is None else min(earliest_start, start_ns)
            latest_end = end_ns if latest_end is None else max(latest_end, end_ns)

            if parent_name == "CPU Tensor Map Setup":
                totals["cpu_tensor_map_setup_ns"] += duration_ns
            elif parent_name == "CPU Op Execution":
                if include_cpu_ops:
                    cpu_op_parent_total_ns += duration_ns
                    sections = entry.get("sections", {})
                    for op_name, op_data in sections.items():
                        if isinstance(op_data, list):
                            op_duration_ns = sum(parse_ns(item.get("duration", 0)) for item in op_data)
                        else:
                            op_duration_ns = parse_ns(op_data)
                        cpu_ops[op_name] += op_duration_ns
            elif parent_name.startswith("ExecuteRunner preparation"):
                totals["execute_runner_preparation_ns"] += duration_ns
            elif parent_name.startswith("ExecuteRunner execution"):
                iteration_count += 1
                execute_runner_parent_total_ns += duration_ns
                for section_name, section_entries in entry.get("sections", {}).items():
                    if not isinstance(section_entries, list):
                        continue
                    section_total_ns = sum(parse_ns(section["duration"]) for section in section_entries)
                    totals[f"execute_section:{section_name}"] += section_total_ns
                    execute_runner_section_total_ns += section_total_ns

    cpu_ops_total_ns = sum(cpu_ops.values())

    if cpu_ops_total_ns == 0 and cpu_op_parent_total_ns > 0:
        cpu_ops_total_ns = cpu_op_parent_total_ns
        cpu_ops["All CPU Ops"] = cpu_op_parent_total_ns

    identified_runtime_ns = (
        totals["execute_runner_preparation_ns"]
        + execute_runner_section_total_ns
        + cpu_ops_total_ns
        + totals["cpu_tensor_map_setup_ns"]
    )
    # The timer span is the full wall-clock interval covered by VART timers,
    # which is why it can exceed the sum of the named sections.
    timer_span_ns = (latest_end - earliest_start) if earliest_start is not None and latest_end is not None else 0
    n = max(iteration_count, 1)

    return {
        "totals": totals,
        "cpu_ops": dict(sorted(cpu_ops.items())),
        "cpu_ops_total_ns": cpu_ops_total_ns,
        "cpu_op_parent_total_ns": cpu_op_parent_total_ns,
        "execute_runner_parent_total_ns": execute_runner_parent_total_ns,
        "execute_runner_section_total_ns": execute_runner_section_total_ns,
        "identified_runtime_ns": identified_runtime_ns,
        "timer_span_ns": timer_span_ns,
        "iteration_count": n,
    }


def collect_context_init_ms(subgraph_cpu_path: Path) -> float:
    data = json.loads(subgraph_cpu_path.read_text())
    return ns_to_ms(sum(parse_ns(entry["duration"]) for entry in data.get("context_init", [])))


def collect_device_kernel_ms(device_ts_path: Path) -> float | None:
    data = json.loads(device_ts_path.read_text())
    freq_mhz = data.get("header", {}).get("clock_freq_MHz")
    entries = data.get("record_timer_ts", [])
    if not freq_mhz or not entries:
        return None

    segments = []
    current = []
    for entry in entries:
        # id==0 marks the start of a new device-timer segment for one execution.
        if entry.get("id") == 0 and current:
            segments.append(current)
            current = []
        current.append(entry)
    if current:
        segments.append(current)

    durations_ms = []
    for segment in segments:
        start_cycle = segment[0]["cycle"]
        end_cycle = None
        for entry in reversed(segment):
            if entry.get("id") == 169:
                end_cycle = entry["cycle"]
                break
        if end_cycle is None:
            end_cycle = segment[-1]["cycle"]
        durations_ms.append((end_cycle - start_cycle) / freq_mhz / 1000.0)

    return mean(durations_ms) if durations_ms else None


def build_tables(timings: dict[str, Any], context_init_ms: float, device_kernel_ms: float | None) -> None:
    n = timings["iteration_count"]
    totals = timings["totals"]
    cpu_ops = timings["cpu_ops"]

    def avg_ns(ns: int | float) -> float:
        return ns_to_ms(ns / n)

    # Convert accumulated totals into per-inference averages so runs with many
    # iterations can be compared directly.
    cpu_ops_total_ms = avg_ns(timings["cpu_ops_total_ns"])
    kernel_host_ms = avg_ns(totals["execute_section:Kernel Execution"])
    execute_runner_unattributed_ns = (
        timings["execute_runner_parent_total_ns"] - timings["execute_runner_section_total_ns"]
    )
    cpu_unattributed_ns = timings["cpu_op_parent_total_ns"] - timings["cpu_ops_total_ns"]
    timer_span_unattributed_ns = (
        timings["timer_span_ns"]
        - timings["execute_runner_parent_total_ns"]
        - timings["cpu_op_parent_total_ns"]
        - totals["cpu_tensor_map_setup_ns"]
        - totals["execute_runner_preparation_ns"]
    )

    overall_title = f"Overall Timing — Average per Inference ({n} iteration{'s' if n != 1 else ''})"
    overall_rows = [
        ["AIE Kernel Execution", fmt_ms(kernel_host_ms)],
        ["Input Quantization and Transformation", fmt_ms(avg_ns(totals["execute_section:Input Quantization and Transformation"]))],
        ["Output DeQuantization and Transformation", fmt_ms(avg_ns(totals["execute_section:Output DeQuantization and Transformation"]))],
        ["Create I/O BOs", fmt_ms(avg_ns(totals["execute_section:Create I/O BOs"]))],
        ["Input Sync", fmt_ms(avg_ns(totals["execute_section:Input Sync"]))],
        ["Output Sync", fmt_ms(avg_ns(totals["execute_section:Output Sync"]))],
        ["CPU Ops", fmt_ms(cpu_ops_total_ms)],
        ["Unattributed ExecuteRunner wall time", fmt_ms(avg_ns(execute_runner_unattributed_ns))],
        ["Total ExecuteRunner", fmt_ms(avg_ns(timings["execute_runner_parent_total_ns"]))],
        ["Unattributed CPU/gap wall time", fmt_ms(avg_ns(cpu_unattributed_ns + timer_span_unattributed_ns))],
        ["Total VART timer span", fmt_ms(avg_ns(timings["timer_span_ns"]))],
    ]
    print_table(overall_title, ["Phase", "Avg Duration"], overall_rows)

    focused_rows = [
        ["AIE Kernel Execution", fmt_ms(kernel_host_ms)],
        ["CPU Ops", fmt_ms(cpu_ops_total_ms)],
        ["AIE Kernel + CPU Ops", fmt_ms(kernel_host_ms + cpu_ops_total_ms)],
    ]
    print_table("AIE Kernel And CPU Ops Only", ["Phase", "Avg Duration"], focused_rows)

    cpu_rows = [[op_name, fmt_ms(avg_ns(duration_ns))] for op_name, duration_ns in cpu_ops.items()]
    cpu_rows.append(["Total CPU Ops", fmt_ms(cpu_ops_total_ms)])
    print_table("CPU Op Breakdown", ["CPU Op", "Avg Duration"], cpu_rows)

    notes_rows = [
        ["Iterations", str(n)],
        ["AIE device cycles average", fmt_ms(device_kernel_ms) if device_kernel_ms is not None else "n/a"],
        ["Identified runtime subtotal (avg)", fmt_ms(avg_ns(timings["identified_runtime_ns"]))],
        ["Full timer span (avg)", fmt_ms(avg_ns(timings["timer_span_ns"]))],
        ["Unattributed wall time (avg)", fmt_ms(avg_ns(timings["timer_span_ns"] - timings["identified_runtime_ns"]))],
        ["One-time context init", fmt_ms(context_init_ms)],
    ]
    print_table("Timer Accounting", ["Metric", "Value"], notes_rows)


def main() -> None:
    parser = argparse.ArgumentParser(description="Summarize record_timer JSON files in milliseconds.")
    parser.add_argument("run_dir", nargs="?", default=".", help="Directory containing record_timer JSON files")
    args = parser.parse_args()

    run_dir = Path(args.run_dir).expanduser().resolve()
    inference_files, subgraph_cpu_path, device_ts_path = load_required_json(run_dir)

    timings = collect_inference_timings(inference_files)
    context_init_ms = collect_context_init_ms(subgraph_cpu_path)
    device_kernel_ms = collect_device_kernel_ms(device_ts_path)

    print(f"Profiling: {run_dir}")
    print("Timer files:")
    for path in inference_files + [subgraph_cpu_path, device_ts_path]:
        print(f"  - {path.name}")

    build_tables(timings, context_init_ms, device_kernel_ms)


if __name__ == "__main__":
    main()
