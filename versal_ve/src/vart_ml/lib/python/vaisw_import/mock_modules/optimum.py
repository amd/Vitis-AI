#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from .wrappers import ModuleWrapper
from .wrappers import disable_wrap


class OptimumExporterOnnxConvertWrapper(ModuleWrapper):
    """Wrapper for optimum.export.convert"""

    def export(self, *args, **kwargs):
        with disable_wrap():
            return self.orig_module.export(*args, **kwargs)

    def export_models(self, *args, **kwargs):
        with disable_wrap():
            return self.orig_module.export_models(*args, **kwargs)

    def export_pytorch(self, *args, **kwargs):
        with disable_wrap():
            return self.orig_module.export_pytorch(*args, **kwargs)

    def export_tensorflow(self, *args, **kwargs):
        with disable_wrap():
            return self.orig_module.export_tensorflow(*args, **kwargs)
