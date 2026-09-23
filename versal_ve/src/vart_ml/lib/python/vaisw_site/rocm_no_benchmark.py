# ===========================================================
# Copyright(C) 2026 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import contextlib
import importlib.abc
import importlib.machinery
import sys


def _disable_rocm_benchmarking(torch_module):
    with contextlib.suppress(Exception):
        torch_module.backends.cudnn.benchmark = False

    with contextlib.suppress(Exception):
        torch_module.backends.miopen.set_flags(True)


class _TorchLoader(importlib.abc.Loader):
    def __init__(self, wrapped):
        self._wrapped = wrapped

    def create_module(self, spec):
        create_module = getattr(self._wrapped, "create_module", None)
        if create_module is None:
            return None
        return create_module(spec)

    def exec_module(self, module):
        self._wrapped.exec_module(module)
        _disable_rocm_benchmarking(module)


class _TorchFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, _target=None):
        if fullname != "torch":
            return None
        spec = importlib.machinery.PathFinder.find_spec(fullname, path)
        if spec is not None and spec.loader is not None:
            spec.loader = _TorchLoader(spec.loader)
        return spec


if "torch" in sys.modules:
    _disable_rocm_benchmarking(sys.modules["torch"])
elif not any(isinstance(finder, _TorchFinder) for finder in sys.meta_path):
    sys.meta_path.insert(0, _TorchFinder())
