# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


try:
    from vaisw_import import is_vaisw_wrapper
except ImportError:

    def is_vaisw_wrapper(_):
        return False


import numpy as np
import tensorflow as tf

from .. import Image
from .. import Stats
from ..utils import log
from .IClassifier import IClassifier


class TF2Classifier(IClassifier):
    """Tensorflow 2 classifier"""

    INPUT_NODE_NAME = "tfinput"

    def setup_preprocess(self, proteus):
        """Tensorflow only take NHWC images."""
        proteus.check_preproc_out(format=Image.Format.NHWC)
        super().setup_preprocess(proteus)

    def _load_network(self, model):
        """Import tensorflow graph."""
        self.networkName = model.network.name
        log.info("network name = " + self.networkName)
        try:
            self.model = tf.keras.models.load_model(self.networkName)
        except ValueError:
            self.model = tf.keras.layers.TFSMLayer(
                self.networkName, call_endpoint="serving_default"
            )
        if is_vaisw_wrapper(self.model):
            self.model.set_network_name(self.network_name)
        elif callable(self.model) and is_vaisw_wrapper(self.model.__call__):
            # In case of tensorflow 2 function loading, the return object may not be the
            # function itself but the __call__ method is the correct object.
            self.model.__call__.set_network_name(self.network_name)

    def run(self, batch: Image.Batch, stats=Stats.Stats()):
        """Run graph on the batch parameter
        Return a list of output matrix based on the number of output layers.
        """
        global clock_acc
        global clock_acc_cnt
        global clock_batch_idx
        with stats.predict_stats(log_time=True):
            out = self.model(batch.data)
        if not isinstance(out, list) and not isinstance(out, dict):
            out = [np.asarray(out)]
        elif isinstance(out, list) and len(out) == 1 and isinstance(out[0], dict):
            out = out[0]
        return out
