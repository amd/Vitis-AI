# Copyright(C) 2026 Advanced Micro Devices Inc.  All Rights Reserved.


class PassThroughWrapper:
    """Wrapper that forwards the first argument by default.
    It can pass through the original function call using the call_original_func method.
    """

    def __init__(self, func):
        self.func = func
        self.registered_args = None
        self.registered_kwargs = None

    def __call__(self, x, *args, **kwargs):
        # additional args/kwargs are stored for potential later use in call_original_func
        self.registered_args = args
        self.registered_kwargs = kwargs
        return x

    def call_original_func(self, x):
        return self.func(x, *(self.registered_args), **(self.registered_kwargs))

    def get_original_input(self):
        raise NotImplementedError("Original input should not be needed in PassThroughWrapper!")


class PreprocTransformDecorator:
    """Decorator that wraps a preprocess function.
    At generation time, it traces the function and exports it to ONNX.
    At runtime, it replaces the original function by a simple forward of the first argument.
    In case one needs to pass through the original function call, it can be done using the
    call_original_func method of its replacement func attribute.
    """

    def __init__(self):
        self.func = None
        self.replacement_func = None
        self.is_set = False

    def __call__(self, func):
        if self.func is not None:
            if self.func != func:
                raise ValueError("Preprocess transforms decorator can only be used once!")
            return func  # func is called normally if already set
        else:
            self.func = func
            self.is_set = True
            # We don't want to import preprocess_transforms in embedded,
            # so we catch the ImportError and use the PassThroughWrapper instead
            try:
                from vaisw_converter.preprocess_transforms import PreprocWrapper
            except ImportError:
                self.replacement_func = PassThroughWrapper(func)
                return self.replacement_func
            else:
                self.replacement_func = PreprocWrapper(func)
                return self.replacement_func

    def get_original_input(self):
        return self.replacement_func.get_original_input()

    def call_original_func(self, x):
        return self.replacement_func.call_original_func(x)

    def clean(self):
        self.func = None
        self.replacement_func = None
        self.is_set = False


preproc_transform = PreprocTransformDecorator()
