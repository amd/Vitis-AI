#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import importlib
import platform

ON_EMBD = platform.processor() in ["aarch64", ""]


def __print__(msg, **_kwargs):
    print(msg, end="")


# If the import vaisw failed because of improper vaisw libs during the sitecustomize
# phase of python then the error is only logged and ignored and the run continue
# without the EoU setup.
# Thus we import vaisw as lately as possible so the import happens during the run
# making any potential import exception to be raised to the caller.
class __VaiswStub:
    def __init__(self, name="vaisw"):
        self.vaisw = None
        self.name = name

    def __getattr__(self, name):
        if self.vaisw is None:
            self.vaisw = importlib.import_module(self.name)
        if ON_EMBD and name in ["logError", "logWarning", "logInfo"]:
            attr = __print__
        else:
            attr = getattr(self.vaisw, name)
        setattr(self, name, attr)  # cache attribute in the stub.
        return attr


if ON_EMBD:
    vaisw = __VaiswStub("vart_ml")
else:
    vaisw = __VaiswStub()
