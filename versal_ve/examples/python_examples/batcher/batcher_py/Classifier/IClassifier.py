# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


from .. import Stats
from .. import utils


class IClassifier:
    """Base class for all framework classifier"""

    def __init__(self, model, shape, outputNames=None, execMode="FPGA"):
        self.input_geometry = shape
        self.network_name = model.name
        self.output_layers = outputNames
        self.execMode = execMode

        # Hack to call vaiswRunner summary if this is set to a function.
        # TODO: remove when it become useless.
        self.summary = None

        utils.log.log("---------- Load label file ----------")
        self._load_labels(model)
        utils.log.log("---------- Load the network ----------")
        self._load_network(model)

    def _load_labels(self, model):
        """Load the label file in form : one label per line in order."""
        self.labels = []
        for l in model.labels:
            self.labels.append(l.strip())

    def _load_network(self, model):
        """Load the network on current framework. Must be overwrite by child classes."""
        raise NotImplementedError

    def _validate_input_shape(self, input_shape, input_name):
        """Function to validate that the input shape is correctly set.
        Must be called by the _load_network function in child classes.
        """
        if not isinstance(input_shape, utils.Shape):
            input_shape = utils.Shape._make(input_shape)
        if self.input_geometry is None:
            utils.log.info(f"input geometry set to {input_shape}")
            self.input_geometry = input_shape
        elif self.input_geometry != input_shape:
            utils.log.warning(
                f"input is set to {self.input_geometry} while layer '{input_name}' wait an input in form {input_shape}"
            )

    def get_model_name(self):
        return self.network_name

    def setup_preprocess(self, proteus):
        """Setup preprocessing for the classifier. Can enforce some preprocess that are invalid
        for the current classifier.
        Also the classifier can retrieve some information from the preprocess.
        Calling this function is mandatory to correctly setup the cassifier.
        """
        # Set the batch size based on the proteus to be sure that it is correct
        self._set_batch_size(proteus.batch_size)

    def _set_batch_size(self, batch_size):
        """Set the input batch size for the graph.
        This allows to specify the correct batch size of the network.
        """
        pass

    def run(self, batch, stats=Stats.Stats()):
        """Run the network with the batch input.

        batch (Image Batch): The batch to predict.
        stats (Stats object) : Can be passed to be filled with the time spent
                               in the prediction process.
        Must be overwrite by child classes.
        """
        raise NotImplementedError

    def train(self, proteus, labels):
        """Train the network with the proteus examples

        proteus (Proteus Images): The batch to predict.
        labels (classes): The class to predict
        Must be overwrite by child classes.
        """
        raise NotImplementedError
