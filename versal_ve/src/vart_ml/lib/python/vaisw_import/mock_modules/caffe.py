#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from .wrappers import ModuleWrapper
from .wrappers import NetworkWrapper


class VaiswNetWrapper(NetworkWrapper):
    """Wrapper of caffe.Net object"""

    def forward(self, *args, **kwargs):
        # print("{} passthrough forward."
        #       .format(ColorString("[WARNING]:", TermColors.BYellow)))
        return super().forward(*args, **kwargs)


class PyCaffeWrapper(ModuleWrapper):
    """Wrapper for caffe.pycaffe module

    The real Net class is in caffe._caffe, pycaffe is only the python interface
    to it but it is almost certain that all use of Net will pass through pycaffe.
    """

    Net = VaiswNetWrapper
