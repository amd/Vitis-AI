#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import os
import sys
from contextlib import contextmanager

from .mock_modules.caffe import PyCaffeWrapper
from .mock_modules.keras import BaseLayerWrapper
from .mock_modules.keras import KSaveWrapper
from .mock_modules.keras import TrainingWrapper
from .mock_modules.onnxruntime import OrtInferenceCollection
from .mock_modules.optimum import OptimumExporterOnnxConvertWrapper
from .mock_modules.pytorch import DataSetModuleWrapper
from .mock_modules.pytorch import ThopProfileWrapper
from .mock_modules.pytorch import TorchOnnxWrapper
from .mock_modules.pytorch import nnModulesModuleWrapper
from .mock_modules.tensorflow import TFEagerDefFunctionWrapper
from .mock_modules.tensorflow import TFEagerFunctionWrapper
from .mock_modules.tensorflow import TFSavedModelLoad
from .mock_modules.tensorflow import TFSaverWrapper
from .mock_modules.tensorflow import TFWrapper
from .mock_modules.tensorflow import VaiswClientSessionWrapper
from .mock_modules.torchvision import TorchvisionTransformsWrapper
from .mock_modules.wrappers import MarkedWrapper
from .mock_modules.wrappers import VaiswAvailableModuleSpec
from .mock_modules.wrappers import VaiswWrapperSpec
from .mock_modules.wrappers import WrappedModules
from .Mocking import MockingModule
from .VaiswStub import vaisw

CATCH_MODULES = {
    # ## tensorflow
    "tensorflow.python.client.session": VaiswClientSessionWrapper,
    "tensorflow.python.training.saver": TFSaverWrapper,
    # In tensorflow 2 some modules are more or less shared with keras
    # so we need to mark them
    "tensorflow.python.keras.engine.training": TrainingWrapper,
    "tensorflow.python.keras.engine.base_layer": BaseLayerWrapper,
    "tensorflow.python.eager.def_function": TFEagerDefFunctionWrapper,
    "tensorflow.python.eager.function": TFEagerFunctionWrapper,
    "tensorflow.python.saved_model.load": TFSavedModelLoad,
    "tensorflow": TFWrapper,
    # ## pytorch
    "torch.nn.modules.module": nnModulesModuleWrapper,
    "torch.utils.data.dataset": DataSetModuleWrapper,
    "torchvision.transforms.transforms": TorchvisionTransformsWrapper,
    "torch.onnx": TorchOnnxWrapper,  # onnx export
    "thop.profile": ThopProfileWrapper,  # profiler for pytorch
    # ## keras
    "keras.engine.training": TrainingWrapper,
    "keras.engine.base_layer": BaseLayerWrapper,
    "keras.saving.save": KSaveWrapper,
    # ## caffe (obsolete)
    "caffe.pycaffe": PyCaffeWrapper,
    # ## onnxruntime
    "onnxruntime.capi.onnxruntime_inference_collection": OrtInferenceCollection,
    # ## optimum (huggingface); onnx export uses more onnxruntime tricks
    "optimum.exporters.onnx.convert": OptimumExporterOnnxConvertWrapper,
}


class CatchCheckError(Exception):
    def __init__(self, module):
        self.module = module

    def __str__(self):
        return f"Module '{self.module}' is already imported. Importer must be create before."


class VaiswImportModule(MarkedWrapper):
    MOCK_MODULES_ENV_NAME = "VAISW_MOCK_MODULES"

    def __init__(self, wrappers=CATCH_MODULES, mock_modules=None):
        """
        The wrappers must be a dict of module name with the wrapper that will
        be called to import the module.

        Parameter `mock_modules` can be set to list modules that the system should
        avoid importing. Importing the modules listed will succeed but any interaction
        with them will raise an exception.
        Submodules of mocked modules are mocked as well.
        This is mostly useful for debug and testing purpose.
        By default the mock_modules will be filled with the environement variable
        'VAISW_MOCK_MODULES'.
        """
        self._activate = True
        self._mod_wrappers = {}
        self.add_module_wrappers(wrappers)
        self.__setup_mocking_module(mock_modules)

    def add_module_wrappers(self, wrappers):
        """Add module to wrap.

        Can be called after creating the VaiswImportModule.
        """
        if wrappers is None:
            return
        for m, c in wrappers.items():
            if m in sys.modules:
                raise CatchCheckError(m)
            self._mod_wrappers[m] = c

    def __setup_mocking_module(self, mock_modules):
        """Special case to disable modules through the module importer"""
        if mock_modules is None:
            mock_modules = os.environ.get(VaiswImportModule.MOCK_MODULES_ENV_NAME, "").split(":")

        # Put mocking modules in list of modules
        for mock_mod in mock_modules:
            if not mock_mod:
                continue
            if mock_mod in sys.modules:
                raise CatchCheckError(mock_mod)
            sys.modules[mock_mod] = MockingModule(mock_mod)

        # If submodules are define as mocking, set them specifically.
        # Otherwise the "import mod.sub" and the "import mod; mod.sub" would not
        # return the same object.
        for mock_mod in mock_modules:
            if "." in mock_mod:
                key, name = mock_mod.rsplit(".", 1)
                if key in sys.modules:
                    setattr(sys.modules[key], name, sys.modules[mock_mod])

    @contextmanager
    def w_deactivate(self):
        try:
            orig_val = self._activate
            self._activate = False
            yield
        finally:
            self._activate = orig_val

    def find_spec(self, fullname, path, target=None):  # noqa: ARG002
        if not self._activate:
            return None

        if fullname in WrappedModules:
            vaisw.logInfo(f"Already wrapped module {fullname!r}\n")
            return VaiswAvailableModuleSpec(WrappedModules[fullname])
        elif fullname in self._mod_wrappers:
            vaisw.logInfo(f"Wrap import of module {fullname!r}\n")
            return VaiswWrapperSpec(self._mod_wrappers[fullname], fullname, path)
        elif "." in fullname:
            key, _ = fullname.rsplit(".", 1)
            if key in sys.modules and isinstance(sys.modules[key], MockingModule):
                return VaiswAvailableModuleSpec(MockingModule(fullname))
        return None
