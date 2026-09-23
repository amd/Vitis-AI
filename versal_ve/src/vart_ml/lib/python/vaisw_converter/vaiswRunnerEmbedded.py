# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import os
from collections.abc import Sequence
from typing import List
from typing import Tuple
from typing import Union

import numpy as np
from vaisw_converter import vaiswRunnerCommon
from vart_ml import UserConfig


class VaiswEmbedded(vaiswRunnerCommon.VaiswCommon):
    def _initialize(self, _model, _inputs):
        if self._init:
            raise RuntimeError("initialize should be called only once.")
        self._init = True

        if self.networkName is None:
            self.networkName = vaiswRunnerCommon.DEFAULT_NETWORK_NAME

        import VART

        snapshotDir = UserConfig.get("snapshot.directory")
        configDir = UserConfig.get("runSession.directory")
        if not snapshotDir and configDir:
            snapshotDir = os.path.join(configDir, "snapshot")
        assert snapshotDir, (
            "ERROR: snapshot directory is not set in embedded mode. "
            "A snapshot directory is required"
        )
        self._Proc = VART.Runner(snapshotDir, output_names=self._outputs)
        return

    def run(self, model, inputs, _opset_version=None):

        if isinstance(inputs, np.ndarray) and inputs.dtype != object:
            inputs = [self.convert_float32_or_uint8(inputs)]
        else:
            inputs = [self.convert_float32_or_uint8(i) for i in inputs]

        if not self._init:
            self._initialize(model, inputs)

        return self._Proc.run(inputs)


# TODO These classes are essentially just copies of classes in vaiswRunner.py
# (__init__ and run method only)
# When cythonization is removed entirely, revisit this code to avoid duplication


class ZOnnx(VaiswEmbedded):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._framework_name = "ONNX"

    def run(self, model, inputs, onnxModel=None, **kwargs):
        onnx_model = model if onnxModel is None else onnxModel
        if isinstance(inputs, dict):
            input_list = [
                inputs[inode.name] for inode in onnx_model.graph.input if inode.name in inputs
            ]
        else:
            input_list = inputs
        return super().run(onnx_model, input_list, **kwargs)


class ZTorch(VaiswEmbedded):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._framework_name = "PyTorch"
        self._outputs_format = None

    def _compute_outputs_format(
        self, torch_outputs, index_output: int
    ) -> Tuple[Union[list, int], int]:
        """
        Recursive function which computes an outputs_fomat from a torch output
        It replaces every torch.Tensor by a unique index_output

        Example of torch_outputs:  [out0, [out1, out2 ,out3]]
        Example of outputs_format: [0, [1, 2, 3]]
        """
        import torch

        if isinstance(torch_outputs, torch.Tensor):
            return index_output, index_output + 1
        elif isinstance(torch_outputs, list):
            for i in range(len(torch_outputs)):
                torch_outputs[i], index_output = self._compute_outputs_format(
                    torch_outputs[i], index_output
                )
            return torch_outputs, index_output
        elif isinstance(torch_outputs, tuple):
            torch_outputs = list(torch_outputs)
            for i in range(len(torch_outputs)):
                torch_outputs[i], index_output = self._compute_outputs_format(
                    torch_outputs[i], index_output
                )
            return torch_outputs, index_output
        elif isinstance(torch_outputs, dict):
            torch_outputs = list(torch_outputs.values())
            for i in range(len(torch_outputs)):
                torch_outputs[i], index_output = self._compute_outputs_format(
                    torch_outputs[i], index_output
                )
            return torch_outputs, index_output
        else:
            raise RuntimeError(
                f"type {type(torch_outputs)} not implemented yet for torch model"
                f" output in compute_outputs_format"
            )

    def _apply_preproc_if_set(self, t, preproc_transform):
        if preproc_transform.is_set:
            return preproc_transform.call_original_func(t)
        return t

    def _set_outputs_format(self, pytorchModel, torch_inputs, call_kwargs):
        """
        Set the outputs_format with the output of the pytorch model
        """
        import torch
        from vaisw_preprocess_decorator import preproc_transform

        pytorchModel.eval()
        if isinstance(torch_inputs, torch.Tensor):
            torch_inputs = self._apply_preproc_if_set(torch_inputs, preproc_transform)
            out = pytorchModel(*(torch_inputs,), **call_kwargs)
        else:
            # Only first element because anyway preproc_transform decorator only
            # supports a single traced input
            torch_inputs = tuple(
                [self._apply_preproc_if_set(torch_inputs[0], preproc_transform)]
                + list(torch_inputs[1:])
            )
            out = pytorchModel(*tuple(torch_inputs), **call_kwargs)
        self._outputs_format, _ = self._compute_outputs_format(out, 0)
        # case with one output, we expect self._output_format to be a list
        if isinstance(self._outputs_format, int):
            self._outputs_format = [self._outputs_format]

    def _fill_outputs_format_with_iriz_outputs(
        self, outputs_format: List[Union[list, int]], iriz_outputs: list
    ):
        """
        Recursive function which creates a new_outputs from iriz_outputs and outputs_fomat
        It replaces every index from outputs_format by the output from iriz_outputs

        Example of outputs_format: [0,[1,2,3]]
        Example of iriz_outputs:   [out0, out1, out2, out3]
        Example of new_outputs:    [out0, [out1, out2, out3]]
        """
        if outputs_format is None:
            raise RuntimeError(f"Outputs format is None, we can't compute the new outputs")
        for i in range(len(outputs_format)):
            if isinstance(outputs_format[i], int):
                if len(iriz_outputs) < outputs_format[i] + 1:
                    # In some network, we expect more output that we have with iriz
                    # but they are ignored, so we make thoses fake output to have error
                    # when we try to acces them instead of having accuracy issues
                    # seen in testCase WIQJ
                    outputs_format[i] = "Fake_output"
                else:
                    outputs_format[i] = iriz_outputs[outputs_format[i]]
            elif isinstance(outputs_format[i], list):
                self._fill_outputs_format_with_iriz_outputs(outputs_format[i], iriz_outputs)
            else:
                raise RuntimeError(
                    f"type {type(outputs_format[i])} not implemented yet for outputs_format"
                )

    def run(self, model, inputs, torchInput=True, torchOutput=True, call_kwargs=None, **kwargs):
        import copy

        import torch

        if call_kwargs is None:
            call_kwargs = {}
        if self._outputs_format is None:
            self._set_outputs_format(model, inputs, call_kwargs)

        if torchInput:

            def to_numpy(inp):
                if isinstance(inp, torch.Tensor):
                    return inp.cpu().numpy()
                elif isinstance(inp, Sequence):
                    return np.array(inp)
                else:
                    return inp

            if isinstance(inputs, (list, tuple)):
                inputs = [to_numpy(i) for i in inputs]
            else:
                inputs = [to_numpy(inputs)]

            for name, value in call_kwargs.copy().items():
                if isinstance(value, np.ndarray):
                    inputs.append(value)
                    del call_kwargs[name]
                elif isinstance(value, torch.Tensor):
                    inputs.append(to_numpy(value))
                    del call_kwargs[name]
                else:
                    continue

        self.call_kwargs = call_kwargs
        outputs = super().run(model, inputs, **kwargs)

        if torchOutput:
            outputs = [torch.from_numpy(o) for o in outputs]

        new_outputs = copy.deepcopy(self._outputs_format)
        self._fill_outputs_format_with_iriz_outputs(new_outputs, outputs)

        if len(new_outputs) == 1:
            new_outputs = new_outputs[0]

        return new_outputs


class ZTensorflow(VaiswEmbedded):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._session = None
        self._framework_name = "Tensorflow"

    def run(self, model, inputs, fetches=None, **kwargs):
        if fetches is not None:
            if not self._outputs:
                self._outputs = fetches
            if self._outputs != fetches:
                raise RuntimeError(
                    f"The fetches have changed. Got new output names '{fetches}' "
                    f"but expected '{self._outputs}'\n"
                    "Please use only one graph per session or use runSession.rejectTfRunSession."
                )
        if isinstance(inputs, dict):
            if len(self._inputs) == 0:
                self._inputs = [k if isinstance(k, str) else k.name for k in inputs.keys()]
            inputs = list(inputs.values())
        return super().run(model, inputs, **kwargs)


class ZKeras(ZTensorflow):
    def __init__(self, model_type="keras", io_convert_Tensor_numpy=False, **kwargs):
        super().__init__(**kwargs)
        self.model_type = model_type
        assert self.model_type in ["keras", "function", "concrete_function"], (
            f"Wrong model type {model_type}"
        )
        self.io_convert_Tensor_numpy = io_convert_Tensor_numpy

    def run(self, model, inputs, **kwargs):
        import tensorflow as tf

        if tf.is_tensor(inputs):
            inputs = inputs.numpy()
        elif isinstance(inputs, list):
            for input in inputs:
                if tf.is_tensor(input):
                    input = input.numpy()
        out = super().run(model, inputs, **kwargs)

        if self.io_convert_Tensor_numpy:
            if self.model_type == "concrete_function" and isinstance(
                model.structured_outputs, dict
            ):
                out = {
                    out_name: tf.convert_to_tensor(out_data)
                    for out_name, out_data in zip(model.structured_outputs.keys(), out)
                }
            elif isinstance(out, (list, tuple)):
                if len(out) == 1:
                    out = tf.convert_to_tensor(out[0])
                else:
                    out = [tf.convert_to_tensor(v) for v in out]
            else:
                out = tf.convert_to_tensor(out)
        return out
