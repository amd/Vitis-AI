#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from .tensorflow import check_tf_inputs
from .tensorflow import reject_fetches
from .wrappers import Framework
from .wrappers import ModuleWrapper
from .wrappers import NetworkWrapper
from .wrappers import disable_wrap


class VaiswLayerWrapper(NetworkWrapper):
    def __call__(self, inputs, *args, **kwargs):
        if check_tf_inputs(inputs):
            # If input is not fully define, send back to the original function.
            return super().__call__(inputs, *args, **kwargs)
        else:
            # call always returns a tensor
            return self.vaisw_run(
                Framework.Keras, inputs, init_args={"io_convert_Tensor_numpy": True}
            )


class BaseLayerWrapper(ModuleWrapper):
    """Wrapper for keras.engine.base_layer module"""

    Layer = VaiswLayerWrapper


class VaiswModelWrapper(NetworkWrapper):
    def __new__(cls, *args, **kwargs):
        # In tensorflow 2/keras, the Model class enforce the use of a functional
        # Model when creating a Model (the class itself and not a sub class)
        # with the correct parameters. As we cannot predict that, we recall
        # the original __new__ function with the original class for this
        # specific case.
        # As __new__ is a special form in python we must be very careful when
        # wrapping it.
        if super().__new__ is object.__new__:
            # object.__new__ only take the class as parameter.
            return super().__new__(cls)
        elif len(cls.__bases__) == 2 and cls.__bases__[0] == VaiswModelWrapper:
            # We are calling new on the Model class (on our wrapper here).
            # Pass the original class object to the super.__new__.
            return super().__new__(cls.__bases__[1], *args, **kwargs)
        else:
            return super().__new__(cls, *args, **kwargs)

    def build(self, input_shape):
        with VaiswLayerWrapper.recall_protect():
            super().build(input_shape)

    def predict(self, in_data, *args, **kwargs):
        if reject_fetches(self.outputs):
            return super().predict(in_data, *args, **kwargs)

        import tensorflow as tf

        if isinstance(in_data, tf.data.Dataset):
            results = []
            for data in in_data:
                if isinstance(data, tuple):
                    # Labels can be provided, even if they are not used
                    data = data[0]
                results.append(self.vaisw_run(Framework.Keras, data))
            return results

        return self.vaisw_run(Framework.Keras, in_data)

    def predict_on_batch(self, in_data):
        if reject_fetches(self.outputs):
            return super().predict_on_batch(in_data)
        return self.vaisw_run(Framework.Keras, in_data)


class TrainingWrapper(ModuleWrapper):
    """Wrapper for keras.engine.training module"""

    Model = VaiswModelWrapper


class KSaveWrapper(ModuleWrapper):
    """Wrapper for keras.saving.save module"""

    def load_model(self, *args, **kwargs):
        # load may call the model so we protect it.
        with disable_wrap():
            return self.orig_module.load_model(*args, **kwargs)
