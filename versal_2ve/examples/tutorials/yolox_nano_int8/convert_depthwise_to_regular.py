#!/usr/bin/env python3
"""
Convert selected depthwise convolutions to mathematically-equivalent regular (dense) convolutions.

This script transforms depthwise Conv nodes in a QDQ ONNX model to regular convolutions
by expanding the weight tensor from shape (C, 1, H, W) to (C, C, H, W) with a diagonal
structure. The conversion is bit-exact and produces identical outputs.

The following depthwise Conv nodes are converted:
  - Conv_1223 (group=32)
  - Conv_1284 (group=64)
  - Conv_1749 (group=64)
  - Conv_2161 (group=64)
  - Conv_2201 (group=64)

Usage:
    python convert_depthwise_to_regular.py \
        --input pt_yolox-nano_3.5/quantized/yolox_nano_onnx_pt.onnx \
        --output onnx_model/yolox_nano_onnx_pt_regular_conv_converted.onnx
"""

import argparse
import copy
from pathlib import Path

import numpy as np
import onnx
from onnx import numpy_helper, TensorProto, helper


NODES_TO_CONVERT = ['Conv_1223', 'Conv_1284', 'Conv_1749', 'Conv_2161', 'Conv_2201']


def build_graph_mappings(model):
    """Build helper mappings for navigating the ONNX graph."""
    output_to_node = {}
    name_to_node = {}
    for node in model.graph.node:
        name_to_node[node.name] = node
        for output in node.output:
            output_to_node[output] = node

    initializers = {init.name: init for init in model.graph.initializer}
    return output_to_node, name_to_node, initializers


def trace_to_initializer(tensor_name, output_to_node, initializers):
    """
    Trace back through QuantizeLinear/DequantizeLinear nodes to find the source initializer.

    Returns:
        (initializer_name, numpy_array) or (None, None) if not found
    """
    if tensor_name in initializers:
        return tensor_name, numpy_helper.to_array(initializers[tensor_name])

    if tensor_name in output_to_node:
        node = output_to_node[tensor_name]
        if node.op_type == "Constant":
            for attr in node.attribute:
                if attr.name == "value":
                    return None, numpy_helper.to_array(attr.t)
        elif node.op_type in ["QuantizeLinear", "DequantizeLinear"]:
            return trace_to_initializer(node.input[0], output_to_node, initializers)

    return None, None


def expand_depthwise_to_regular_weight(weight):
    """
    Expand depthwise convolution weight to regular convolution weight.

    Depthwise: weight shape (C, 1, H, W) with group=C
    Regular:   weight shape (C, C, H, W) with group=1

    The regular weight has a diagonal structure where weight[i, i, :, :] = depthwise_weight[i, 0, :, :]
    and all other channels are zero.
    """
    out_channels, in_ch_per_group, kh, kw = weight.shape
    assert in_ch_per_group == 1, f"Expected in_ch_per_group=1 for depthwise conv, got {in_ch_per_group}"

    new_weight = np.zeros((out_channels, out_channels, kh, kw), dtype=weight.dtype)
    for i in range(out_channels):
        new_weight[i, i, :, :] = weight[i, 0, :, :]

    return new_weight


def convert_depthwise_to_regular(model, conv_names=None):
    """
    Convert specified depthwise Conv nodes to regular Conv nodes.

    Args:
        model: ONNX model
        conv_names: List of Conv node names to convert (default: NODES_TO_CONVERT)

    Returns:
        Modified ONNX model
    """
    if conv_names is None:
        conv_names = NODES_TO_CONVERT

    model = copy.deepcopy(model)
    output_to_node, name_to_node, initializers = build_graph_mappings(model)

    converted_count = 0

    for conv_name in conv_names:
        if conv_name not in name_to_node:
            print(f"Warning: Conv node '{conv_name}' not found in model, skipping")
            continue

        conv_node = name_to_node[conv_name]

        if conv_node.op_type != "Conv":
            print(f"Warning: Node '{conv_name}' is not a Conv node, skipping")
            continue

        group = 1
        for attr in conv_node.attribute:
            if attr.name == "group":
                group = attr.i
                break

        weight_input = conv_node.input[1]
        init_name, weight = trace_to_initializer(weight_input, output_to_node, initializers)

        if weight is None:
            print(f"Warning: Could not find weight for '{conv_name}', skipping")
            continue

        out_channels, in_ch_per_group, kh, kw = weight.shape

        if not (group == out_channels and in_ch_per_group == 1):
            print(f"Warning: '{conv_name}' is not a depthwise conv "
                  f"(group={group}, out_ch={out_channels}, in_ch_per_group={in_ch_per_group}), skipping")
            continue

        new_weight = expand_depthwise_to_regular_weight(weight)

        if init_name is not None:
            for init in model.graph.initializer:
                if init.name == init_name:
                    init.CopyFrom(numpy_helper.from_array(new_weight, init_name))
                    break

        for attr in conv_node.attribute:
            if attr.name == "group":
                attr.i = 1
                break

        print(f"Converted {conv_name}: group {group} -> 1, "
              f"weight {weight.shape} -> {new_weight.shape}")
        converted_count += 1

    print(f"\nTotal converted: {converted_count}/{len(conv_names)} depthwise Conv nodes")

    return model


def verify_conversion(original_model_path, converted_model_path, conv_names=None):
    """Verify that the conversion was successful by comparing model structures."""
    if conv_names is None:
        conv_names = NODES_TO_CONVERT

    orig_model = onnx.load(original_model_path)
    conv_model = onnx.load(converted_model_path)

    orig_output_to_node, orig_name_to_node, _ = build_graph_mappings(orig_model)
    conv_output_to_node, conv_name_to_node, _ = build_graph_mappings(conv_model)

    print("\nVerification:")
    print("=" * 60)

    all_ok = True
    for conv_name in conv_names:
        orig_node = orig_name_to_node.get(conv_name)
        conv_node = conv_name_to_node.get(conv_name)

        if orig_node is None or conv_node is None:
            print(f"  {conv_name}: MISSING")
            all_ok = False
            continue

        orig_group = 1
        conv_group = 1
        for attr in orig_node.attribute:
            if attr.name == "group":
                orig_group = attr.i
        for attr in conv_node.attribute:
            if attr.name == "group":
                conv_group = attr.i

        if conv_group == 1 and orig_group > 1:
            print(f"  {conv_name}: OK (group {orig_group} -> {conv_group})")
        else:
            print(f"  {conv_name}: FAILED (group {orig_group} -> {conv_group})")
            all_ok = False

    return all_ok


def main():
    parser = argparse.ArgumentParser(
        description="Convert depthwise convolutions to regular convolutions in ONNX model",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        "--input", "-i",
        required=True,
        help="Path to input ONNX model (e.g., pt_yolox-nano_3.5/quantized/yolox_nano_onnx_pt.onnx)"
    )
    parser.add_argument(
        "--output", "-o",
        required=True,
        help="Path for output ONNX model (e.g., onnx_model/yolox_nano_onnx_pt_regular_conv_converted.onnx)"
    )
    parser.add_argument(
        "--nodes",
        nargs="+",
        default=NODES_TO_CONVERT,
        help=f"Conv node names to convert (default: {NODES_TO_CONVERT})"
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Verify conversion by comparing original and converted models"
    )

    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.exists():
        raise FileNotFoundError(f"Input model not found: {input_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"Loading model from: {input_path}")
    model = onnx.load(str(input_path))

    print(f"\nConverting {len(args.nodes)} depthwise Conv nodes to regular Conv...")
    converted_model = convert_depthwise_to_regular(model, args.nodes)

    print(f"\nSaving converted model to: {output_path}")
    onnx.save(converted_model, str(output_path))

    if args.verify:
        verify_conversion(str(input_path), str(output_path), args.nodes)

    print("\nDone!")


if __name__ == "__main__":
    main()
