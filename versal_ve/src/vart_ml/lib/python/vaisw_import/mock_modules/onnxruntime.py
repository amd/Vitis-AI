#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import io

from ..VaiswStub import vaisw
from .wrappers import Framework
from .wrappers import ModuleWrapper
from .wrappers import NetworkWrapper


class OrtSessionWrapper(NetworkWrapper):
    def run(self, output_names, input_feed, run_options=None):
        vaisw.logWarning(
            "We do not support calling Session directly. Please use "
            "InferenceSession to enable vaisw wrapping.\n"
        )
        return super.run(output_names, input_feed, run_options)


class OrtInferenceSessionWrapper(NetworkWrapper):
    def __init__(self, path_or_bytes, *args, **kwargs):
        import onnx

        if isinstance(path_or_bytes, str):
            self.onnxModel = onnx.load(path_or_bytes)
        elif isinstance(path_or_bytes, bytes):
            self.onnxModel = onnx.load(io.BytesIO(path_or_bytes))
        else:
            raise TypeError(f"Cannot load from type {type(path_or_bytes)}")
        super().__init__(path_or_bytes, *args, **kwargs)

    def run(self, output_names, input_feed, run_options=None):  # noqa: ARG002
        # This is inference session, consider run as going for inference.
        return self.vaisw_run(Framework.OnnxRuntime, input_feed, onnxModel=self.onnxModel)


class OrtInferenceCollection(ModuleWrapper):
    """Wrapper for onnxruntime.capi.onnxruntime_inference_collection module"""

    Session = OrtSessionWrapper
    InferenceSession = OrtInferenceSessionWrapper
