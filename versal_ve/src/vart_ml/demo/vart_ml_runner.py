#!/usr/bin/env python3

# Copyright 2024 Advanced Micro Devices Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#


import argparse
import copy
import functools
import json
import os
import time

import numpy as np
import VART

np.random.seed(0)

error_file = "error_stat.txt"

# Default gold-check tolerances
DEFAULT_GOLD_RTOL = 1e-5
DEFAULT_GOLD_ATOL = 0.01


# Generic demo to run a snapshot with multiple options.
# This demo uses random input by default.


def get_snapshot_tolerance(snapshot_path):
    """Read rtol/atol from tolerance.txt in the snapshot directory, if present."""
    rtol, atol = DEFAULT_GOLD_RTOL, DEFAULT_GOLD_ATOL
    tol_file = os.path.join(snapshot_path, "tolerance.txt")
    if os.path.isfile(tol_file):
        with open(tol_file) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) >= 2:
                    rtol, atol = float(parts[0]), float(parts[1])
                break

    if "GOLD_RTOL" in os.environ:
        rtol = float(os.environ["GOLD_RTOL"])
    if "GOLD_ATOL" in os.environ:
        atol = float(os.environ["GOLD_ATOL"])
    return rtol, atol


@functools.cache
def _load_embedded_export(snapshot_dir):
    export_dir = os.path.join(snapshot_dir, "embedded_export")
    with open(os.path.join(export_dir, "main_attr.json")) as json_file:
        main_attr = json.load(json_file)
    with open(os.path.join(export_dir, "main.json")) as json_file:
        main = json.load(json_file)
    return main_attr, main


def get_snapshot_input_names(snapshot_dir):
    """Get external graph input tensor names from main_attr.json."""
    main_attr, _ = _load_embedded_export(snapshot_dir)
    return main_attr.get("inputs", [])


def get_formats_from_snapshot(path):
    main_attr, _ = _load_embedded_export(path)
    name = main_attr["__properties__"]["id"]

    with open(
        os.path.join(
            path,
            f"embedded_export/{name}_embd_export_vaisw_subgraph_call/snapshot.dump.uploadInfos",
        ),
    ) as jsonFile:
        snapshot = json.load(jsonFile)
        return [
            snapshot["uploads"][key]["ddrInterface"]["shape"] for key in snapshot["uploads"].keys()
        ]


def _arm_op_input_format_for_tensor(main, input_tensor_name):
    """Return layer.params.input_format if tensor is the input of an arm op."""
    for node in main.values():
        if not isinstance(node, dict):
            continue
        if node.get("execution", {}).get("device") != "ON_ARM":
            continue
        if node.get("inputs", [None])[0] == input_tensor_name:
            return node["layer"]["params"]["input_format"]
    return None


def get_reshapes_from_snapshot(snapshot_dir):
    """Return (input_reshapes, output_reshapes) from boundary RESHAPE arm ops.

    Each input_reshapes entry is None or a target shape list.
    Each output_reshapes entry is None or (target_shape, channel_count).
    Returns (None, None) if the snapshot has no boundary reshapes.
    """
    main_attr, main = _load_embedded_export(snapshot_dir)
    input_names = main_attr.get("inputs", [])
    output_names = main_attr.get("outputs", [])

    has_boundary = False
    for node in main.values():
        if not isinstance(node, dict):
            continue
        if node.get("execution", {}).get("device") != "ON_ARM":
            continue
        params = node.get("layer", {}).get("params", {})
        if params.get("type") != "RESHAPE":
            continue
        if params.get("input_format") != params.get("output_format"):
            continue
        if any(inp in set(input_names) for inp in node.get("inputs", [])):
            has_boundary = True
            break
        if any(out in set(output_names) for out in node.get("outputs", [])):
            has_boundary = True
            break

    if not has_boundary:
        return None, None

    def find_reshape(tensor_name, match_key):
        for val in main.values():
            if not isinstance(val, dict) or "layer" not in val:
                continue
            entries = val.get(match_key, [])
            if entries and entries[0] == tensor_name:
                params = val["layer"]["params"]
                if params.get("type") == "RESHAPE":
                    return val
                break
        return None

    input_reshapes = []
    for name in input_names:
        node = find_reshape(name, "inputs")
        input_reshapes.append(node["layer"]["params"]["shape"] if node else None)

    output_reshapes = []
    for name in output_names:
        node = find_reshape(name, "outputs")
        if node:
            shape = node["layer"]["params"]["shape"]
            channel_count = _trace_slice_channel_count(main, node)
            output_reshapes.append((shape, channel_count))
        else:
            output_reshapes.append(None)

    return input_reshapes, output_reshapes


def _trace_slice_channel_count(main, reshape_node):
    """Trace backwards from a RESHAPE node to find the SLICE channel count."""
    tensor_name = reshape_node["inputs"][0]
    while tensor_name:
        for val in main.values():
            if not isinstance(val, dict) or "layer" not in val:
                continue
            if val.get("outputs", [None])[0] != tensor_name:
                continue
            params = val["layer"]["params"]
            if params.get("type") == "SLICE" and 1 in params.get("axes", []):
                idx = params["axes"].index(1)
                return params["ends"][idx] - params["starts"][idx]
            tensor_name = val["inputs"][0] if val.get("inputs") else None
            break
        else:
            break
    return None


def get_original_input_shape_formats(snapshot_dir):
    """Return per-input layout of dumped reference inputs for one snapshot."""
    _, main = _load_embedded_export(snapshot_dir)
    input_names = get_snapshot_input_names(snapshot_dir)
    upload_formats = get_formats_from_snapshot(snapshot_dir)

    formats = []
    for idx, input_name in enumerate(input_names):
        arm_op_format = _arm_op_input_format_for_tensor(main, input_name)
        if arm_op_format is not None:
            formats.append(arm_op_format)
        elif idx < len(upload_formats):
            formats.append(upload_formats[idx])
        else:
            formats.append(main.get(input_name, {}).get("params", {}).get("ttype"))
    return formats


def get_snapshot_input_shapes(path):
    """Get input tensor names/shapes from main_attr.json and main.json."""
    _, main = _load_embedded_export(path)

    return [
        {"name": input_name, "shape": main.get(input_name, {}).get("params", {}).get("shape")}
        for input_name in get_snapshot_input_names(path)
    ]


def graph_input_consumed_by_onnx_subgraph(snapshot_dir):
    """Return True if any graph input is consumed by a CALL_ONNX subgraph."""
    input_names = set(get_snapshot_input_names(snapshot_dir))
    _, main = _load_embedded_export(snapshot_dir)

    for node in main.values():
        if not isinstance(node, dict):
            continue
        if node.get("layer", {}).get("params", {}).get("type") != "CALL_ONNX":
            continue
        if any(node_input in input_names for node_input in node.get("inputs", [])):
            return True
    return False


def get_snapshot_input_format(snapshot_dir, requested_input_format):
    """Return effective input format for one snapshot."""
    if requested_input_format == "from_snapshot":
        return "from_snapshot"

    if graph_input_consumed_by_onnx_subgraph(snapshot_dir):
        print(
            f"Warning: snapshot {snapshot_dir} has graph inputs consumed by a CALL_ONNX subgraph. "
            f"Ignoring --input_format {requested_input_format}, using from_snapshot instead."
        )
        return "from_snapshot"

    input_specs = get_snapshot_input_shapes(snapshot_dir)
    non_4d_inputs = [
        spec
        for spec in input_specs
        if not isinstance(spec["shape"], list) or len(spec["shape"]) != 4
    ]
    if non_4d_inputs:
        bad_inputs_msg = ", ".join(
            f"{spec['name']} shape={spec['shape']}" for spec in non_4d_inputs
        )
        print(
            f"Warning: snapshot {snapshot_dir} has non-4D inputs ({bad_inputs_msg}). "
            f"Ignoring --input_format {requested_input_format}, using from_snapshot instead."
        )
        return "from_snapshot"

    if "NHWC" in get_formats_from_snapshot(snapshot_dir) and requested_input_format == "NCHW":
        print(
            f"Warning: snapshot {snapshot_dir} has NHWC inputs. "
            f"Ignoring --input_format {requested_input_format}, using from_snapshot instead."
        )
        return "from_snapshot"

    if any(get_reshapes_from_snapshot(snapshot_dir)):
        print(
            f"Warning: snapshot {snapshot_dir} has a boundary RESHAPE arm op. "
            f"Ignoring --input_format {requested_input_format}, using from_snapshot instead."
        )
        return "from_snapshot"

    return requested_input_format


def data_to_numpy(data):
    return [data] if isinstance(data, np.ndarray) else data


def dump_outputs(path, data, name):
    np.savez(os.path.join(path, name), *data_to_numpy(data))


def transform_input_data(data, dest_data_format, shape_format):
    if dest_data_format == "NHWC" and shape_format == "NCHW":
        data = np.transpose(data, (0, 2, 3, 1)).copy()

    elif dest_data_format == "NCHW" and shape_format == "NHWC":
        data = np.transpose(data, (0, 3, 1, 2)).copy()
    return data


def copy_to_ddr(dst, src):
    if dst.ndim == 1:
        if dst.size >= 64 and dst.size % 64 == 0:
            np.copyto(dst, src)
        else:
            for i in range(dst.size):
                dst[i] = src[i]
        return
    # Recursively copy data in DDR
    for i in range(dst.shape[0]):
        copy_to_ddr(dst[i], src[i])


def _parse_gold_filename(filename):
    """Parse ``{tensor_name}_{batchIdx}_{dtype}.npy`` using right-to-left underscore splitting."""
    stem = filename.rsplit(".", 1)[0]
    type_sep = stem.rfind("_")
    batch_sep = stem.rfind("_", 0, type_sep)
    batch_idx = int(stem[batch_sep + 1 : type_sep])
    name = stem[:batch_sep]
    return name, batch_idx


def _load_gold_dir(gold_dir, tensor_names):
    """Load ``.npy`` gold files from *gold_dir*, returning ``batches[batch_idx][tensor_idx]``."""
    files_by_name = {}
    for f in os.listdir(gold_dir):
        if not f.endswith(".npy"):
            continue
        name, batch_idx = _parse_gold_filename(f)
        files_by_name.setdefault(name, {})[batch_idx] = os.path.join(gold_dir, f)

    assert files_by_name, f"No .npy gold files found in {gold_dir}"
    nb_batches = max(max(b.keys()) for b in files_by_name.values()) + 1
    batches = []
    for b in range(nb_batches):
        tensors = []
        for tname in tensor_names:
            assert tname in files_by_name and b in files_by_name[tname], (
                f"Missing gold file for tensor '{tname}' batch {b} in {gold_dir}"
            )
            tensors.append(np.load(files_by_name[tname][b]))
        batches.append(tensors)
    return batches


def load_golds(args, runners, inputs_types, input_formats, input_shape_formats):
    """Load inputs/outputs from ``embedded_export/golds/`` npy files."""
    all_inputs_per_runner = []
    all_outputs_per_runner = []

    for snap_idx, (snap, runner) in enumerate(zip(args.snapshot, runners)):
        golds_dir = os.path.join(snap, "embedded_export", "golds")
        input_batches = _load_gold_dir(os.path.join(golds_dir, "inputs"), runner.input_names)
        output_batches = _load_gold_dir(os.path.join(golds_dir, "outputs"), runner.output_names)

        for tensors in input_batches:
            for i, arr in enumerate(tensors):
                tensors[i] = transform_input_data(
                    arr.astype(inputs_types[snap_idx][i]),
                    input_formats[snap_idx],
                    input_shape_formats[snap_idx][i],
                )

        all_inputs_per_runner.append(input_batches)
        all_outputs_per_runner.append(output_batches)

    nb_batches = len(all_inputs_per_runner[0])
    all_inputs = [
        [all_inputs_per_runner[r][b] for r in range(len(args.snapshot))] for b in range(nb_batches)
    ]
    all_output_refs = [
        [all_outputs_per_runner[r][b] for r in range(len(args.snapshot))] for b in range(nb_batches)
    ]

    return all_inputs, all_output_refs


def load_ios(args, inputs_types, input_formats, input_shape_formats):
    """Load and read inputs/outputs numpy archives"""

    # Get the inputs/outputs dir paths for all runners and ensure they exists
    inputs_dirs = [os.path.join(snap, "inputs") for snap in args.snapshot]
    output_refs_dirs = [os.path.join(snap, "outputs") for snap in args.snapshot]
    assert all(os.path.exists(inputs_dir) for inputs_dir in inputs_dirs), (
        "inputs folder is missing in the snapshot directory"
    )
    assert all(os.path.exists(output_refs_dir) for output_refs_dir in output_refs_dirs), (
        "outputs folder is missing in the snapshot directory"
    )

    # Get all inputs/outputs numpy archives paths from the dir paths
    input_npz_paths = [
        [os.path.join(input_dir, x) for x in sorted(os.listdir(input_dir))]
        for input_dir in inputs_dirs
    ]
    output_ref_npz_paths = [
        [os.path.join(output_ref_dir, x) for x in sorted(os.listdir(output_ref_dir))]
        for output_ref_dir in output_refs_dirs
    ]

    # Swap axes as we will iterate over batches first
    input_npz_paths = np.swapaxes(input_npz_paths, 0, 1)
    output_ref_npz_paths = np.swapaxes(output_ref_npz_paths, 0, 1)

    # Load inputs
    input_npzs = [
        [np.load(in_npz) for in_npz in input_npz_paths[idx]] for idx in range(len(input_npz_paths))
    ]

    all_inputs = [
        [
            [transform_input_data(inpz, runner_input_format, isf[0])]
            if isinstance(inpz, np.ndarray)
            else [
                transform_input_data(inpz[key].astype(dtype), runner_input_format, sf)
                for key, dtype, sf in zip(inpz.files, it, isf)
            ]
            for inpz, it, isf, runner_input_format in zip(
                inpz_batch, inputs_types, input_shape_formats, input_formats
            )
        ]
        for inpz_batch in input_npzs
    ]

    # Load reference outputs
    output_ref_npzs = [
        [np.load(out_ref_npz) for out_ref_npz in output_ref_npz_paths[idx]]
        for idx in range(len(output_ref_npz_paths))
    ]
    all_output_refs = [
        [
            [output_ref_npz]
            if isinstance(output_ref_npz, np.ndarray)
            else [output_ref_npz[key] for key in output_ref_npz.files]
            for output_ref_npz in output_ref_npz_batch
        ]
        for output_ref_npz_batch in output_ref_npzs
    ]

    return all_inputs, all_output_refs


def gen_rand_inputs(runners):
    """Generate 10 random batches of inputs for the given runners"""

    runners_input = []
    for runner in runners:
        runner_input = []
        for s, t in zip(runner.input_shapes, runner.input_types):
            # Generate per-sample to avoid a large float64 intermediate that could OOM.
            random_data = np.empty(s, dtype=t)
            for b in range(s[0]):
                random_data[b] = np.random.rand(*s[1:]).astype(t)
            runner_input.append(random_data)
        runners_input.append(runner_input)
    return [runners_input for i in range(10)]


def quantize_tensor(tensor, nat_dtype, coeff):
    """Quantize a single tensor"""

    # If there is no quantization to perform, return now
    if (tensor.dtype == nat_dtype) or (coeff == 0):
        return tensor

    # Quantize to (u)int8
    if nat_dtype.type in (np.int8, np.uint8):
        if tensor.itemsize == 1:
            pass
        elif tensor.itemsize == 4:
            tensor = np.stack([
                (tensor[b] * coeff).round().clip(-128, 127).astype(nat_dtype)
                for b in range(tensor.shape[0])
            ])
        else:
            raise Exception(f"Unsupported dtype {tensor.dtype} with itemsize {tensor.itemsize}")

    # Quantize to bfloat16
    elif nat_dtype.type == np.uint16:
        if tensor.itemsize == 2:
            tensor = tensor.view(np.uint16)
        elif tensor.itemsize == 4:
            tensor = np.right_shift(tensor.view(np.uint32), 16, dtype=np.uint32).view(np.uint16)[
                ..., 0::2
            ]
        elif tensor.itemsize == 8:
            tensor = np.right_shift(tensor.view(np.uint32), 16, dtype=np.uint32).view(np.uint16)[
                ..., 2::4
            ]
        else:
            raise Exception(f"Unsupported dtype {tensor.dtype} with itemsize {tensor.itemsize}")

    # Quantize to float32
    elif nat_dtype.type == np.float32:
        pass

    # Unsupported quantization
    else:
        raise Exception(f"Unsupported tensor dtype {tensor.dtype} vs runner {nat_dtype}")

    return tensor


def reorder_tensor(tensor, from_layout, to_layout, to_shape=(), from_shape=None):
    """Reorder a single tensor"""

    if from_shape is not None:
        tensor = np.reshape(tensor, from_shape)

    # Shapes are returned as lists by vart_ml_py_api while numpy use tensors to
    # store them. Align the types to allow comparison.
    if isinstance(to_shape, list):
        to_shape = tuple(to_shape)

    if from_layout == "NC":
        # If both source and destination layouts are NC, slice the C dimension.
        if to_layout == "NC":
            return tensor[0 : to_shape[0], 0 : to_shape[1]]
        # Going from FLAT layout to anything else only needs a reshape.
        return np.reshape(tensor, to_shape)

    if from_layout == to_layout:
        if tensor.shape == to_shape:
            return tensor
        if np.prod(tensor.shape) == np.prod(to_shape):
            return np.reshape(tensor, to_shape)
        return tensor

    # Convert from current layout to NHWC.

    if from_layout == "NHC":
        tensor = np.reshape(tensor, [tensor.shape[0], tensor.shape[1], 1, tensor.shape[2]])

    elif from_layout == "NWC":
        tensor = np.reshape(tensor, [tensor.shape[0], 1, tensor.shape[1], tensor.shape[2]])

    elif from_layout == "NCHW":
        tensor = np.transpose(tensor, (0, 2, 3, 1))

    elif (
        (from_layout == "NHWC")
        or (from_layout == "NHWC4")
        or (from_layout == "NHWC8")
        or (from_layout == "GENERIC")
    ):
        pass

    elif (from_layout == "NC4HW4") or (from_layout == "NC8HW8"):
        bigPixel = 4 if (from_layout == "NC4HW4") else 8

        N, C, H, W, _ = tensor.shape

        # Create an empty array with native shape format.
        new_C = C * bigPixel
        tmp = np.zeros((N, H, W, new_C), dtype=tensor.dtype)

        # Create arrays to index channels.
        c_idx = np.arange(new_C)  # Indexes in the destination tensor.
        p_idx = c_idx % bigPixel  # Position inside the bigPixel-wide block.
        block_idx = c_idx // bigPixel  # Which block it belongs to.

        # Broadcast indices to N, H, W.
        n_idx = np.arange(N)[:, None, None, None, None]
        h_idx = np.arange(H)[None, None, :, None, None]
        w_idx = np.arange(W)[None, None, None, :, None]

        # Use advanced indexing to scatter into tmp.
        tmp[..., c_idx] = tensor[n_idx, block_idx, h_idx, w_idx, p_idx]

        tensor = tmp

    elif from_layout == "NHW16C4WC":
        w_blk_size = 16
        c_blk_size = 4

        N, H, W_blk, C_blk, _, _ = tensor.shape
        W = W_blk * w_blk_size
        C = C_blk * c_blk_size

        # Create an empty NHWC array (may be larger than to_shape due to padding).
        tmp = np.zeros((N, H, W, C), dtype=tensor.dtype)

        # Create 1D arrays to index channels.
        c_idx = np.arange(C)
        c_blk_idx = c_idx % c_blk_size
        c_blk = c_idx // c_blk_size

        # Create 1D arrays to index width.
        w_idx = np.arange(W)
        w_blk_idx = w_idx % w_blk_size
        w_blk = w_idx // w_blk_size

        # Create 2D meshgrids to handle both W and C dimensions.
        w_idx_grid, c_idx_grid = np.meshgrid(w_idx, c_idx, indexing="ij")
        w_blk_idx_grid, c_blk_idx_grid = np.meshgrid(w_blk_idx, c_blk_idx, indexing="ij")
        w_blk_grid, c_blk_grid = np.meshgrid(w_blk, c_blk, indexing="ij")

        # Broadcast indices to N, H.
        n_idx = np.arange(N)[:, None, None, None]
        h_idx = np.arange(H)[None, :, None, None]

        # Use advanced indexing to gather from the blocked tensor into NHWC.
        tmp[n_idx, h_idx, w_idx_grid, c_idx_grid] = tensor[
            n_idx, h_idx, w_blk_grid, c_blk_grid, w_blk_idx_grid, c_blk_idx_grid
        ]

        tensor = tmp

    elif from_layout == "NH2HWC4C":
        N, H_blk, h_blk_size, W, C_blk, c_blk_size = tensor.shape
        H = H_blk * h_blk_size
        C = C_blk * c_blk_size

        # Create an empty NHWC array (may be larger than to_shape due to padding).
        tmp = np.zeros((N, H, W, C), dtype=tensor.dtype)

        h_idx = np.arange(H)
        h_b = (h_idx // h_blk_size)[:, None, None]  # (H, 1, 1) → (1, H, 1, 1)
        h_bi = (h_idx % h_blk_size)[:, None, None]  # (H, 1, 1)
        h_i = h_idx[:, None, None]  # (H, 1, 1)

        c_idx = np.arange(C)
        c_b = (c_idx // c_blk_size)[None, None, :]  # (1, 1, C) → (1, 1, 1, C)
        c_bi = (c_idx % c_blk_size)[None, None, :]  # (1, 1, C)
        c_i = c_idx[None, None, :]  # (1, 1, C)

        n_idx = np.arange(N)[:, None, None, None]  # (N, 1, 1, 1)
        w_i = np.arange(W)[None, :, None]  # (1, W, 1) → (1, 1, W, 1)

        # Broadcasts to (N, H, W, C).
        tmp[n_idx, h_i, w_i, c_i] = tensor[n_idx, h_b, h_bi, w_i, c_b, c_bi]

        tensor = tmp

    elif from_layout == "NH2C4HWC":
        N, H_blk, C_blk, h_blk_size, W, c_blk_size = tensor.shape
        H = H_blk * h_blk_size
        C = C_blk * c_blk_size

        # Create an empty NHWC array (may be larger than to_shape due to padding).
        tmp = np.zeros((N, H, W, C), dtype=tensor.dtype)

        h_idx = np.arange(H)
        h_b = (h_idx // h_blk_size)[:, None, None]
        h_bi = (h_idx % h_blk_size)[:, None, None]
        h_i = h_idx[:, None, None]

        c_idx = np.arange(C)
        c_b = (c_idx // c_blk_size)[None, None, :]
        c_bi = (c_idx % c_blk_size)[None, None, :]
        c_i = c_idx[None, None, :]

        n_idx = np.arange(N)[:, None, None, None]
        w_i = np.arange(W)[None, :, None]

        # Broadcasts to (N, H, W, C).
        tmp[n_idx, h_i, w_i, c_i] = tensor[n_idx, h_b, c_b, h_bi, w_i, c_bi]

        tensor = tmp

    else:
        raise Exception(f"Unsupported tensor reorder's source layout: {from_layout}")

    # Convert from NHWC to destination layout.

    if to_layout == "NC":
        tensor = tensor[:, 0, 0, :]

    elif to_layout == "NCH":
        tensor = np.reshape(
            tensor, [tensor.shape[0], tensor.shape[1] * tensor.shape[2], tensor.shape[3]]
        )
        tensor = tensor.transpose((0, 2, 1))

    elif (to_layout == "NHC") or (to_layout == "NHW"):
        tensor = np.reshape(
            tensor, [tensor.shape[0], tensor.shape[1] * tensor.shape[2], tensor.shape[3]]
        )

    elif to_layout == "NCHW":
        tensor = tensor.transpose((0, 3, 1, 2))

    elif (to_layout == "NC4HW4") or (to_layout == "NC8HW8"):
        bigPixel = 4 if (to_layout == "NC4HW4") else 8

        N, H, W, C = tensor.shape

        # Create an empty array with native shape format.
        new_C = (C + (bigPixel - 1)) // bigPixel
        tmp = np.zeros((N, new_C, H, W, bigPixel), dtype=tensor.dtype)

        # Create arrays to index channels.
        c_idx = np.arange(C)  # Indexes in the original tensor.
        p_idx = c_idx % bigPixel  # Position inside the bigPixel-wide block.
        block_idx = c_idx // bigPixel  # Which block it belongs to.

        # Broadcast indices to N, H, W.
        n_idx = np.arange(N)[:, None, None, None]
        h_idx = np.arange(H)[None, :, None, None]
        w_idx = np.arange(W)[None, None, :, None]

        # Use advanced indexing to scatter into tmp.
        tmp[n_idx, block_idx, h_idx, w_idx, p_idx] = tensor[..., c_idx]

        tensor = tmp

    elif (
        (to_layout == "NHWC")
        or (to_layout == "NHWC4")
        or (to_layout == "NHWC8")
        or (to_layout == "GENERIC")
    ):
        pass

    elif to_layout == "NHW16C4WC":
        w_blk_size = 16
        c_blk_size = 4

        N, H, W, C = tensor.shape

        # Create an empty array with native shape format.
        new_W = (W + w_blk_size - 1) // w_blk_size
        new_C = (C + c_blk_size - 1) // c_blk_size
        tmp = np.zeros((N, H, new_W, new_C, w_blk_size, c_blk_size), dtype=tensor.dtype)

        # Create 1D arrays to index channels.
        c_idx = np.arange(C)  # Indexes in the original tensor.
        c_blk_idx = c_idx % c_blk_size  # Position inside the final block.
        c_blk = c_idx // c_blk_size  # Which block it belongs to.

        # Create 1D arrays to index width.
        w_idx = np.arange(W)  # Indexes in the original tensor.
        w_blk_idx = w_idx % w_blk_size  # Position inside the final block.
        w_blk = w_idx // w_blk_size  # Which block it belongs to.

        # Create 2D meshgrids to handle both W and C dimensions.
        # This creates all combinations of (W, C) indices.
        w_idx_grid, c_idx_grid = np.meshgrid(w_idx, c_idx, indexing="ij")
        w_blk_idx_grid, c_blk_idx_grid = np.meshgrid(w_blk_idx, c_blk_idx, indexing="ij")
        w_blk_grid, c_blk_grid = np.meshgrid(w_blk, c_blk, indexing="ij")

        # Broadcast indices to N, H.
        n_idx = np.arange(N)[:, None, None, None]
        h_idx = np.arange(H)[None, :, None, None]

        # Use advanced indexing to scatter into tmp.
        tmp[n_idx, h_idx, w_blk_grid, c_blk_grid, w_blk_idx_grid, c_blk_idx_grid] = tensor[
            ..., w_idx_grid, c_idx_grid
        ]

        tensor = tmp

    elif to_layout == "NH2HWC4C":
        _, new_H, h_blk_size, new_W, new_C, c_blk_size = to_shape

        N, H, W, C = tensor.shape

        # Create an empty array with native shape format.
        tmp = np.zeros((N, new_H, h_blk_size, new_W, new_C, c_blk_size), dtype=tensor.dtype)

        # Clamp to target capacity (NHWC tensor may be block-aligned and larger).
        h_idx = np.arange(min(H, new_H * h_blk_size))
        h_b = (h_idx // h_blk_size)[:, None, None]
        h_bi = (h_idx % h_blk_size)[:, None, None]
        h_i = h_idx[:, None, None]

        c_idx = np.arange(min(C, new_C * c_blk_size))
        c_b = (c_idx // c_blk_size)[None, None, :]
        c_bi = (c_idx % c_blk_size)[None, None, :]
        c_i = c_idx[None, None, :]

        n_idx = np.arange(N)[:, None, None, None]
        w_i = np.arange(min(W, new_W))[None, :, None]

        # Broadcasts to (N, H, W, C).
        tmp[n_idx, h_b, h_bi, w_i, c_b, c_bi] = tensor[n_idx, h_i, w_i, c_i]

        tensor = tmp

    elif to_layout == "NH2C4HWC":
        _, new_H, new_C, h_blk_size, new_W, c_blk_size = to_shape

        N, H, W, C = tensor.shape

        # Create an empty array with native shape format.
        tmp = np.zeros((N, new_H, new_C, h_blk_size, new_W, c_blk_size), dtype=tensor.dtype)

        # Clamp to target capacity (NHWC tensor may be block-aligned and larger).
        h_idx = np.arange(min(H, new_H * h_blk_size))
        h_b = (h_idx // h_blk_size)[:, None, None]
        h_bi = (h_idx % h_blk_size)[:, None, None]
        h_i = h_idx[:, None, None]

        c_idx = np.arange(min(C, new_C * c_blk_size))
        c_b = (c_idx // c_blk_size)[None, None, :]
        c_bi = (c_idx % c_blk_size)[None, None, :]
        c_i = c_idx[None, None, :]

        n_idx = np.arange(N)[:, None, None, None]
        w_i = np.arange(min(W, new_W))[None, :, None]

        # Broadcasts to (N, H, W, C).
        tmp[n_idx, h_b, c_b, h_bi, w_i, c_bi] = tensor[n_idx, h_i, w_i, c_i]

        tensor = tmp

    else:
        raise Exception(f"Unsupported tensor reorder's destination layout: {to_layout}")

    return tensor


def process_input(args, runners, inputs, input_shape_formats, ddr_bufs=None, input_reshapes=None):
    """Process input to native / zero copy format."""

    # Quantization + reorder fused per-tensor to limit peak memory.
    # Build a new list to avoid mutating the original (may be shared across iterations).
    processed = []
    for r, (runner, input_shape_format, reshapes) in enumerate(
        zip(
            runners,
            input_shape_formats,
            input_reshapes if input_reshapes is not None else [None] * len(runners),
        )
    ):
        runner_tensors = []
        for j in range(len(inputs[r])):
            t = quantize_tensor(inputs[r][j], runner.input_native_types[j], runner.input_coeffs[j])
            t = reorder_tensor(
                t,
                input_shape_format[j],
                runner.input_native_shape_formats[j],
                runner.input_native_shape[j],
                from_shape=reshapes[j] if reshapes else None,
            )
            runner_tensors.append(t)
        processed.append(runner_tensors)
    inputs = processed

    # If native, write the input in an array with native strides
    if args.in_native:
        for runner, input in zip(runners, inputs):
            for i in range(len(input)):
                # Build strides
                strides = runner.input_native_strides[i]

                # Build a base array large enough to contain the native input
                base_array = np.empty(
                    [input[i].shape[0] * strides[0]], dtype=runner.input_native_types[i]
                )

                # Create an array with native strides and copy input into it
                tmp = input[i]
                input[i] = np.lib.stride_tricks.as_strided(
                    base_array,
                    shape=input[i].shape,
                    strides=strides,
                )
                np.copyto(input[i], tmp)

    elif args.in_zero_copy:
        for k, (runner, input) in enumerate(zip(runners, inputs)):
            # Move inputs to DDR buffers while computing potential padding
            for i in range(len(input)):
                N = input[i].shape[0]
                native_shape_format = runner.input_native_shape_formats[i]

                if native_shape_format in ["NCHW", "NC4HW4", "NC8HW8"]:
                    padding = runner.input_native_shape[i][1] - input[i].shape[1]
                    if padding:
                        pad = np.zeros(
                            (N, padding, input[i].shape[2], input[i].shape[3]), dtype=input[i].dtype
                        )
                        input[i] = np.concatenate((input[i], pad), axis=1)

                elif native_shape_format in ["NHWC", "NHWC4", "NHWC8"]:
                    padding = runner.input_native_shape[i][3] - input[i].shape[3]
                    if padding:
                        pad = np.zeros(
                            (N, input[i].shape[1], input[i].shape[2], padding), dtype=input[i].dtype
                        )
                        input[i] = np.concatenate((input[i], pad), axis=3)

                elif native_shape_format == "NHW16C4WC":
                    padding = runner.input_native_shape[i][3] - input[i].shape[3]
                    if padding:
                        pad = np.zeros(
                            (
                                N,
                                input[i].shape[1],
                                input[i].shape[2],
                                padding,
                                input[i].shape[4],
                                input[i].shape[5],
                            ),
                            dtype=input[i].dtype,
                        )
                        input[i] = np.concatenate((input[i], pad), axis=3)

                elif native_shape_format == "NH2HWC4C":
                    padding = runner.input_native_shape[i][4] - input[i].shape[4]
                    if padding:
                        pad = np.zeros(
                            (
                                N,
                                input[i].shape[1],
                                input[i].shape[2],
                                input[i].shape[3],
                                padding,
                                input[i].shape[5],
                            ),
                            dtype=input[i].dtype,
                        )
                        input[i] = np.concatenate((input[i], pad), axis=4)

                elif native_shape_format == "NH2C4HWC":
                    # shape: (N, H_blk, C_blk, h_blk_size, W, c_blk_size) — pad C_blk at axis 2
                    padding = runner.input_native_shape[i][2] - input[i].shape[2]
                    if padding:
                        pad = np.zeros(
                            (
                                N,
                                input[i].shape[1],
                                padding,
                                input[i].shape[3],
                                input[i].shape[4],
                                input[i].shape[5],
                            ),
                            dtype=input[i].dtype,
                        )
                        input[i] = np.concatenate((input[i], pad), axis=2)

                elif native_shape_format == "GENERIC":
                    pass

                else:
                    raise Exception(
                        f"Unsupported input shape format {native_shape_format} for zero copy"
                    )

                if ddr_bufs[k][i].strides[0] != input[i].strides[0]:
                    copy_to_ddr(ddr_bufs[k][i], input[i])
                else:
                    np.copyto(ddr_bufs[k][i], input[i])

            # Override original inputs
            inputs[k] = ddr_bufs[k]

    return inputs


def get_channel_idx(layout):
    """Get channel index in shape from layout."""

    if layout in ["NC", "NCH", "NCHW", "NC4HW4", "NC8HW8"]:
        return 1

    if layout in ["NHC"]:
        return 2

    if layout in ["NHWC", "NHWC4", "NHWC8"]:
        return 3

    return -1


def process_output(
    args, runner, outputs, output_shape_formats, output_shapes, output_reshapes=None
):
    """Process vart_ml_runner output to snapshot format.

    Must be called on native / zero_copy outputs.
    """
    # Process native data to normal output
    for i in range(len(outputs)):
        if args.out_zero_copy and outputs[i].size * outputs[i].itemsize != outputs[i].strides[0]:
            order = "F"
            if (
                not outputs[i].data.contiguous
                and outputs[i].ndim >= 2
                and outputs[i].shape[-1] > outputs[i].shape[-2]
                and outputs[i].strides[-2] <= outputs[i].strides[-1]
            ):
                # Transposed
                order = "C"

            if outputs[i].ndim == 2:
                outputs[i] = [list(outputs[i][j]) for j in range(outputs[i].shape[0])]
            elif (
                "NHWC" in runner.output_native_shape_formats[i]
                and outputs[i].shape[1] == outputs[i].shape[2] == 1
            ):
                tmp = [list(outputs[i][j][0][0]) for j in range(outputs[i].shape[0])]
                outputs[i] = np.reshape(tmp, outputs[i].shape)
            elif (
                outputs[i].ndim == 3
                and "GENERIC" in runner.output_native_shape_formats[i]
                and outputs[i].shape[-1] == 1
            ):
                tmp = [list(outputs[i][0][j]) for j in range(outputs[i].shape[1])]
                outputs[i] = np.reshape(tmp, outputs[i].shape)

            outputs[i] = copy.deepcopy(np.array(outputs[i], order=order))
        else:
            outputs[i] = copy.deepcopy(outputs[i])

        # Convert from native format
        outputs[i] = reorder_tensor(
            outputs[i],
            runner.output_native_shape_formats[i],
            output_shape_formats[i],
            output_shapes[i],
        )

        # Slice channels if needed
        channel_idx = get_channel_idx(output_shape_formats[i])
        if channel_idx != -1:
            reshape_channel = None
            if output_reshapes is not None and output_reshapes[i] is not None:
                reshape_channel = output_reshapes[i][1]

            target_channel_size = (
                reshape_channel
                if reshape_channel is not None
                else runner.output_shapes[i][channel_idx]
            )

            if outputs[i].shape[channel_idx] > target_channel_size:
                slices = [slice(None)] * outputs[i].ndim
                slices[channel_idx] = slice(0, target_channel_size)
                outputs[i] = outputs[i][tuple(slices)]

        # Dequantize
        if outputs[i].itemsize == 1:
            outputs[i] = (outputs[i] / runner.output_coeffs[i]).astype(np.float32)
        elif outputs[i].itemsize == 2:
            outputs[i] = outputs[i].astype(np.uint32)
            outputs[i] = np.left_shift(outputs[i], 16)
            outputs[i] = outputs[i].view(np.float32)
        elif outputs[i].itemsize in [4, 8]:
            pass
        else:
            raise Exception(
                f"Unsupported output dtype {outputs[i].dtype} with itemsize {outputs[i].itemsize}"
            )

        if output_reshapes is not None and output_reshapes[i] is not None:
            reshape_target = output_reshapes[i][0]
            if output_shape_formats[i] == "NHWC" and outputs[i].ndim == 4:
                outputs[i] = outputs[i].transpose((0, 3, 1, 2))
            outputs[i] = outputs[i].reshape(reshape_target)

    return outputs


def stability_test(
    args, runners, batch_idx, inputs, ref_outputs, stab_test_errors, output_reshapes=None
):
    """Rerun the model n times to ensure it is stable.

    If there is any error, add it to stab_test_errors.
    """

    n = args.n_stability_test

    print(f"Running {n} times the same example for stability testing")

    err_msgs = []

    for i in range(n):
        for k, runner in enumerate(runners):
            ref_output = ref_outputs[k]
            output_i = runner(inputs[k])

            # Process outputs from zero copy / native if needed
            if args.out_zero_copy or args.out_native:
                output_i = process_output(
                    args,
                    runner,
                    output_i,
                    runner.output_shape_formats,
                    runner.output_shapes,
                    output_reshapes[k] if output_reshapes is not None else None,
                )

            # Check that there are the same number of outputs
            if len(output_i) != len(ref_output):
                err_msg = (
                    f"ERROR: Stability test {i + 1} failed for batch {batch_idx}! "
                    f"Found outputs with different lengths for the same input: "
                    f"{len(ref_output)}, {len(output_i)}"
                )
                print(err_msg)
                err_msgs.append(err_msg)
                break

            for j in range(len(ref_output)):
                # Check that each outputs' shapes are the same
                if output_i[j].shape != ref_output[j].shape:
                    err_msg = (
                        f"ERROR: Stability test {i + 1} failed for batch {batch_idx}! "
                        f"Found outputs with different shapes at index {j} "
                        f"for the same input: {ref_output[j].shape}, {len(output_i[j].shape)}"
                    )
                    print(err_msg)
                    err_msgs.append(err_msg)

                # Check that each outputs are equal
                else:
                    if not np.array_equal(output_i[j], ref_output[j]):
                        err_msg = (
                            f"ERROR: Stability test {i + 1} failed for batch {batch_idx}! "
                            f"Found different outputs for the same input at index {j}"
                        )
                        print(err_msg)
                        err_msgs.append(err_msg)

    if err_msgs:
        stab_test_errors.append("\n".join(err_msgs))
    else:
        print("Stability test passed!")


def gold_check(args, output_refs, outputs, error, named=False):
    """Check vart_ml_runner outputs correspond to dumped ones for a single batch.

    Must be called in real data mode.
    When *named* is True, outputs and refs are matched by index (same ordering from the runner).
    """

    for k in range(len(args.snapshot)):
        rtol, atol = get_snapshot_tolerance(args.snapshot[k])
        e = []
        if named:
            for i, output_ref in enumerate(output_refs[k]):
                output = outputs[k][i]
                if np.allclose(output, output_ref, rtol=rtol, atol=atol):
                    e.append(0)
                else:
                    e.append(np.max(np.square(output - output_ref)))
        else:
            # Iterate on all combinations of outputs and ref outputs
            # This needs to be done as outputs order is not guaranteed to be the same as the ref
            for output_ref in output_refs[k]:
                maybe_e = []
                for output in outputs[k]:
                    if output.shape == output_ref.shape:
                        if np.allclose(output, output_ref, rtol=rtol, atol=atol):
                            maybe_e.append(0)
                        else:
                            maybe_e.append(np.max(np.square(output - output_ref)))
                if maybe_e:
                    e.append(np.min(maybe_e))
                else:
                    print(f"No output shape matches the reference shape {output_ref.shape}")
                    print(f"Candidates : {[output.shape for output in outputs[k]]}")
                    e.append(float("nan"))
        error.append(e)


def run(args):
    input_formats = [get_snapshot_input_format(snap, args.input_format) for snap in args.snapshot]

    runners = [
        VART.Runner(snapshot_dir=snap, input_format=input_format, npu_only=args.npu_only)
        for snap, input_format in zip(args.snapshot, input_formats)
    ]

    inputs_types = [runner.input_types for runner in runners]
    input_shape_formats = [runner.input_shape_formats for runner in runners]

    if args.in_zero_copy:  # noqa SIM102
        if any(runner.set_input_zero_copy() for runner in runners):
            raise AttributeError(
                "Cannot modify native format of all input tensors. "
                "At least one tensor is linked to an ONNX node."
            )
        # Allocate native buffers capable of holding data
        ddr_bufs = [runner.alloc_ddr_bufs() for runner in runners]

    if args.in_native:  # noqa SIM102
        if any(runner.set_input_native() for runner in runners):
            raise AttributeError(
                "Cannot modify native format of all input tensors. "
                "At least one tensor is linked to an ONNX node."
            )

    if args.out_zero_copy:
        output_shapes = [runner.output_shapes for runner in runners]
        output_shape_formats = [runner.output_shape_formats for runner in runners]
        if any(runner.set_output_zero_copy() for runner in runners):
            raise AttributeError(
                "Cannot modify native format of all output tensors. "
                "At least one tensor is linked to an ONNX node."
            )

    if args.out_native:
        output_shapes = [runner.output_shapes for runner in runners]
        output_shape_formats = [runner.output_shape_formats for runner in runners]
        if any(runner.set_output_native() for runner in runners):
            raise AttributeError(
                "Cannot modify native format of all output tensors. "
                "At least one tensor is linked to an ONNX node."
            )

    # Get inputs
    if args.real_data:
        # Remove the error file if it exists
        if os.path.exists(error_file):
            os.remove(error_file)

        original_input_shape_formats = [
            get_original_input_shape_formats(snap)
            if input_format != "from_snapshot"
            else runner.input_shape_formats
            for snap, input_format, runner in zip(args.snapshot, input_formats, runners)
        ]
        has_golds = all(
            os.path.isdir(os.path.join(snap, "embedded_export", "golds")) for snap in args.snapshot
        )
        if has_golds:
            all_inputs, all_output_refs = load_golds(
                args, runners, inputs_types, input_formats, original_input_shape_formats
            )
        else:
            all_inputs, all_output_refs = load_ios(
                args, inputs_types, input_formats, original_input_shape_formats
            )
    else:
        all_inputs = gen_rand_inputs(runners)

    # If needed, create the dump path
    if args.dump_IOs:
        os.makedirs(args.dump_IOs, exist_ok=True)

    if args.in_zero_copy or args.in_native or args.out_zero_copy or args.out_native:
        input_reshapes, output_reshapes = zip(*[
            get_reshapes_from_snapshot(snap) for snap in args.snapshot
        ])
    else:
        input_reshapes = output_reshapes = None

    error = []
    stab_test_errors = []
    for idx in range(len(all_inputs)):
        # Process inputs into native data
        if args.in_native:
            all_inputs[idx] = process_input(
                args,
                runners,
                all_inputs[idx],
                input_shape_formats,
                input_reshapes=input_reshapes,
            )
        elif args.in_zero_copy:
            all_inputs[idx] = process_input(
                args,
                runners,
                all_inputs[idx],
                input_shape_formats,
                ddr_bufs,
                input_reshapes=input_reshapes,
            )

        # Run model
        before = time.time()
        outputs = [runner(input) for runner, input in zip(runners, all_inputs[idx])]
        after = time.time()
        print(f"Inference took {round((after - before) * 1000, 3)} ms")

        # Dump inputs/outputs to given path
        if args.dump_IOs:
            for runner_i, (in_data, out_data) in enumerate(zip(all_inputs[idx], outputs)):
                dump_outputs(args.dump_IOs, in_data, f"input_{runner_i}_{idx}")
                dump_outputs(args.dump_IOs, out_data, f"output_{runner_i}_{idx}")

        # Process outputs from zero copy / native if needed
        if args.out_zero_copy or args.out_native:
            outputs = [
                process_output(
                    args,
                    runners[i],
                    outputs[i],
                    output_shape_formats[i],
                    output_shapes[i],
                    output_reshapes=output_reshapes[i] if output_reshapes is not None else None,
                )
                for i in range(len(outputs))
            ]

        # Run stability tests
        if args.n_stability_test > 0:
            stability_test(
                args,
                runners,
                idx,
                all_inputs[idx],
                outputs,
                stab_test_errors,
                output_reshapes=output_reshapes,
            )

        # Gold check per batch
        if args.real_data:
            gold_check(args, all_output_refs[idx], outputs, error, named=has_golds)

        # Free processed inputs to avoid accumulating memory
        all_inputs[idx] = None

    f_msg = "\n --- ERRORS REPORT --- \n"

    # Check gold check errors
    if args.real_data:
        max_error = max(np.max(item) for item in error)
        err_idx = 0
        for idx in range(len(all_inputs)):
            for k in range(len(args.snapshot)):
                model_name = args.snapshot[k].split("/")[-2]
                print(f"Model: {model_name}: Max error batch n {idx} = {error[err_idx]}")
                err_idx += 1
    if args.real_data and max_error:
        f_msg += f"ERROR: found mismatch with gold: {error}"
        with open(error_file, "w") as f:
            f.write(str(max_error))

    # Check stability errors
    if stab_test_errors:
        f_msg += "\nERROR: Some stability test(s) failed. Review above error logs for more details"

    # If any error occurred, print error messages and exit
    if (args.real_data and max_error) or (stab_test_errors):
        print(f_msg)
        exit(1)

    print("OK: no error found")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Options for vart_ml_runner",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "--n_stability_test",
        type=int,
        default=0,
        help="Test stability through comparison of N additional iterations of the same run",
    )
    parser.add_argument(
        "--npu_only",
        action="store_true",
        default=False,
        help="skip ONNX Subgraphs",
    )
    parser.add_argument(
        "--in_native",
        action="store_true",
        default=False,
        help="enable native mode for inputs",
    )
    parser.add_argument(
        "--in_zero_copy",
        action="store_true",
        default=False,
        help="enable zero copy mode for inputs",
    )
    parser.add_argument(
        "--out_native",
        action="store_true",
        default=False,
        help="enable native mode for outputs",
    )
    parser.add_argument(
        "--out_zero_copy",
        action="store_true",
        default=False,
        help="enable zero copy mode for outputs",
    )
    parser.add_argument(
        "--real_data",
        action="store_true",
        default=False,
        help="Re-use saved inputs and compare to expected output",
    )
    parser.add_argument(
        "--dump_IOs",
        type=str,
        default="",
        help="Path to dump runners' inputs/outputs to (without pre/post processing)",
    )
    parser.add_argument(
        "--snapshot",
        type=str,
        required=True,
        help="Path to the snapshot folder",
        nargs="+",
    )
    parser.add_argument(
        "--input_format",
        type=str,
        required=False,
        default="from_snapshot",
        choices=["NHWC", "NCHW", "from_snapshot"],
    )
    args = parser.parse_args()
    run(args)
