#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from .wrappers import Framework
from .wrappers import ModuleWrapper
from .wrappers import NetworkWrapper
from .wrappers import disable_wrap


class TorchModuleWrapper(NetworkWrapper):
    def __call__(self, *inputs, **kwargs):
        return self.vaisw_run(Framework.Pytorch, inputs, call_kwargs=kwargs)


class nnModulesModuleWrapper(ModuleWrapper):
    """Wrapper for torch.nn.modules.module module"""

    Module = TorchModuleWrapper


class DatasetWrapper(NetworkWrapper):
    @classmethod
    def __init_subclass__(cls, **kwargs):
        # Replace the __getitem__ function in the subclass to protect
        # it from the wrapper if it transform the data through a network.
        super().__init_subclass__(**kwargs)
        if not getattr(cls.__getitem__, "_is_wrapped", False):
            orig_func = cls.__getitem__

            def wrap_getitem(self, key):
                with TorchModuleWrapper.recall_protect():
                    return orig_func(self, key)

            wrap_getitem._is_wrapped = True  # noqa: SLF001
            cls.__getitem__ = wrap_getitem

    @classmethod
    def __class_getitem__(cls, key):
        if len(cls.__bases__) == 2 and cls.__bases__[0] == DatasetWrapper:
            return cls.__bases__[1].__class_getitem__(key)
        else:
            return super().__class_getitem__(key)


class DataSetModuleWrapper(ModuleWrapper):
    """Wrapper for torch.utils.data.dataset module"""

    Dataset = DatasetWrapper


class ThopProfileWrapper(ModuleWrapper):
    """Wrapper for thop.profile module"""

    def profile(self, *args, **kwargs):
        with disable_wrap():
            return self.orig_module.profile(*args, **kwargs)


class TorchOnnxWrapper(ModuleWrapper):
    """Wrapper for torch.onnx module"""

    def export(self, *args, **kwargs):
        with disable_wrap():
            return self.orig_module.export(*args, **kwargs)
