#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from types import ModuleType

from .mock_modules.wrappers import MarkedWrapper


class MockingObject:
    """Simple mock object that should not be used.

    All access to attributes should raise AttributeError unless the attribute
    was explicitly set.
    Calling the object like a function return another Mock object.
    """

    def __getattr__(self, name):
        raise AttributeError(f"Object {self.name} is a Mocking class and cannot be used.")

    def __repr__(self):
        return f"<MockingObject {self.name} id={id(self)}>"

    def __call__(self, *_args, **_kwargs):
        attr = "__" + self.name + "_call"
        if not hasattr(self, attr):
            r = MockingObject()
            r.name = self.name + ".__call__()"
            setattr(self, attr, r)
        return getattr(self, attr)


class MockingModule(ModuleType, MarkedWrapper):
    """False class mocking a module.

    Return MockingObject subclass on every attributes access.
    """

    def __init__(self, name: str, doc: str = None):
        self.name = name
        super().__init__(name, doc)
        # Make a fake __file__ dunder method to avoid issue when inspecting mocking modules
        self.__file__ = f"MockingModule({self.name})"
        # Make a fake __version__ dunder method to avoid issue on package version checking
        self.__version__ = "1.0"

    def __repr__(self):
        return f"<MockingModule {self.name} id={id(self)}>"

    def __getattr__(self, name):
        mo = type(name, (MockingObject,), {"name": self.name + "." + name})
        setattr(self, name, mo)
        return mo
