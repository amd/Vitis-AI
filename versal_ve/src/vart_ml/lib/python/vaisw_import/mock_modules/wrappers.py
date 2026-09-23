#!/usr/bin/env python3
# ruff: noqa: SLF001
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import inspect
import os
import platform
import sys
from contextlib import contextmanager
from contextvars import ContextVar
from enum import Enum
from enum import auto
from functools import wraps
from importlib.abc import Loader
from importlib.machinery import ModuleSpec
from types import ModuleType
from typing import Any
from typing import Callable
from typing import ContextManager
from typing import Dict
from typing import List
from typing import Optional
from typing import Sequence
from typing import Tuple
from typing import Type

from vaisw_import.VaiswStub import vaisw

# In case a module load some sub module that are also wrapped
# this dict is filled with all the wrapped modules
WrappedModules: Dict[str, ModuleType] = {}

ON_EMBD = platform.processor() in ["aarch64", ""]


def add_module_to_wrapped_modules(module: ModuleType, name: Optional[str] = None):
    global WrappedModules
    if name is None:
        name = module.__name__
    if name in WrappedModules:
        raise RuntimeError(
            f"Module wrappers {WrappedModules[name].__name__} and {module.__name__} "
            f"share the same alias {name}"
        )
    WrappedModules[name] = module


# Simple way to avoid wrapper at any time
_global_protect: ContextVar[bool] = ContextVar("_global_protect", default=False)


@contextmanager
def disable_wrap():
    """Wrapper need to be disable in each entry into vaiswRunner to avoid re-entry."""
    try:
        global _global_protect
        orig_val = _global_protect.get()
        _global_protect.set(True)
        yield
    finally:
        _global_protect.set(orig_val)


class MarkedWrapper:
    """Base class to mark wrappers for the upper world"""

    pass


def is_vaisw_wrapper(cls):
    # fmt: off
    return ((inspect.isclass(cls) and issubclass(cls, MarkedWrapper))
            or isinstance(cls, MarkedWrapper))
    # fmt: on


class FrameworkFallbackExc(Exception):
    """Special exception to avoid intercepting function from this object anymore.

    When this exception is raised, the wrapper will mark the object as not supported
    and future call will always be passthrough the original version.
    """

    pass


class Framework(Enum):
    Tensorflow = auto()  # tf1
    Keras = auto()  # keras and tf2
    Pytorch = auto()
    Caffe = auto()
    OnnxRuntime = auto()


class NetworkWrapper(MarkedWrapper):
    """Base class for all classes that are intended to replace a network.

    All framework will call a 'network' object (with one or multiple layers)
    to handle the result. All common functions to all those objects can be
    put here.
    """

    # Declared as class attribute to always be define in object even those that
    # will not call the __init__ (ex. those retrieved from a serialization).
    network_name = None
    __runner = None

    def set_network_name(self, name: str):
        """Vaisw function to allow wrapped network to be named."""
        self.network_name = name

    def vaisw_run(self, framework, input_data, init_args=None, **kwargs):
        """Unique link to vaiswRunner so it can be changed easily

        Will create the vaiswRunner.Z* object and call run on it. The extra
        kwargs are passed to the underline run function, see this function to
        get details of the args.
        The init_args dict is passed to the Z* object creation function.
        """
        if self.__runner is None:
            # by construction we do not support different frameworks in the same
            # 'network' object
            # Beware of pickling if you modify this part (see __getstate__ below)
            if ON_EMBD or vaisw.UserConfig.get("snapshot.mode") == "run":
                from vaisw_converter import vaiswRunnerEmbedded as vaiswRunner
            else:
                from vaisw_converter import vaiswRunner
            if init_args is None:
                init_args = {}
            self.__runner = {
                Framework.Tensorflow: vaiswRunner.ZTensorflow,
                Framework.Keras: vaiswRunner.ZKeras,
                Framework.Pytorch: vaiswRunner.ZTorch,
                Framework.OnnxRuntime: vaiswRunner.ZOnnx,
            }[framework](networkName=self.network_name, **init_args)

        with disable_wrap():
            try:
                return self.__runner.run(self, input_data, **kwargs)
            except FrameworkFallbackExc:
                # do not dump framework fallback exception.
                raise
            except Exception:
                import traceback  # isort:skip

                tb = traceback.format_exception(*sys.exc_info())
                st = traceback.format_stack()
                # Full stack trace, by default traceback only goes up to here.
                vaisw.logError("".join(tb[:1] + st + tb[1:]), dump_output=False)
                raise

    def __getstate__(self):
        # The runner object may not be pickable but it is not really important
        # because we can recreate it in the next run.
        # The init part of vaisw will of course rerun on the deserialize object.
        from vaisw_converter import vaiswRunner

        return {k: v for k, v in self.__dict__.items() if not isinstance(v, vaiswRunner.Vaisw)}


class _NWDecorators:
    """Decorators for the NetworkWrapper

    This class list all decorators that will be applied to the NetworkWrapper.
    We cannot use metaclass to avoid issue if a class in the framework inherit from
    the wrapper and use another metaclass (multiple metaclass being forbidden).
    But it would be easier that way :(
    """

    NO_PROTECT = "_no_vaisw_protect"

    @staticmethod
    def no_protect(func: Callable[..., Any]) -> Callable[..., Any]:
        """Decorator to apply to functions that does not have to be protected by
        the wrap protect"""
        setattr(func, _NWDecorators.NO_PROTECT, True)
        return func

    @staticmethod
    def to_be_protected(fname: str, func: Callable[..., Any]) -> bool:
        """Return True if the function must be protected"""
        return not hasattr(func, _NWDecorators.NO_PROTECT) and callable(func) and fname != "__new__"

    @staticmethod
    def wrap_protect(cls):
        """Decorator to add recall protect to a Network wrapper class

        When acting on Network (loading, dump to onnx, ...), the framework may
        call its wrapped function to perform some caluclation. If we are not able
        to differenciate standard inference and special case, the decrator add a
        'recall_protect' context manager to a wrapper that make it fully passthrough.

        The object need to inherit from the original Network class as we call super()
        to get the original function.
        """
        # The recall_protect is meant to be used as class variable
        rec_prot_name = f"_{cls.__name__}_recall_protect"
        setattr(cls, rec_prot_name, False)
        # The self_protect is used as an instance variable. We set the self_protect
        # into the class to ensure that the value is always available.
        rec_prot_self_name = f"_{cls.__name__}_self_protect"
        setattr(cls, rec_prot_self_name, False)

        # First wrap all functions with the protection
        def _prot(name, func):
            @wraps(func)
            def wrapper(self, *args, **kwargs):
                global _global_protect
                if _global_protect.get() or getattr(cls, rec_prot_name):
                    return getattr(super(cls, self), name)(*args, **kwargs)
                elif getattr(self, rec_prot_self_name):
                    with disable_wrap():
                        # Disable wrapper globally as the instance can recall its
                        # own framework for subparts of the network.
                        return getattr(super(cls, self), name)(*args, **kwargs)
                else:
                    try:
                        return func(self, *args, **kwargs)
                    except FrameworkFallbackExc:
                        # Mark this object not supported by disabling the instance
                        # and run original function.
                        setattr(self, rec_prot_self_name, True)
                        return getattr(self, name)(*args, **kwargs)

            return wrapper

        # We wrap all defined functions. Maybe it is too much...
        for name, val in vars(cls).items():
            if _NWDecorators.to_be_protected(name, val):
                setattr(cls, name, _prot(name, val))

        # Now add the recall_protect function to the class.
        def recall_protect():
            """Context manager to put all the class as passthrough."""
            try:
                setattr(cls, rec_prot_name, True)
                yield
            finally:
                setattr(cls, rec_prot_name, False)

        cls.recall_protect = staticmethod(contextmanager(recall_protect))

        # Add also a protect function for the object instance.
        def self_recall_protect(self):
            """Context manager to put the instance as passthrough."""
            try:
                orig_value = getattr(self, rec_prot_self_name)
                setattr(self, rec_prot_self_name, True)
                yield
            finally:
                setattr(self, rec_prot_self_name, orig_value)

        cls.self_recall_protect = contextmanager(self_recall_protect)

    @staticmethod
    def transfer_vars(cls, orig_cls):
        """Decorator to transfer vars from original funcs into wrapped one.

        In tensorflow v1, tensorflow mark some of the original functions with
        a specific attribute and the behavior may change depending on the existence
        of this attribute.
        Thus we copy all attributes to the wrapping function to avoid such change.
        """
        for name, val in vars(cls).items():
            orig_val = getattr(orig_cls, name, None)
            if callable(orig_val) and _NWDecorators.to_be_protected(name, val):
                val.__dict__.update(vars(orig_val))

    @staticmethod
    def decorate(cls, orig_cls):
        assert issubclass(cls, NetworkWrapper), "These decorators are only for NetworkWrappers"
        _NWDecorators.wrap_protect(cls)
        _NWDecorators.transfer_vars(cls, orig_cls)
        return cls


class MetaModule(type):
    """Meta class for ModuleWrapper

    If the class define variables that are subclasses of NetworkWrapper, this meta
    replace them with a real wrapper in the 'Module' class. This wrapper is created
    lazily when the user access the attribute in the module.
    The wrapper inherit from the NetworkWrapper and the original class (the one
    with the same name in the original module).
    """

    def __new__(cls, cls_name, bases, clsdict):
        """
        For all NetworkWrapper in the module, we will create the wrapper class that
        inheritate from the NetworkWrapper and from the original class. This is setup
        to be called after the loading of the module so we do not have to import the
        original module ourself.

        Also all wrapper class will be decorate with NWDecorators.decorate function.
        """
        net_wraps = [
            (k, v)
            for k, v in clsdict.items()
            if inspect.isclass(v) and issubclass(v, NetworkWrapper)
        ]

        wrapped_cls = {}
        for prop, wrap_cls in net_wraps:
            # All class variable will be wrapped and correctly set as property for access.
            local_name = f"_{cls_name}_{prop}"
            wrapped_cls[wrap_cls] = local_name
            code = cls._wrapped_type_code(local_name, prop, wrap_cls, wrapped_cls)
            exec(
                "\n    ".join([f"def {local_name}(self):"] + code),
                {wrap_cls.__name__: wrap_cls, **globals()},
                clsdict,
            )
            clsdict[prop] = property(clsdict[local_name])

        # Override the after_exec_hook to make special change
        clsdict["_after_exec_hook"] = cls._after_exec_code(
            clsdict.get("_after_exec_hook", lambda _: None), net_wraps, wrapped_cls
        )

        return super().__new__(cls, cls_name, bases, clsdict)

    @staticmethod
    def _after_exec_code(
        orig_after_exec_func,
        net_wraps: List[Tuple[str, Any]],
        wrapped_cls: Dict[NetworkWrapper, str],
    ):
        """Make wrapper for the after_exec_hook

        This allow to the wrapper to make some special change after the loading
        of the original module.
        If the wrapper already have this function set it is passed as orig_after_exec_func
        here to be called at the end.
        """
        wrappers = {prop: wrapped_cls[wcls] for prop, wcls in net_wraps}

        def _implicit_wrap(self):
            """Wrap all class that inherit from a wrapped class

            Here we look for local classes that inherited from class that
            are wrapped. We enforce the wrapping of such class, otherwise the
            inheritance will be invalid. As we do not alter the class we simply
            wrap it with an emplty class.
            e.g.:
              orig module :
              class A:
                  ...
              class B(A):
                  ...

              wrapper :
                class Wa(NetworkWrapper):
                    ...

            In such case a wrapper for B is automatically setup so the B class
            correctly inherit from the wrapped A class :
              wrapped classes (created at runtime):
                class A'(Wa, A):
                    pass
                class B'(B, A'):
                    pass
            """
            for vname in dir(self):
                v = getattr(self, vname)
                if (
                    not vname.startswith("_")  # ignore "private" values
                    and inspect.isclass(v)
                    and not issubclass(v, NetworkWrapper)
                ):
                    inherit = [
                        getattr(self, wrappers[c.__name__])()
                        for c in v.mro()
                        if c.__name__ in wrappers
                    ]
                    if len(inherit) > 0:
                        d = {}
                        if "__class_getitem__" in dir(v):
                            # Handling of Generic which define the __class_getitem__
                            # and should then be define in the wrapper.
                            d["__class_getitem__"] = v.__class_getitem__
                        setattr(self, vname, type(f"Vaisw{vname}", (v, *inherit), d))

        def __hook(self):
            # call orig func at the end
            _implicit_wrap(self)
            orig_after_exec_func(self)

        return __hook

    @staticmethod
    def _wrapped_type_code(
        local_name: str, prop: str, wrap_cls: NetworkWrapper, wrapped_cls: Dict[NetworkWrapper, str]
    ):
        """Make the code to create the wrapped class.

        The wrapped class inherit from wrapper and the prop (the original property
        from the original module).
        If the wrapper inherit from another wrapper, the wrapped class of this other
        wrapper will correctly setup the class hierarchy. This is needed when a class
        inherit from another in the same module and we want to wrap both classes.
        In case of inheritance from another module, nothing special need to be done.
        e.g.:
          orig module :
            class A:
                ...
            class B(A):
                ...

          wrapper :
            class Wa(NetworkWrapper):
                ...
            class Wb(Wa): # Mark inheritance in the wrapper too.
                ...

          wrapped classes (created at runtime, no code from them as all come from wrappers):
            class A'(Wa, A):
                pass
            class B'(Wb, B, A'):
                pass

        The final hierarchy is the following (it would be the same if B was in
        another module and import A, which would be A' in the wrapping context) :

                      Wa          A
                       \\        /
                        -- A' --
                          /
             Wb          B
              \\        /
                -- B' --

        In case of multiple wrapping of the same object, we avoid wrapping the
        wrapper. Otherwise we have an inheritance loop.
        """
        wrapper = wrap_cls.__name__
        wrap_name = f"Vaisw{prop}"

        # Handling inheritance
        # We always first inherit from the wrapper (which we decorate)
        wrap_inherit = [f"_NWDecorators.decorate({wrapper}, self.orig_module.{prop})"]
        # then we inherit from the original property
        wrap_inherit += [f"self.orig_module.{prop}"]
        # finally we add all already wrapped classes to be transparent with
        # the local inheritance schema
        wrap_inherit += [f"self.{wrapped_cls[c]}()" for c in wrap_cls.mro()[1:] if c in wrapped_cls]
        wrap_inherit = "({})".format(", ".join(wrap_inherit))  # make a tuple

        local_var = f"{local_name}_zwrap_"
        code = [
            f"if not hasattr(self, {local_var!r}):",
            f"    if issubclass(self.orig_module.{prop}, {wrapper}):",
            f"        self.{local_var} = self.orig_module.{prop}",
            f"    else:",
            f"        self.{local_var} = type('{wrap_name}', {wrap_inherit}, {{}})",
        ]

        if sys.version_info < (3, 7):
            # Special case for generic in python <= 3.6
            # On this version the __class_getitem__ does not exists yet so we need
            # to setup the wrapper class to be 'like a Generic'
            # typing is imported multiple times but it is not an issue.
            code += [
                f"    import typing",
                f"    if type(self.orig_module.{prop}) is typing.GenericMeta:",
                f"        self.{local_var}.__parameters__ = self.orig_module.{prop}.__parameters__",
            ]

        code += [f"return self.{local_var}"]
        return code


class ModuleWrapper(ModuleType, MarkedWrapper, metaclass=MetaModule):
    """Wrapper for any module

    This wrapper is seen as a module. All attributes accessible through the
    original module is accessible in this wrapper. Any attribute overwritten
    by the wrapper will shadow the original attribute, allowing to do other actions.
    The 'orig_module' property allow to retrieve the original module.

    The original module must be available when constructing this wrapper.
    """

    @contextmanager
    def _module_pass_through(self):
        """Context manager to allow importing the original module under it.

        This is mostly intended to be used during module initilization so that
        exec code can import the original module to make some changes on it
        without being distrupt by the wrapper.
        """
        try:
            sys.modules[self.__name__] = self.__orig_module
            yield
        finally:
            sys.modules[self.__name__] = self

    def __init__(
        self,
        module: ModuleType,
        spec: ModuleSpec = None,
        doc: str = None,
        aliases: Sequence[str] = [],
        attrs: Dict[str, Any] = None,
    ):
        """Define the original module through the module param.

        One can define spec and doc for this module.
        'aliases' is used to setup the 'modules' dict with all aliases linked
        to this module wrapper.
        'attrs' allows to simply set any attributes to the module.
        """
        # get name from module
        name = module.__name__
        cls_name = getattr(self.__class__, "name", "")
        if cls_name and name != cls_name:
            raise AttributeError("This wrapper can only be used with {!r} module", cls_name)

        # get also doc from the class if available
        doc = getattr(self.__class__, "doc", doc)

        # init module
        super().__init__(name, doc)
        self.__loader__ = spec.loader if spec else None
        self.__spec__ = spec
        self.__package__ = cls_name

        # Set module to be able to retrieve values from it.
        self.__orig_module = module

        # Setup the 'modules' with all aliases.
        add_module_to_wrapped_modules(self)  # Add self name as alias.
        for s in set(aliases):  # Use set to remove duplicate.
            add_module_to_wrapped_modules(self, s)

        # All attrs variables are define in the wrapper
        if attrs is None:
            attrs = {}
        try:
            for k, v in attrs.items():
                setattr(self, k, v)
        except BaseException:
            raise AttributeError(f"Can't set attribute {k!r} in module {name}") from None

    @property
    def orig_module(self):
        return self.__orig_module

    def __getattr__(self, name):
        # All non defined values are retrived from the original module.
        if self.__orig_module is None:
            raise AttributeError("Module is not set")
        return getattr(self.__orig_module, name)

    def __dir__(self):
        # dir(vaisw_module) correctly gives all the functions from the underlying module.
        if self.__orig_module is None:
            raise AttributeError("Module is not set")
        return dir(self.__orig_module)

    # exec module hooks, called before and after the exec_module of the sub module.
    # The sub module is set before these functions are called
    # Only called when the module is loaded through a Spec.
    def _before_exec_hook(self):
        """Called at the beginning of the exec_module function in the Spec wrapper"""
        pass

    def _after_exec_hook(self):
        """Called at the end of the exec_module function in the Spec wrapper"""
        pass


class VaiswAvailableModuleSpec(ModuleSpec):
    """Special spec for modules that were already imported but not put in sys.modules"""

    def __init__(self, module):
        self._module = module
        super().__init__(module.__name__, self)

    def __repr__(self):
        return f"<class {self.__class__.__qualname__}({self._module.__name__})>"

    def create_module(self, _spec):
        return self._module

    def exec_module(self, module):
        # module is already import. Nothing to do
        # TODO: see if calling the hooks is useful in this case.
        pass


class VaiswWrapperSpec(ModuleSpec, Loader):
    """Vaisw Wrapper for modules spec

    Load the original module pass as parameter and return the wrapper specified
    in the constructor. The wrapper must hinerit from the ModuleWrapper class.

    The exec_wrp wrap the exec_module into a 'with' statement
    """

    def __init__(
        self,
        wrapper: Type[ModuleWrapper],
        name: str,
        path: Optional[List[str]],
        exec_wrp: Optional[ContextManager[None]] = None,
    ):
        self._wrapper = wrapper
        # exec_wrp is not mandatory
        self._exec_wrp = exec_wrp if exec_wrp else self._no_context()
        self._mod_name = name
        super().__init__(name, self, origin=os.path.join("vaisw_modules", *name.split(".")) + ".py")

        # get original spec (vaisw finder is marked so we filter it out)
        for imp in sys.meta_path:
            if not is_vaisw_wrapper(imp):
                self._spec = imp.find_spec(self._mod_name, path)
                if self._spec:
                    break
        else:
            raise ImportError(f"Cannot find module {self._mod_name!r}")

    @contextmanager
    def _no_context(self):
        yield

    def __repr__(self):
        return f"<class {self.__class__.__qualname__}({self._mod_name})>"

    def create_module(self, _orig_spec):
        sub_mod = self._spec.loader.create_module(self._spec)
        if sub_mod is None:
            # default import mechanism
            from importlib import util

            sub_mod = util.module_from_spec(self._spec)
        return self._wrapper(module=sub_mod, spec=self)

    def exec_module(self, module_wrp):
        module_wrp._before_exec_hook()

        with self._exec_wrp, module_wrp._module_pass_through():
            self._spec.loader.exec_module(module_wrp.orig_module)

        module_wrp._after_exec_hook()
