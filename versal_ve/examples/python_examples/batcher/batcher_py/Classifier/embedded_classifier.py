# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import os

from VART import Runner

from .. import Image
from .. import Stats
from .IClassifier import IClassifier


class EmbeddedClassifier(IClassifier):
    def __init__(self, _framework, model, shape, outputNames, snapshot):
        self.snapshot = os.path.expanduser(snapshot)
        self.image_format = Image.Format.NHWC if "tensorflow" in model.path else Image.Format.NCHW
        super().__init__(model, shape, outputNames)

    def setup_preprocess(self, proteus):
        proteus.check_preproc_out(format=self.image_format)
        self.batch_size = proteus.batch_size
        super().setup_preprocess(proteus)

    def _load_network(self, model):
        self.vaisw = Runner(snapshot_dir=self.snapshot, network_name=model.name)

    def run(self, batch, stats=Stats.Stats()):
        with stats.predict_stats():
            return self.vaisw.run(batch.data)
