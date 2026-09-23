# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


import numpy as np
import onnx
from onnx_tf.backend import prepare

from .. import Stats
from .IClassifier import IClassifier


class TFONNXClassifier(IClassifier):
    """Tensorflow classifier"""

    INPUT_NODE_NAME = "tfinput"

    def _load_network(self, model):
        """Import tensorflow graph."""
        onnx_model = onnx.load(model.network.name)  # load onnx model
        self.graph_session = prepare(onnx_model)

    def run(self, batch, stats=Stats.Stats()):
        """Run graph on the batch parameter
        Return a list of output matrix based on the number of output layers.
        """
        shape = batch.shape
        batch = np.reshape(batch, (shape[0], shape[3], shape[1], shape[2]))
        with stats.predict_stats():
            return self.graph_session.run(batch)
