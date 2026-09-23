# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


import numpy as np
import vaisw

from .. import Stats
from .IClassifier import IClassifier


# correct solution:
def softmax(x):
    """Compute softmax values for each sets of scores in x."""
    e_x = np.exp(x - np.max(x))
    return e_x / e_x.sum(axis=0)  # only difference


def concatenateResults(listResults):
    if isinstance(listResults, dict):
        assert len(listResults) > 0, "No images"
        return [listResults[key] for key in listResults]
    else:
        assert len(listResults) > 0, "No images"
        return [
            np.concatenate([b[layerName] for b in listResults])
            for layerName in listResults[0].keys()
        ]


class ServerClassifier(IClassifier):
    """Server Classifier"""

    def setup_preprocess(self, proteus):
        """Snapshot has to be exported from tensorflow"""
        # proteus.check_preproc_out(format = Image.Format.NHWC)
        super().setup_preprocess(proteus)

    def _load_labels(self, model):
        # Labels are not mandatory here but load it if possible.
        if model.have_labels():
            super()._load_labels(model)

    def _load_network(self, network):
        self.server = vaisw.Server3(vaisw.SplitSession3.NONE)
        self.networkName = network.name
        self.inputs = "I don't know yet"

    def run(self, batch, stats=Stats.Stats()):
        """Run graph on the batch parameter
        Return a list of output matrix based on the number of output layers.
        """
        with stats.predict_stats():
            self.server.upload(batch.data)
            return concatenateResults(self.server.download())
