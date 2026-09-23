# -*- coding: utf-8 -*-
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================
# flake8: noqa
# isort:skip_file

"""Preprocessing functions and classes."""

from __future__ import print_function
from __future__ import division

import sys, os
import re
from enum import Enum
import numpy as np
import random
import time
from collections import OrderedDict
from typing import Any, ClassVar, Optional, Union, Callable

if sys.version_info < (3, 9):
    from typing import List
else:
    List = list

if sys.version_info >= (3,):
    from functools import reduce

from . import Image
from .utils import log, grouper, Shape, ThreadPool
from .utils import parse_prog, yaccer, Datatype
from .Stats import Stats

ListOrVal = lambda x: Union[List[x], x]
FuncList = []  # List all pre process functions


class _PreProcessFuncs:
    """Group of pre processing functions"""

    class __StructMeta(type):
        """Every class that use this metaclass will have its __init__ generated.
        The init will take all class variable and setup the variable with utils.Datatype
        using the type hints. ClassVar variables are not changed in the __init__.
        Hints values will used as default values for the __init__ parameters.
        """

        @classmethod
        def __prepare__(cls, name, bases):
            return OrderedDict()

        def _make_init(clsdict, annot, defaults):
            if len(annot) == 0:
                return "def __init__(self):\n    pass"
            # arguments
            _make_arg = lambda x: '"{}"'.format(x) if isinstance(x, Enum) else x
            c = ["{} = {}".format(f, _make_arg(defaults[f])) if f in defaults else f for f in annot]
            code = "def __init__(self, {}):\n".format(", ".join(c))
            # code
            d = ["self.{} = {}".format(f, f) for f in annot]
            d.append("self.__subinit__()")
            code += "    {}".format("\n    ".join(d))
            return code

        def __new__(cls, name, bases, clsdict):
            if sys.version_info < (3, 7):
                isClassVar = lambda x: isinstance(x, type(ClassVar))
            else:
                isClassVar = lambda x: hasattr(x, "__origin__") and x.__origin__ is ClassVar

            # First get full hints and default values from inherit classes
            hints = OrderedDict()
            defaults = {}
            for c in bases:
                hints.update(c._args)
                defaults.update(c._defaults)
            # Update with local variables and remove ClassVar
            hints.update(clsdict.get("__annotations__", {}))
            defaults.update({a: clsdict[a] for a in hints if a in clsdict})
            for k in list(hints):
                if isClassVar(hints[k]):
                    del hints[k]
            clsdict["_defaults"] = defaults
            clsdict["_args"] = hints

            if not name.startswith("_"):
                # generate init for all classes and set values to Datatypes
                exec(cls._make_init(clsdict, hints, defaults), globals(), clsdict)
                for hname, typ in hints.items():
                    clsdict[hname] = Datatype.convert(hname, typ)
            clsobj = super().__new__(cls, name, bases, dict(clsdict))
            if not name.startswith("_"):
                # Check type hints for the preProcess method
                if not hasattr(clsobj, "preProcess"):
                    raise AttributeError(
                        "Class {} must contains a 'preProcess' function.".format(name)
                    )
                FuncList.append(clsobj)
            return clsobj

    class __Base(metaclass=__StructMeta):
        # Set to true to avoid call by image without data.
        use_data: ClassVar[bool] = False

        def __call__(self, image):
            # We handle fake image specificaly to simplify the code
            if self.use_data and isinstance(image, Image.FakeImg):
                return image
            return self.preProcess(image)

        def __subinit__(self):
            """This function is called after the generated __init__ to handle special cases"""
            pass

        def __repr__(self):
            r = self.__class__.__name__
            if len(self._args) > 0:
                r += "({})".format(
                    ", ".join("{} = {}".format(p, getattr(self, p)) for p in self._args)
                )
            return r

    class __Alias(__Base):
        """Base class to create aliases classes"""

        def preProcess(self, outputs: Any, batch) -> Any:
            # Alias will be replaced so this should not be called
            return NotImplementedError("Should not be called")

        def alias(self):
            return NotImplementedError("Aliases must implement alias function.")

    class Transpose(__Base):
        """Transpose image data following axes."""

        axes: List[int]

        def preProcess(self, image) -> Image.Image:
            return image.transpose(self.axes)

    class __Format(Transpose):
        # to be set by subclasses
        axes: ClassVar[List[int]] = None
        from_format = None
        to_format = None

        def preProcess(self, image) -> Image.Image:
            if image.format == self.from_format:
                return super().preProcess(image)
            elif image.format == self.to_format:
                return image
            else:
                raise ValueError(
                    "Unsupported transposition from {} to {}".format(image.format, self.format)
                )

    class NHWC(__Format):
        """\
        Convenience function to transpose image into NHWC format.
        Do nothing is image is already in good format."""

        axes = [1, 2, 0]
        from_format = Image.Format.NCHW
        to_format = Image.Format.NHWC

    class NCHW(__Format):
        """\
        Convenience function to transpose image into NCHW format.
        Do nothing is image is already in good format."""

        axes = [2, 0, 1]
        from_format = Image.Format.NHWC
        to_format = Image.Format.NCHW

    class __Color(__Base):
        def preProcess(self, image) -> Image.Image:
            """Change internal color position"""
            return image.convert(self.color)

    class BGR(__Color):
        """Move image data into BGR color."""

        color = Image.Color.BGR

    class RGB(__Color):
        """Move image data into RGB color."""

        color = Image.Color.RGB

    class __bin_op(__Base):
        use_data: ClassVar[bool] = True
        op: ClassVar[Callable] = None
        val: ListOrVal(float)

        def __subinit__(self):
            if isinstance(self.val, (list, tuple)):
                assert len(self.val) == 3, (
                    "As we broadcast values on channel, only 3 values are supported"
                )
                self.preProcess = self.preProcess_array
            self.op = self.__class__.op

        def preProcess_array(self, image) -> Image.Image:
            a = np.array(self.val, dtype=np.float32).reshape(
                (3, 1, 1) if image.format == Image.Format.NCHW else (1, 1, 3)
            )
            return image.update_data(self.op(image.data, a))

        def preProcess(self, image) -> Image.Image:
            return image.update_data(self.op(image.data, self.val))

    class add(__bin_op):
        """Add image data with a constant value"""

        op = lambda x, y: x + y

    class sub(__bin_op):
        """Sub image data with a constant value"""

        op = lambda x, y: x - y

    class mul(__bin_op):
        """Multiplies image data with a constant value"""

        op = lambda x, y: x * y

    class div(__bin_op):
        """Divide image data by a constant value"""

        op = lambda x, y: x / y

    class StdNorm(__Alias):
        """Normalize data (simply apply (sub mean) (div deviation))."""

        mean: ListOrVal(float)
        deviation: ListOrVal(float)

        def alias(self):
            # We accept null deviation for legacy support.
            if isinstance(self.deviation, (list, tuple)):
                is_dev_null = all([x == 0.0 for x in self.deviation])
                if not is_dev_null:
                    self.deviation = [1.0 if x == 0.0 else x for x in self.deviation]
            else:
                is_dev_null = self.deviation == 0.0

            if is_dev_null:
                return [_PreProcessFuncs.sub(self.mean)]
            else:
                return [_PreProcessFuncs.sub(self.mean), _PreProcessFuncs.div(self.deviation)]

    class __Rect(__Base):
        """Base class for all classes that change image size"""

        width: int
        height: Optional[int] = None

        def __subinit__(self):
            if self.height is None:
                self.height = self.width

    class Resize(__Rect):
        """\
        Resize image using the method setup as parameter.
        If call with only one value, apply a square resize.
        Valid methods are : {self.method_doc}"""

        method: Image.ResampleMethod = Image.ResampleMethod.Bilinear
        method_doc = ", ".join(e.name for e in Image.ResampleMethod)

        def preProcess(self, image) -> Image.Image:
            return image.resize(self.width, self.height, self.method)

    class Resize_longest(Resize):
        """\
        Resize image by setting its longest size to the parameter value and keeping aspect ratio.
        If call with only one value, apply a square resize.
        Valid methods are : {self.method_doc}"""

        def preProcess(self, image) -> Image.Image:
            _, h, w = image.shape
            width = min(w * self.width // w, w * self.height // h)
            height = min(h * self.width // w, h * self.height // h)
            return image.resize(width, height, self.method)

    class Resize_shortest(Resize):
        """\
        Resize image by setting its shortest size to the parameter value and keeping aspect ratio.
        If call with only one value, apply a square resize.
        Valid methods are : {self.method_doc}"""

        def preProcess(self, image) -> Image.Image:
            _, h, w = image.shape
            width = max(w * self.width // w, w * self.height // h)
            height = max(h * self.width // w, h * self.height // h)
            return image.resize(width, height, self.method)

    class Crop(__Rect):
        """Crop image to the size defined. Apply square crop if called with one value."""

        def preProcess(self, image) -> Image.Image:
            return image.crop(self.width, self.height)

    class Expand(__Rect):
        """\
        Expand image to the size defined. Apply square crop if called with one value.
        black bars will be filled with the "fill" parameter."""

        fill: int = 128

        def preProcess(self, image) -> Image.Image:
            return image.expand(self.width, self.height, self.fill)

    class PanScan(__Alias, __Rect):
        """Resize in PanScan format."""

        resize: int = 0
        method: Image.ResampleMethod = Image.ResampleMethod.Bilinear

        def alias(self):
            w = self.width if self.resize == 0 else self.resize
            h = self.height if self.resize == 0 else self.resize
            return [
                _PreProcessFuncs.Resize_shortest(w, h, self.method),
                _PreProcessFuncs.Crop(self.width, self.height),
            ]

    class LetterBox(__Alias, Expand):
        """Resize in letterbox format."""

        def alias(self):
            return [
                _PreProcessFuncs.Resize_longest(self.width, self.height),
                _PreProcessFuncs.Expand(self.width, self.height, self.fill),
            ]

    class __Dump(__Base):
        dumpPath: str

        def __subinit__(self):
            if not os.path.isdir(self.dumpPath):
                os.makedirs(self.dumpPath)

    class DumpImage(__Dump):
        """Dump images to the path (Should be a folder)"""

        def preProcess(self, image):
            image.dump(os.path.join(self.dumpPath, image.file))
            return image

    class DumpBinImage(__Dump):
        """Dump binary data of the image"""

        def preProcess(self, image):
            fname = os.path.splitext(image.file)[0] + ".bin"
            with open(os.path.join(self.dumpPath, fname), "w") as f:
                log.log('Saving image "{}".'.format(f.name))
                image.data.tofile(f)
            return image


class Proteus:
    """The pre-processing class."""

    def __init__(
        self,
        dir_name,
        nbImages=None,
        batch_size=20,
        nbThreads=8,
        keys=None,
        randomize=False,
        offset=0,
        repeat=1,
        preLoadImages=False,
        imgDtype=np.float32,
    ):
        """Setup the preprocessing object to handle the directory <dir_name>.
        By default no preprocessing is done, you need to set a preprocessing program
        and pass it to set_preproc to make the preprocessing.
        The class is multithreaded using the number of threads set in the parameters.
        """
        # We filter the files that we seems to support to avoid issue
        log.log("---------- Load Images ----------")
        if os.path.isdir(dir_name):
            self.dir_name = dir_name
            self.file_list = []
            dlen = len(dir_name)
            r = re.compile(r".*\.(jpe?g|bmp|png|webp)$", re.IGNORECASE)
            load_base = len(next(os.walk(dir_name))[1]) or 1  # nb of subfolder
            for i, (path, dirs, files) in enumerate(os.walk(dir_name)):
                if keys and keys.valid():
                    self.file_list += [
                        os.path.join(path[dlen + 1 :], f) for f in files if f in keys
                    ]
                else:
                    self.file_list += [
                        os.path.join(path[dlen + 1 :], f) for f in filter(r.match, files)
                    ]
                log.log("{:03}% of images loaded".format((i + 1) * 100 / load_base))
        else:
            # if the path is a file, use it directly
            self.dir_name = os.path.dirname(dir_name)
            self.file_list = [os.path.basename(dir_name)]
        log.log("Images loaded")

        # randomize file list
        if randomize:
            log.log("Randomize image set...")
            self.file_list = random.sample(self.file_list, len(self.file_list))
        else:
            self.file_list = sorted(self.file_list)

        self.file_list = self.file_list[offset:]

        # nbImages can be any value. Negative remove entries from the end of list, None take all.
        self.file_list = self.file_list[:nbImages]

        self.nbImages = len(self.file_list)
        if self.nbImages == 0:
            raise RuntimeError("No image available")
        if nbImages is not None and nbImages > self.nbImages:
            log.warning(
                "Asking for {} images but only {} are available, so repeating input images...".format(
                    nbImages, self.nbImages
                )
            )
            while len(self.file_list) < nbImages:
                self.file_list.extend(self.file_list)
            self.file_list = self.file_list[:nbImages]
            self.nbImages = len(self.file_list)

        self.batch_size = batch_size
        if batch_size > self.nbImages:
            log.warning(
                "Asking for batch of {} but only {} are available, continue anyway...".format(
                    batch_size, self.nbImages
                )
            )
            self.batch_size = self.nbImages
        elif self.nbImages % batch_size != 0:
            log.info(
                "batchSize ({}) is not a divisor of the number of images ({}).".format(
                    batch_size, self.nbImages
                )
            )

        self.repeat = repeat

        # array of preprocessing function
        self.preprocess = []
        # by default just read the image
        self.img_cls = Image.CV2Img
        self.nbBatches = (self.nbImages + self.batch_size - 1) // self.batch_size
        # Create now the list of batch of images
        self.file_list = [list(filter(None, x)) for x in grouper(self.file_list, self.batch_size)]
        log.log(
            str(len(self.file_list))
            + " batches of "
            + str(self.batch_size)
            + " images repeated "
            + str(self.repeat)
            + " times"
        )

        self.pool = ThreadPool(nbThreads)

        self.preLoadImages = preLoadImages
        self.img_data = []
        self.preloaded = False
        self.img_dtype = np.dtype(imgDtype)

    def set_preproc(self, prog):
        prog = parse_prog(prog)
        pp = yaccer(prog, _PreProcessFuncs)
        self.preprocess = []
        for p in pp:
            if hasattr(p, "alias"):
                self.preprocess.extend(p.alias())
            else:
                self.preprocess.append(p)

    def check_preproc_out(self, format=None, shape=None):
        img = Image.FakeImg()
        img = reduce(lambda o, f: f(o), self.preprocess, img)
        if format is not None:
            if img.format != format:
                if img.format != Image.Format.Unset:
                    log.warning("{} input format is enforce by the classifier...".format(format))
                self.preprocess.append(getattr(_PreProcessFuncs, format.name)())

    def set_imgReader(self, reader):
        """Set the proteus image reader.
        The image reader must be a child of the ImgReader class.
        """
        self.img_cls = reader

    def iter_repeat(self, l, count=1):
        while count > 0:
            for e in l:
                yield e
            count = count - 1

    def __iter__(self):
        "Allow to loop through all batches. Each iteration return a Batch object."
        if self.preLoadImages and not self.preloaded:
            log.log("Preloading all images...")
            preloadStart = time.time()
            self.preload()
            preloadEnd = time.time()
            log.log(
                f"Pre-loaded images in {preloadEnd - preloadStart} s ({int(self.nbImages / self.repeat / (preloadEnd - preloadStart))} FPS)",
                flush=True,
            )

        self._iterFiles = self.iter_repeat(
            self.img_data if self.preloaded else self.file_list, self.repeat
        )
        return self

    def next(self):  # python2
        return self.__next__()

    def __next__(self):  # python3
        n = next(self._iterFiles)
        return n if self.preloaded else self._read_img_batch(n)

    def _pp_img(self, path):
        """Read image from path with full preprocess"""
        return reduce(
            lambda o, f: f(o), self.preprocess, self.img_cls.read_img(path, self.img_dtype)
        )

    def _read_img_batch(self, images):
        """Read a batch of images. Typically call the read_img function for each image.
        Returns a Batch object.
        """
        with Stats.Timer(lambda x: log.info("PreProcess time -> {} s".format(x))):
            r = self.pool.map(self._pp_img, [os.path.join(self.dir_name, f) for f in images])
            return Image.Batch(r, self.batch_size)

    def preload(self):
        """Preloading images before inference to save some execution time"""
        try:
            from tqdm import tqdm
        except ImportError:

            def tqdm(flist, **kwargs):
                print("Preloading images...")
                for e in flist:
                    yield e

        for batch in tqdm(self.file_list, bar_format="{l_bar}{bar}"):
            self.img_data.append(self._read_img_batch(batch))
        self.preloaded = True
