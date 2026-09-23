#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from collections.abc import Sequence

from ..VaiswStub import vaisw
from .wrappers import Framework
from .wrappers import ModuleWrapper
from .wrappers import NetworkWrapper
from .wrappers import _NWDecorators
from .wrappers import disable_wrap


def reject_fetches(fetches):
    rejected_fetches = vaisw.UserConfig.get("runSession.rejectTfRunSession")
    if rejected_fetches is None or len(rejected_fetches) == 0:
        return False
    else:

        def _name(op):
            return op if isinstance(op, str) else op.name

        rejected_fetches = set(rejected_fetches.split(","))
        if isinstance(fetches, dict):
            fetches_name = {_name(op) for op in fetches.values()}
        elif isinstance(fetches, (list, tuple)):
            fetches_name = {_name(op) for op in fetches}
        else:
            fetches_name = {_name(fetches)}
        return fetches_name == rejected_fetches


class VaiswSessionWrapper(NetworkWrapper):
    """Base class to overwrite the tf.compat.v1 Session class.

    The real wrapper will inherit from this class and the original Session
    class from tensorflow. Thus calling super() acually lead to access the
    original class.
    """

    def __fetches_variables(self, fetches, feed_dict):
        # Check that the fetches is not used to assign variable
        import tensorflow as tf  # already imported at this point

        if not isinstance(fetches, Sequence):
            fetches = [fetches]

        # fetching AssignVariable Operation is run to set variables. fallback
        if any(getattr(op, "type", None) == "AssignVariableOp" for op in fetches):
            return True

        if len(fetches) == len(feed_dict):
            for fetch in fetches:
                # In tf1, fetching variables with value is used to set values to variables.
                if isinstance(fetch, tf.Variable):
                    return True
                # Keras load_weights_from_hdf5_group use placeholders
                elif isinstance(fetch, tf.Tensor):
                    op = fetch.op
                    if op.type == "Assign" and op.inputs[-1] in feed_dict.keys():
                        return True
        return False

    # Get outputs by name. May not work if fetches are operations.
    def __name(self, op):
        return op if isinstance(op, str) else op.name

    def __flatten_fetches(self, fetches):
        fetches_sizes = []
        vaisw_fetches = []
        for fetch in fetches:
            fetches_sizes.append(len(fetch))
            if isinstance(fetch, (list, tuple)):
                vaisw_fetches.extend(self.__name(op) for op in fetch)
            elif isinstance(fetch, dict):
                vaisw_fetches.extend(self.__name(op) for op in fetch.values())
            else:
                vaisw.logInfo(f"Nested fetches of type {type(fetch)} may not be supported\n")
                vaisw_fetches.extend(self.__name(op) for op in fetch)
        return fetches_sizes, vaisw_fetches

    def run(self, fetches, feed_dict=None, options=None, run_metadata=None):
        if not isinstance(feed_dict, dict):
            # fallback to original run.
            vaisw.logInfo(f"feed_dict={feed_dict} is not supported: fallback to tensorflow\n")
            return super().run(
                fetches, feed_dict=feed_dict, options=options, run_metadata=run_metadata
            )
        elif self.__fetches_variables(fetches, feed_dict):
            return super().run(
                fetches, feed_dict=feed_dict, options=options, run_metadata=run_metadata
            )
        elif reject_fetches(fetches):
            vaisw.logInfo(f"Fallback to tensorflow: Rejected session fetches = {fetches}\n")
            return super().run(
                fetches, feed_dict=feed_dict, options=options, run_metadata=run_metadata
            )
        else:
            # According to the tensorflow doc :
            # The fetches argument may be a single graph element, or an arbitrarily nested
            # list, tuple, namedtuple, dict, or OrderedDict containing graph elements at its
            # leaves. The value returned by run() has the same shape as the fetches argument,
            # where the leaves are replaced by the corresponding values returned by TensorFlow.

            # vaiswRunner always return a list.
            if isinstance(fetches, dict):
                out = self.vaisw_run(
                    Framework.Tensorflow,
                    feed_dict,
                    fetches=[self.__name(op) for op in fetches.values()],
                )
                return dict(zip(fetches, out))
            elif isinstance(fetches, (list, tuple)):
                # Nested fetches must have the same type
                if isinstance(fetches[0], (dict, list, tuple)):
                    # Flatten nested lists before sending fetches to Vaisw
                    fetches_size, vaisw_fetches = self.__flatten_fetches(fetches)
                    out = self.vaisw_run(Framework.Tensorflow, feed_dict, fetches=vaisw_fetches)
                    # As Vaisw return a list, reshape output the same way as input fetches
                    nested_out = []
                    if isinstance(fetches[0], (list, tuple)):
                        for i, _ in enumerate(fetches):
                            nested_out.append(out[i * fetches_size[i] : (i + 1) * fetches_size[i]])
                    elif isinstance(fetches[0], dict):
                        for i, fetch in enumerate(fetches):
                            nested_out.append(
                                dict(
                                    zip(fetch, out[i * fetches_size[i] : (i + 1) * fetches_size[i]])
                                )
                            )
                    else:
                        return out
                    return nested_out

                else:
                    return self.vaisw_run(
                        Framework.Tensorflow, feed_dict, fetches=[self.__name(op) for op in fetches]
                    )
            else:
                out = self.vaisw_run(
                    Framework.Tensorflow, feed_dict, fetches=[self.__name(fetches)]
                )
                if len(out) != 1:
                    raise RuntimeError(
                        f"Invalid run fetching {fetches}, getting {len(out)} outputs"
                    )
                return out[0]


class VaiswClientSessionWrapper(ModuleWrapper):
    """Wrapper for tensorflow.python.client.session module

    This module contains the original Session definition. It is valid
    for both tensorflow 1 and 2.
    """

    Session = VaiswSessionWrapper
    InteractiveSession = VaiswSessionWrapper


class VaiswSaverWrapper(NetworkWrapper):
    def restore(self, sess, save_path):
        # disable wrapper when restoring checkpoint
        with VaiswSessionWrapper.recall_protect():
            super().restore(sess, save_path)


class TFSaverWrapper(ModuleWrapper):
    """Wrapper for tensorflow.python.training.saver module"""

    Saver = VaiswSaverWrapper


def check_tf_inputs(inputs) -> bool:
    """Check data if is must fallback or can be used by vaisw.
    return True if we need to fallback to the original command.
    """
    import tensorflow as tf  # We consider tf to be available.

    def non_defined_tfTensor(tensor):
        # if data is regular tf/keras tensor, check if its shape is fully defined
        # and fallback if not.
        return (
            isinstance(getattr(tensor, "shape", None), tf.TensorShape)
            and not tensor.shape.is_fully_defined()
        )

    if isinstance(inputs, tf.Tensor):
        # Only EagerTensor (tensor with data) have numpy define. Symbolic tensors don't.
        # FIXME: I am not sure how to handle Sequence of Tensors so I leave
        # this case for future self, if it happens.
        return not hasattr(inputs, "numpy")
    elif isinstance(inputs, (list, tuple)):
        return any(non_defined_tfTensor(i) for i in inputs)
    else:
        # If the data is unknown we consider it valid for vaisw.
        return non_defined_tfTensor(inputs)


class VaiswFunctionWrapper(NetworkWrapper):
    def __call__(self, *args, **kwargs):
        inputs = args[0] if len(args) == 1 else args
        if check_tf_inputs(inputs):
            # If input is not fully define, send back to the original function.
            return super().__call__(inputs, *args, **kwargs)
        else:
            return self.vaisw_run(Framework.Keras, inputs, init_args={"model_type": "function"})


class TFEagerDefFunctionWrapper(ModuleWrapper):
    """Wrapper for tensorflow.python.eager.def_function module"""

    Function = VaiswFunctionWrapper


class VaiswConcreteFunctionWrapper(NetworkWrapper):
    @_NWDecorators.no_protect
    def get_concrete_function(self, *_args, **_kwargs):
        # ConcreteFunction does not implement get_concrete_function because it is
        # already a concrete function (Thanks captain obvious !)
        # Anyway tf2onnx use it as a tf.Function which implement this function to
        # retrieve the concrete version of itself. We implement it to avoid issues
        # internally.
        return self

    def __call__(self, *args, **kwargs):
        inputs = args[0] if len(args) == 1 else args
        if check_tf_inputs(inputs):
            # If input is not fully define, send back to the original function.
            return super().__call__(inputs, *args, **kwargs)
        else:
            return self.vaisw_run(
                Framework.Keras,
                inputs,
                init_args={"model_type": "concrete_function", "io_convert_Tensor_numpy": True},
            )


class TFEagerFunctionWrapper(ModuleWrapper):
    """Wrapper for tensorflow.python.eager.function module"""

    ConcreteFunction = VaiswConcreteFunctionWrapper


class TFSavedModelLoad(ModuleWrapper):
    """Wrapper for tensorflow.python.saved_model.load module"""

    def load(self, *args, **kwargs):
        # load may call the model so we protect it.
        with disable_wrap():
            return self.orig_module.load(*args, **kwargs)


class TFWrapper(ModuleWrapper):
    """Wrapper for tensorflow module"""

    def _after_exec_hook(self):
        # This hook trigger right after tf import finished
        # We prevent TF from using all GPU mem, to prevent frontend crash when it
        # need GPU utilization for simulation (calib, deepQ, etc.)
        # Somehow wrapping tensorflow.python.framework.config is not enough and create
        # issues in the run (TODO: maybe find why and fix that).
        try:
            from tensorflow.python.framework.config import set_visible_devices

            set_visible_devices([], "GPU")
        except ModuleNotFoundError:
            pass
