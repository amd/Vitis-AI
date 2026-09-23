# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import contextlib
import ctypes
import functools
import os
import re
import sys
import typing
from collections import namedtuple
from enum import Enum
from itertools import zip_longest
from multiprocessing import Pool

from .colors import ColorString
from .colors import TermColors


class log:
    class __level:
        NONE = 0
        ERROR = 5
        WARN = 10
        NORMAL = 15
        INFO = 20
        VERBOSE = 25
        __values = [NONE, ERROR, WARN, NORMAL, INFO, VERBOSE]

        def __init__(self):
            pass

        def __getitem__(self, i):
            return self.__values[i]

        def __len__(self):
            return len(self.__values)

    ERROR_MSG = ColorString("[ERROR]", TermColors.BRed)
    WARN_MSG = ColorString("[WARNING]", TermColors.BYellow)
    NORMAL_MSG = ColorString("[AMD]", TermColors.Color_Off)
    INFO_MSG = ColorString("[INFO]", TermColors.BBlue)
    VERBOSE_MSG = ColorString("[VERBOSE]", TermColors.BWhite)
    DEBUG_MSG = ColorString("[DEBUG]", TermColors.BPurple)

    level = __level()
    __cur_level = level.NORMAL

    @classmethod
    def set_verbose(cls, level):
        cls.__cur_level = level

    @classmethod
    def error(cls, *args, **kwargs):
        """Print error in visible way. Use like the print function."""
        if cls.__cur_level >= log.level.ERROR:
            print(cls.ERROR_MSG, *args, **kwargs)

    @classmethod
    def warning(cls, *args, **kwargs):
        """Print warning in visible way. Use like the print function."""
        if cls.__cur_level >= log.level.WARN:
            print(cls.WARN_MSG, *args, **kwargs)

    @classmethod
    def log(cls, *args, **kwargs):
        """Standard log message. Use like the print function."""
        if cls.__cur_level >= log.level.NORMAL:
            print(cls.NORMAL_MSG, *args, **kwargs)

    @classmethod
    def info(cls, *args, **kwargs):
        """Print info message. Use like the print function."""
        if cls.__cur_level >= log.level.INFO:
            print(cls.INFO_MSG, *args, **kwargs)

    @classmethod
    def verbose(cls, *args, **kwargs):
        """Print verbose message. Use like the print function."""
        if cls.__cur_level >= log.level.VERBOSE:
            print(cls.VERBOSE_MSG, *args, **kwargs)

    @classmethod
    def debug(cls, *args, **kwargs):
        """Print debug message. Always printed no matter the verbose level."""
        print(cls.DEBUG_MSG, *args, **kwargs)


# Special class to handle shape in CHW mode
Shape = namedtuple("Shape", "channels height width")


def grouper(iterable, n, fillvalue=None):
    "Collect data into fixed-length chunks or blocks (from python doc)"
    # grouper('ABCDEFG', 3, 'x') --> ABC DEF Gxx
    args = [iter(iterable)] * n
    return filter(None, zip_longest(*args, fillvalue=fillvalue))


def doc_body(s):
    "From string containing subject and body separate by blank lines, returns the body."
    r = 0
    for l in s.splitlines():
        r += len(l) + 1
        if len(l.strip()) == 0:
            break
    return s[r:]


_environ = None


def _get_libc_environb_iter():
    """Get iterator on environment from C as bytestrings.
    Iterator give a byte array in the form "key=value" at each iteration
    (split on b'=' to separate).
    """
    global _environ
    if _environ is None:
        libc = ctypes.CDLL(None)
        _environ = ctypes.POINTER(ctypes.c_char_p).in_dll(libc, "environ")
    return iter(functools.partial(next, iter(_environ)), None)


def get_libc_env(key, default=None):
    """Retrieve some env variable. Return the default if not found"""
    if key in os.environ:
        return os.environ[key]
    for k, v in (x.decode().split("=", 1) for x in _get_libc_environb_iter()):
        if k == key:
            return v
    return default


def getVaiswLogFile():
    try:
        import vaisw

        return os.path.join(
            os.path.expanduser(vaisw.UserConfig.get("log.directory")),
            vaisw.UserConfig.get("log.fileName"),
        )
    except Exception:
        return os.path.realpath("vaisw_execution.log")


class Box:
    """Helper class for box"""

    def __init__(self, x, y, w, h):
        """Box data represent the upper left corner + with and height of the box"""
        self.x = float(x)
        self.y = float(y)
        self.width = float(w)
        self.height = float(h)

    @classmethod
    def from_center(cls, x, y, w, h):
        """Get box from center coordonates"""
        return cls(x - w / 2, y - h / 2, w, h)

    @classmethod
    def up_left_down_right(cls, xul, yul, xdr, ydr):
        """Get box from upper-left and lower-right coordonates"""
        return cls(xul, yul, xdr - xul, ydr - yul)

    def iou(self, o):
        """Return iou between this box and the one passed as parameter"""
        xa = max(self.x, o.x)
        ya = max(self.y, o.y)
        xb = min(self.x + self.width, o.x + o.width)
        yb = min(self.y + self.height, o.y + o.height)
        intersect = max(0.0, xb - xa) * max(0.0, yb - ya)
        return (
            0
            if intersect == 0
            else intersect / (self.width * self.height + o.width * o.height - intersect)
        )

    def clip_inside(self, w, h=None, x=0, y=0):
        """\
        Return new box with position clipped inside a rectangle.

        Multiple version of calling this functin are describe here:
        clip_inside(size) : return the box clipped in a square starting at (0,0) of (size,size) side.
        clip_inside(w, h) : return the box clipped in a rectangle starting at (0,0) of (w,h) side.
        clip_inside(w, h, x, y) : return the box clipped in a rectangle starting at (x,y) of (w,h) side.
        """
        if h is None:
            h = w

        def clip(x, l, h):
            return max(l, min(h, x))

        nx, ny = clip(self.x, x, x + w), clip(self.y, y, y + h)
        right, down = self.x + self.width, self.y + self.height
        nr, nd = clip(right, x, x + w), clip(down, y, y + h)
        return Box.up_left_down_right(nx, ny, nr, nd)

    def __repr__(self):
        return f"BOX({str(self)})"

    def __str__(self):
        return f"({self.x:.2f},{self.y:.2f}) {self.width:.2f}x{self.height:.2f}"

    def __iter__(self):
        # Make Box iterable so it can be unpacked
        return iter((self.x, self.y, self.width, self.height))

    # operators for simplification (useful for scaling)
    def __ioper(self, value, oper):
        """Template for inplace operators. Work on single or two values"""
        if isinstance(value, (int, float)):
            self.x = oper(self.x, value)
            self.y = oper(self.y, value)
            self.width = oper(self.width, value)
            self.height = oper(self.height, value)
        elif isinstance(value, (list, tuple)):
            if len(value) != 2:
                raise ValueError(
                    f"Operation only supported with two values, currently {len(value)}"
                )
            w, h = value
            self.x = oper(self.x, w)
            self.y = oper(self.y, h)
            self.width = oper(self.width, w)
            self.height = oper(self.height, h)
        else:
            raise TypeError(f"Unsupported Box operation with {type(value)} type")
        return self

    def __imul__(self, value):
        return self.__ioper(value, lambda x, y: x * y)

    def __itruediv__(self, value):
        return self.__ioper(value, lambda x, y: x / y)

    def __mul__(self, value):
        """See imul doc"""
        b = Box(*self)  # copy current
        b *= value
        return b

    def __truediv__(self, value):
        """See idiv doc"""
        b = Box(*self)  # copy current
        b /= value
        return b


class Tee:
    """
    File like object that write its input to multiple files and also to stdout if set.
    Init with the path to all target files. Can be used in a with statement.
    """

    def __init__(self, *args, **kwargs):
        """if tee_stdout is True, all written data will be also teed to stdout."""
        self.file_names = args[:]
        self._init_kwargs(**kwargs)
        self.files = []
        for f in self.file_names:
            try:
                self.files.append(open(f, self.mode))  # noqa: SIM115
            except Exception:  # noqa: PERF203
                log.warning(f"can't open file {f}")
        self.is_open = True
        self.std_files = []
        if self.tee_stdout:
            self.std_files.append(sys.stdout)

    def _init_kwargs(self, mode="w", tee_stdout=True):
        """Python 2 does not support optional arg after variable positional args.
        Do the trick by using this extra function.
        """
        self.mode = mode
        self.tee_stdout = tee_stdout

    def write(self, s):
        """Write the string s to all files."""
        if not self.is_open:
            raise OSError("Cannot write on closed tee file")
        for f in self.files:
            f.write(s)
        for f in self.std_files:
            f.write(s)

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        if self.is_open:
            for f in self.files:
                f.close()
        self.is_open = False


class ThreadPool:
    """
    Helper class to simplify the use of multiprocess pool.
    If the number of thread is one, this class will act linearly without creating any
    processes. This class will also avoid issues with pickling.
    """

    def __init__(self, nbThreads):
        if nbThreads > 1:
            self.pool = Pool(processes=nbThreads)
            self.nbThreads = nbThreads
        else:
            self.nbThreads = 1

    def map(self, func, iterable):
        if self.nbThreads > 1:
            return self.pool.map(func, iterable)
        else:
            return [func(i) for i in iterable]

    def starmap(self, func, iterable):
        if self.nbThreads > 1:
            return self.pool.starmap(func, iterable)
        else:
            return [func(*i) for i in iterable]

    def _pickle_error(self, *_args, **_kwargs):
        raise RuntimeError("This class should not be used after being pickled.")

    def __getstate__(self):
        # This class doees not support pickling but does not throw error on it.
        # Using this class after pickling throw an error.
        return {"nbThreads": self.nbThreads}

    def __setstate__(self, state):
        self.__dict__.update(state)
        self.map = self._pickle_error
        self.starmap = self._pickle_error


def raise_(ex):
    """Function to raise an exception. Can be used in lambdas and conditional expressions."""
    raise ex


class Datatype:
    """Helper to enforce value for a variable.

    Example usage :
      var = Datatype.Integer("var")
      var = 3 # ok
      var = "toto" # Error, waiting for integer
      var = "3" # ok, auto conversion to int.
    """

    @staticmethod
    def convert(name, type_):
        """Convert from hints type to corresponding Datatype."""
        origin = type_.__origin__ if hasattr(type_, "__origin__") else type_
        if origin is int:
            return Datatype.Integer(name)
        elif origin is float:
            return Datatype.Float(name)
        elif origin is str:
            return Datatype.String(name)
        elif isinstance(origin, type) and issubclass(origin, Enum):
            return Datatype.Enum(name, origin)
        elif origin is typing.Union:
            return Datatype.Union(name, [Datatype.convert(name, t) for t in type_.__args__])
        elif origin is None or origin is type(None):
            return Datatype.NoneType(name)
        elif origin is list:
            assert len(type_.__args__) == 1, "Multiple list type is not supported"
            return Datatype.List(name, Datatype.convert(name, type_.__args__[0]))
        else:
            raise NotImplementedError(f"Unsupported type : {origin}")

    class __Base:
        def __init__(self, name):
            self.name = name

        def __delete__(self, instance):
            raise RuntimeError("Cannot delete")

        def __set__(self, instance, value):
            try:
                instance.__dict__[self.name] = self.checkValue(value)
            except Exception:
                raise ValueError(f"{self.name} wait for {self.svalues()}. Got '{value}'") from None

        def svalues(self):
            """String representation of possible values"""
            return "N/A"

        def checkValue(self, value):
            raise NotImplementedError("Must be set by subclasses")

    class Typed(__Base):
        typ = None

        def svalues(self):
            return self.typ.__name__

        def checkValue(self, value):
            return self.typ(value)

    class Integer(Typed):
        typ = int

    class Float(Typed):
        typ = float

    class String(Typed):
        typ = str

    class Enum(__Base):
        def __init__(self, name, enum):
            self.name = name
            self.enum = enum

        def svalues(self):
            return "{}({})".format(self.enum.__name__, ", ".join(e.name for e in self.enum))

        def checkValue(self, value):
            if isinstance(value, str):
                return self.enum[value]
            else:
                return self.enum(value)

    class NoneType(__Base):
        def svalues(self):
            return "None"

        def checkValue(self, value):
            if value is None:
                return
            else:
                raise ValueError("not None")

    class Union(__Base):
        def __init__(self, name, subcls):
            self.name = name
            self.subcls = subcls

        def svalues(self):
            return "either {} or {}".format(
                ", ".join(s.svalues() for s in self.subcls[:-1]), self.subcls[-1].svalues()
            )

        def checkValue(self, value):
            for cls in self.subcls:
                with contextlib.suppress(Exception):
                    return cls.checkValue(value)
            else:
                raise ValueError("Nothing valid in union")

    class List(__Base):
        def __init__(self, name, intcls):
            self.name = name
            self.intcls = intcls

        def svalues(self):
            return f"List[{self.intcls.svalues()}]"

        def checkValue(self, value):
            if isinstance(value, str):
                raise ValueError("Invalid")  # handle special case as strings are iterable.
            return [self.intcls.checkValue(x) for x in value]


def parse_prog(prog):
    def _rec(it, root=(), count=0):
        for s in it:
            if s == "(":
                root += (_rec(it, count=count + 1),)
            elif s == ")":
                return root if count > 0 else raise_(SyntaxError("Mismatch parenthesis"))
            else:
                root += (s,)
        return root if count == 0 else raise_(SyntaxError("Mismatch parenthesis"))

    return _rec(iter(re.findall(r"[()]|:?[^\s()]+", prog)))


def lst2str(l):
    if isinstance(l, (list, tuple)):
        return "({})".format(" ".join([lst2str(x) for x in l]))
    else:
        return str(l)


def yaccer(prog, func_cls):
    """Get `prog` parsed by parse_prog and change it to list of classes from func_cls."""

    def parse_args(args):
        it = iter(args)
        for s in it:
            if isinstance(s, str) and s.startswith(":"):
                yield (True, s[1:], next(it, None))
            else:
                yield (False, s, None)

    funcs = []
    for p in prog:
        if isinstance(p, str):
            p = (p,)
        if not hasattr(func_cls, p[0]):
            raise ValueError(f"Unknown parsed function {p}")
        cls = getattr(func_cls, p[0])
        args = []
        kwargs = {}
        for is_key, opt, val in parse_args(p[1:]):
            if is_key:
                if val is None:
                    raise SyntaxError(f"keyword {opt} wait for a value")
                kwargs[opt] = val
            else:
                args.append(opt)
        try:
            funcs.append(cls(*args, **kwargs))
        except TypeError as e:
            raise TypeError(str(e).replace("__init__()", str(lst2str(p)))) from None
    return funcs
