# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


import os

import torch
from packaging.version import Version

try:
    from vaisw_import import is_vaisw_wrapper
except ImportError:

    def is_vaisw_wrapper(_):
        return False


from .. import Stats
from ..Proteus import Image
from ..utils import log
from .IClassifier import IClassifier


class PYClassifier(IClassifier):
    """PyTorch Classifier"""

    def setup_preprocess(self, proteus):
        """PyTorch only take NCHW images."""
        proteus.check_preproc_out(format=Image.Format.NCHW)
        super().setup_preprocess(proteus)

    def _load_network(self, network):
        if os.path.isdir(network.network.name) and os.path.isfile(
            os.path.join(network.network.name, "hubconf.py")
        ):
            log.log("Loading model using Torch Hub...")
            self.model = torch.hub.load(
                network.network.name, network.name, pretrained=True, source="local"
            )
        else:
            log.log("Loading torch file...")
            if Version(torch.__version__) >= Version("2.6"):
                self.model = torch.load(network.network, weights_only=False)
            else:
                self.model = torch.load(network.network)
        self.model.eval()
        if hasattr(self.model, "AuxLogits"):
            log.log("Removing AuxLogits for inference")
            self.model.AuxLogits = None
        if torch.cuda.device_count():
            self.model.cuda()
        else:
            self.model.to("cpu")
        if is_vaisw_wrapper(self.model):
            self.model.set_network_name(self.network_name)

    def run(self, batch, stats=Stats.Stats()):
        """Run graph on the batch parameter
        Return a list of output matrix based on the number of output layers.
        """

        def map_output(data, output_func=lambda x: x):
            if isinstance(data, dict):
                return {k: output_func(v) for k, v in data.items()}
            if isinstance(data, list):
                if isinstance(data[0], (dict, list)):
                    return [map_output(out, output_func) for out in data]
                return [output_func(out) for out in data]
            return [output_func(data)]

        inputs = torch.from_numpy(batch.data)
        with stats.predict_stats(log_time=True):
            if torch.cuda.device_count():
                return map_output(self.model(inputs.cuda()), lambda x: x.detach().cpu().numpy())
            else:
                return map_output(self.model(inputs), lambda x: x.detach().numpy())
