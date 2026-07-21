#!/usr/bin/env python3
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
"""Inspect and display VAIML partition connectivity metadata.

Usage:
    python3 inspect_connectivity_metadata.py <path-to-connectivity_metadata.json>

Example:
    python3 inspect_connectivity_metadata.py my_cache/yolov7_NMS/connectivity_metadata.json
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from collections import defaultdict


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    meta_path = Path(sys.argv[1]).expanduser().resolve()
    if not meta_path.exists():
        print(f"Error: File not found: {meta_path}")
        sys.exit(1)
    
    meta = json.loads(meta_path.read_text())
    
    # ── execution order ──────────────────────────────────────────────────
    print("=" * 80)
    print("VAIML Partition Connectivity Metadata")
    print("=" * 80)
    print()
    
    execution_order = meta.get("execution_order", [])
    # This order is the runtime handoff sequence across CPU and NPU partitions.
    print("Execution Order (graph partitions):")
    for i, partition_name in enumerate(execution_order, 1):
        print(f"  {i}. {partition_name}")
    print()
    
    # ── model inputs/outputs ─────────────────────────────────────────────
    model_inputs = meta.get("inputs", [])
    model_outputs = meta.get("outputs", [])
    
    print("Model Inputs:")
    for inp in model_inputs:
        print(f"  - {inp}")
    print()
    
    print("Model Outputs:")
    for out in model_outputs:
        print(f"  - {out}")
    print()
    
    # ── graph edges ──────────────────────────────────────────────────────
    edges = meta.get("graph_edges", [])
    print(f"Total Graph Edges: {len(edges)}")
    print()
    
    # Separate graph-boundary edges from partition-to-partition edges so the
    # printed summary mirrors how data enters, moves through, and exits the graph.
    graph_input_edges = [
        e for e in edges
        if e.get("source_partition") == "__graph_input__"
    ]
    graph_output_edges = [
        e for e in edges
        if e.get("destination_partition") == "__graph_output__"
    ]
    inter_partition_edges = [
        e for e in edges
        if e.get("source_partition") not in ("__graph_input__", "__graph_output__")
        and e.get("destination_partition") not in ("__graph_input__", "__graph_output__")
    ]
    
    print(f"Graph Input Edges: {len(graph_input_edges)}")
    for e in graph_input_edges:
        src = e.get("source_partition")
        dst = e.get("destination_partition")
        tensor_name = e.get("temp_buffer_name", "?")
        shape = e.get("shape", [])
        dtype = e.get("data_type", "?")
        shape_str = "×".join(str(s) for s in shape) if shape else "?"
        print(f"  {src} → {dst}: {tensor_name} [{shape_str}] ({dtype})")
    print()
    
    print(f"Inter-Partition Edges: {len(inter_partition_edges)}")
    for e in inter_partition_edges:
        src = e.get("source_partition")
        dst = e.get("destination_partition")
        tensor_name = e.get("temp_buffer_name", "?")
        shape = e.get("shape", [])
        dtype = e.get("data_type", "?")
        shape_str = "×".join(str(s) for s in shape) if shape else "?"
        print(f"  {src} → {dst}: {tensor_name} [{shape_str}] ({dtype})")
    print()
    
    print(f"Graph Output Edges: {len(graph_output_edges)}")
    for e in graph_output_edges:
        src = e.get("source_partition")
        dst = e.get("destination_partition")
        tensor_name = e.get("temp_buffer_name", "?")
        shape = e.get("shape", [])
        dtype = e.get("data_type", "?")
        shape_str = "×".join(str(s) for s in shape) if shape else "?"
        print(f"  {src} → {dst}: {tensor_name} [{shape_str}] ({dtype})")
    print()
    
    # ── verification summary ─────────────────────────────────────────────
    print("=" * 80)
    print("Partition Verification Summary")
    print("=" * 80)
    
    num_partitions = len(execution_order)
    print(f"Total partitions: {num_partitions}")
    print()
    
    print("Partition names in execution order:")
    for i, p in enumerate(execution_order, 1):
        print(f"  {i}. {p}")
    print()
    
    print(f"Inter-partition edges: {len(inter_partition_edges)}")
    print()
    
    if num_partitions > 0 and len(inter_partition_edges) > 0:
        print("✓ Graph partition and connectivity successful!")
    else:
        print("✗ Graph partition or connectivity may not be complete.")


if __name__ == "__main__":
    main()
