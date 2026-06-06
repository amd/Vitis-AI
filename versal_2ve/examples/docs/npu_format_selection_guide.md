# NPU Format Selection Guide



## Overview

When an application pipelines pre-processing with NPU inference, the critical decision is selecting the NPU input tensor format. The NPU strictly expects its input tensor to follow a specific layout and data type; therefore, the pre-processor must be configured so its output buffer exactly matches this required tensor format. Any mismatch in layout or precision leads to incorrect inference results or execution failure.

This guide focuses on how to determine and align with the NPU's expected tensor format by choosing the appropriate `colour-format` in `preprocess-config` of the [`x_plus_ml_vart`](../cpp_examples/x_plus_ml_vart/README.md) application. The goal is to ensure the pre-processed output is already in the NPU's native tensor format, enabling direct consumption by the NPU with zero copy and no additional data conversion.

The procedure is three steps:

1. Inspect the model's input-tensor metadata (`memory_layout`, `dtype`).
2. Map that metadata to a `colour-format` value using the table in [preprocessing_config.md](preprocessing_config.md#selecting-colour-format).
3. Set the value in the application's JSON config and rely on the built-in validation to catch mistakes.

---

## Step 1 — Dump the model's input tensor metadata

Use the `[ml_vart](../cpp_examples/ml_vart/README.md)` application with the `[--get-model-info](../cpp_examples/ml_vart/README.md#inspecting-model-metadata)` flag to dump the compiled model's tensor layout and data type:

```bash
ml_vart --get-model-info <model-path>
```

This produces a `<model_basename>_info.json` file. The schema of this output is documented in the [ml_vart `--get-model-info` output formats appendix](../cpp_examples/ml_vart/README.md#appendix---get-model-info-output-formats).

For every input tensor, the dump reports `memory_layout` and `dtype` under both a `cpu` block and an `hw` block. `x_plus_ml_vart` can use either CPU or HW tensors, selected per model via `inference-config.runner-options.input-tensor-type` (`"CPU"` or `"HW"`); read the block that matches the configured tensor type.

> **Note:** To achieve zero copy between the pre-processor and the NPU, select the **HW** tensor type. The HW tensor format is the NPU's native format; using it avoids any intermediate layout conversion between pre-processing and inference.

Note the `memory_layout` (for example `NHWC`, `NCHW`, `HCWNC4`) and `dtype` (for example `INT8`, `FP16`, `BF16`, `FLOAT32`) of the first input tensor — these two values drive the format choice in the next step.

---

## Step 2 — Map the tensor metadata to a `colour-format`

The `image_processing` PL kernel describes its output in **video-format terms** (`RGB`, `RGBP`, `RGBX`, `Y_UV8_420`, ...). The NPU describes its inputs in **tensor-layout terms** (`NHWC`, `NCHW`, `HCWNC4`, ...). These two naming systems do not map one-to-one, so `colour-format` is the bridge: it encodes both the colour space and the destination tensor layout / data type.

Use the `(memory_layout, dtype)` pair from Step 1 to pick a value from the lookup tables in [preprocessing_config.md](preprocessing_config.md#selecting-colour-format). That section contains:

- A **Quick mapping** table for the common INT8 / FP32 layouts.
- An **Accepted values** table that lists every `colour-format` recognised by each preprocess-driving application along with the layout / dtype it represents.

For a deeper understanding of the NPU-supported tensor formats and how they are organized in memory, refer to the [Vitis AI tensor formats user guide](https://vitisai.docs.amd.com/).

This guide describes the NPU input tensor format through the video colour formats produced by the Image Processing IP, because those formats map directly to the NPU's expected input layout. If the Image Processing IP is not part of your pipeline, refer to the tensor-format documentation linked above and construct the input buffers to match the NPU tensor's memory layout and data type.

---

## Step 3 — Set the value and let the application validate

Set the chosen value as `preprocess-config.colour-format` in the model's JSON config:

```json
"preprocess-config": {
  "mean-r": 123.675,
  "mean-g": 116.28,
  "mean-b": 103.53,
  "scale-r": 0.017124,
  "scale-g": 0.017507,
  "scale-b": 0.017429,
  "colour-format": "RGBX",
  "maintain-aspect-ratio": true,
  "resizing-type": "PANSCAN",
  "in-mem-bank": 2,
  "out-mem-bank": 2
}
```

When `x_plus_ml_vart` starts, it cross-checks the user-specified `colour-format` against the inference tensor's `memory_layout` and `dtype` (obtained from the runner). If the two do not agree, the application aborts initialization with a message that names both the value the user provided and the value the tensor metadata expects, for example:

```
ERROR: colour-format mismatch for instance 0: user specified "RGB" but tensor metadata (layout=HCWNC4, dtype=INT8) expects a different VideoFormat (...)
```

This catches misconfigurations early instead of letting them silently produce incorrect pre-processed buffers.

> **Tip:** In `x_plus_ml_vart`, `colour-format` is **optional**. If you omit it, the application derives the `VideoFormat` automatically from the inference tensor's layout and data type assuming an `RGB` colour space. Specify the field explicitly only when you need a non-default colour space (for example `BGR`) or when you want the start-up validation to flag a mismatch.

---

## Related documentation

- **[preprocessing_config.md](preprocessing_config.md)** — Full `preprocess-config` schema, including the `colour-format` mapping tables referenced in Step 2.
- **[ml_vart README](../cpp_examples/ml_vart/README.md)** — `ml_vart` application, including `--get-model-info` usage and output-format appendix.
- **[Vitis AI tensor formats user guide](https://vitisai.docs.amd.com/)** — In-depth reference on memory layouts, data types, and tensor-format conversions.

