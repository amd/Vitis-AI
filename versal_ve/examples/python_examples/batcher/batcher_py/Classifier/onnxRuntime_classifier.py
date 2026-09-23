# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


import onnxruntime  # to inference ONNX models, we use the ONNX Runtime

try:
    from vaisw_import import is_vaisw_wrapper
except ImportError:

    def is_vaisw_wrapper(_):
        return False


from .. import Image
from .. import Stats
from ..utils import log
from .IClassifier import IClassifier


class OnnxRuntimeClassifier(IClassifier):
    """OnnxRuntime Classifier"""

    def setup_preprocess(self, proteus):
        """OnnxRuntime only take NCHW images."""
        proteus.check_preproc_out(format=Image.Format.NCHW)
        super().setup_preprocess(proteus)

    def _load_network(self, network):
        log.log("load onnx file...")
        sess_options = onnxruntime.SessionOptions()
        sess_options.intra_op_num_threads = 1  # onnx is not compatible with slurm in multi core :/
        self.model = onnxruntime.InferenceSession(network.network.name, sess_options=sess_options)
        self.inputs = self.model.get_inputs()
        assert len(self.inputs) == 1, "We only support one input node"
        self.input_name = self.inputs[0].name
        if is_vaisw_wrapper(self.model):
            print("is wrapped")
            self.model.set_network_name(self.network_name)

    def _set_batch_size(self, batch_size):
        if not is_vaisw_wrapper(self.model):
            modelBatchSize = self.inputs[0].shape[0]
            if isinstance(modelBatchSize, str):
                modelBatchSize = batch_size
            assert modelBatchSize > 0 and modelBatchSize == batch_size, (
                f"onnxruntime supports only batch size equal to model batch size, use a batch size of {modelBatchSize}"
            )

    def run(self, batch, stats=Stats.Stats()):
        """Run graph on the batch parameter
        Return a list of output matrix based on the number of output layers.
        """
        with stats.predict_stats():
            log.log("Running onnx inference")
            return [self.model.run([], {self.input_name: batch.data})[0]]
