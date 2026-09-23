# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

"""This file (and folder) allows to get classifier class without importing all
frameworks at the same time. Thus it is setting a single interface, no matter that
all possible frameworks are available on the machine.

See the IClassifier class to get the constructor parameters of the classifiers.

Example usage to get tensorflow classifier :
from .Classifier import Classifiers
classifier = Classifiers.get("tensorflow", <init params...>)
"""

import importlib


class Classifiers:
    class LazyImport:
        def __init__(self, module, cls):
            self.module = module
            self.cls = cls

        def __call__(self, *args, **kwargs):
            mod = importlib.import_module(self.module, __package__)
            return getattr(mod, self.cls)(*args, **kwargs)

    spec_cls = {
        "tensorflow": LazyImport(".tensorflow_classifier", "TFClassifier"),
        "tensorflow2": LazyImport(".tensorflow2_classifier", "TF2Classifier"),
        "pytorch": LazyImport(".pytorch_classifier", "PYClassifier"),
        "tensorflowONNX": LazyImport(".tensorflowONNX_classifier", "TFONNXClassifier"),
        "onnxRuntime": LazyImport(".onnxRuntime_classifier", "OnnxRuntimeClassifier"),
        "server": LazyImport(".server_classifier", "ServerClassifier"),
        "embedded": LazyImport(".embedded_classifier", "EmbeddedClassifier"),
    }

    @classmethod
    def get(
        self, framework, model, shape, outputNames=None, embeddedSnapshot=None, execMode="FPGA"
    ):
        if framework not in self.spec_cls:
            raise ValueError(f"Framework {framework} is not yet supported")
        if framework == "embedded":
            from .embedded_classifier import EmbeddedClassifier

            return EmbeddedClassifier(framework, model, shape, outputNames, embeddedSnapshot)
        else:
            return self.spec_cls[framework](model, shape, outputNames, execMode)
