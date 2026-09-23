# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


try:
    from vaisw_import import is_vaisw_wrapper
except ImportError:

    def is_vaisw_wrapper(_):
        return False


import os

import tensorflow as tf

from .. import Image
from .. import Stats
from ..utils import log
from .IClassifier import IClassifier


class TFClassifier(IClassifier):
    """Tensorflow classifier"""

    INPUT_NODE_NAME = "tfinput"

    def setup_preprocess(self, proteus):
        """Tensorflow only take NHWC images."""
        proteus.check_preproc_out(format=Image.Format.NHWC)
        super().setup_preprocess(proteus)

    @staticmethod
    def get_io_layers(graph_def):
        """Retrieve the input and output layers from a tensorflow GraphDef.
        Returns a tuple of the list of input nodes and the list of output layers' name.
        """
        inputs = []  # list entry nodes (placeholders)
        nodes = []  # list all nodes
        full_inputs = set()  # list nodes that are inputs of others
        for n in graph_def.node:
            if n.op != "Const" and n.op != "NoOp":
                nodes.append(n.name)
                for inp in n.input:
                    nodes.append(inp)
                    full_inputs.add(inp)
                if len(n.input) == 0:
                    inputs.append(n)
        outputs = [o for o in nodes if o not in full_inputs]
        return inputs, outputs

    def _load_network(self, model):
        """Import tensorflow graph."""
        if not os.path.isdir(model.network.name):
            self.keras = False
            self.tfgraph = tf.compat.v1.Graph()
            graph_def = tf.compat.v1.GraphDef()
            graph_def.ParseFromString(model.network.read())
            inputs, outputs = self.get_io_layers(graph_def)
            assert len(inputs) == 1, "We only support one input node"

            self.input_layer = inputs[0].name
            if len(inputs[0].attr["shape"].shape.dim) == 4:
                input_shape = [d.size for d in inputs[0].attr["shape"].shape.dim]
            elif len(inputs[0].attr["shape"].shape.dim) == 0:
                # unconstrained shape
                input_shape = [-1, -1, -1, -1]
            else:
                raise RuntimeError(
                    "Unsupprted input shape {} from layer {}".format(
                        inputs[0].attr["shape"].shape.dim, self.input_layer
                    )
                )
            if self.output_layers is None:
                self.output_layers = outputs

            # NHWC shape
            self._validate_input_shape(
                (input_shape[3], input_shape[1], input_shape[2]), self.input_layer
            )
            with self.tfgraph.as_default():
                if (
                    input_shape[0] != -1
                    or input_shape[1] != self.input_geometry.height
                    or input_shape[2] != self.input_geometry.width
                ):
                    dataType = inputs[0].attr["dtype"].type  # keep data type.
                    # Replace input layer to support multi image batch
                    ph = tf.compat.v1.placeholder(
                        dataType,
                        shape=(
                            None,
                            self.input_geometry.height,
                            self.input_geometry.width,
                            self.input_geometry.channels,
                        ),
                        name=self.INPUT_NODE_NAME,
                    )
                    tf.compat.v1.import_graph_def(
                        graph_def, input_map={self.input_layer + ":0": ph}, name=""
                    )
                    self.input_layer = self.INPUT_NODE_NAME
                else:
                    tf.compat.v1.import_graph_def(graph_def, name="")

                # Set name in graph though a constant node
                tf.compat.v1.constant("runSession.networkName=" + self.network_name, name="vaisw")

            self.graph_session = tf.compat.v1.Session(graph=self.tfgraph)
            self.input_operation = self.tfgraph.get_operation_by_name(self.input_layer).outputs[0]
            self.output_operations = [
                self.tfgraph.get_operation_by_name(l).outputs[0] for l in self.output_layers
            ]
            log.info("input :", self.input_operation)
            log.info("outputs :", self.output_operations)
            if is_vaisw_wrapper(self.graph_session):
                self.graph_session.set_network_name(self.network_name)
        else:
            # load keras model
            self.keras = True
            self.networkName = model.network.name
            log.info("network name = " + self.networkName)
            self.model = tf.keras.models.load_model(self.networkName)
            if is_vaisw_wrapper(self.model):
                self.model.set_network_name(self.network_name)

    def run(self, batch: Image.Batch, stats=Stats.Stats()):
        """Run graph on the batch parameter
        Return a list of output matrix based on the number of output layers.
        """
        with stats.predict_stats(log_time=True):
            if self.keras:
                # TODO: review this
                out = self.model.predict(batch.data)
                if not isinstance(out, list) and not isinstance(out, dict):
                    out = [out]
                elif isinstance(out, list) and len(out) == 1 and isinstance(out[0], dict):
                    out = out[0]
                return out
            else:
                return self.graph_session.run(
                    self.output_operations, {self.input_operation: batch.data}
                )
