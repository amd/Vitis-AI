# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

"""Image abstraction module"""

import copy
import os
from enum import Enum

import numpy as np

from .utils import Box
from .utils import Shape


class Format(Enum):
    """Enumeration of the data format desciption for image or batch."""

    Unset = -1  # Special case where format could be known but is not enforce
    Unknown = 0
    NCHW = 1
    NHWC = 2

    def __str__(self):
        return self.name


class Color(Enum):
    """Enumeration of the channel order for image or batch."""

    Unset = -1  # Special case where color could be known but is not enforce
    Unknown = 0
    RGB = 1
    BGR = 2

    def __str__(self):
        return self.name


class ResampleMethod(Enum):
    """Enumeration of supported resample method for image resize."""

    Bilinear = 0
    Bicubic = 1

    def __str__(self):
        return self.name


class Batch:
    """Class to handle batch of images"""

    def __init__(self, images, batch_size=0):
        """Initialze the batch.
        images (list of Image object): The list of images to put in the batch.
                                       See the Image object for detail on it.
        batch_size (int): The waited batch size. Can be greater than the real number of file
                          in the batch.
        """
        self.images = images
        self.batch_size = batch_size

    def __getitem__(self, i):
        # list like access to get an image
        return self.images[i]

    def __len__(self):
        """Gives the number of file in this batch"""
        return len(self.images)

    @property
    def data(self):
        """Get the batch data as an numpy array. It will be recalculate at each access."""
        return np.asarray([im.data for im in self.images])

    def align_data(self, batch_size=None):
        """Get the data but fill the result array with 0 to follow the batch_size parameter.
        By default take the batch_size from the class.
        """
        if batch_size is None:
            batch_size = self.batch_size
        out = self.data
        if out.shape[0] > batch_size:
            raise RuntimeError(
                f"Ask for data with batch_size={batch_size} but current batch is {out.shape[0]}."
            )
        if out.shape[0] < batch_size:
            out = np.pad(out, [(0, batch_size - out.shape[0])] + [(0, 0)] * (len(out.shape) - 1))
        return out

    @property
    def files(self):
        """Simplification property that returns the list of files name"""
        return [im.file for im in self.images]

    @property
    def waited_size(self):
        """Get the waited batch size. Apart for specific case, it should be equal to len()"""
        return self.batch_size


def _or(a, b):
    return a if a is not None else b


class Image:
    """Image abstraction class. Not intended to be created directly."""

    def __init__(self, data, path, cls, shape, color, format, dtype=np.float32, ref=None, roi=None):
        # properties
        self._img = data
        self._ref: Image = ref if ref is not None else self
        self._cls = cls  # image manipulation class

        self.path: str = path
        _, self.file = os.path.split(path)
        self.shape: Shape = shape
        self.format: Format = format
        self.color: Color = color
        self.dtype = dtype

        # Region Of Interest of the image. It components must be normalized.
        # TODO: Check to use pixel values instead of normalized one.
        #       Remove offset and scale once this value is correctly used.
        self.roi = _or(roi, Box(0, 0, 1, 1))

        # Values to keep the current offset and scale of the image.
        # These values are updated if we use the letterbox resizing.
        # Values are in percentage and in the form w, h.
        self.offset = (self.roi.x, self.roi.y)
        self.scale = (self.roi.width, self.roi.height)

    def update_data(self, data):
        """Public helper to create new image based on this one with new data."""
        return self._update(data)

    def _update(self, data, shape=None, color=None, format=None, roi=None):
        """Helper to create new image based on this one.
        Return the new one, the current image is not modified."""
        img = copy.copy(self)  # Use copy to maintain the Image subclass if any
        img._img = data  # noqa: SLF001
        img.shape = _or(shape, img.shape)
        img.color = _or(color, self.color)
        img.format = _or(format, self.format)
        img.roi = self.compose_roi(self.roi, roi)
        img.offset = (img.roi.x, img.roi.y)
        img.scale = (img.roi.width, img.roi.height)
        return img

    @staticmethod
    def compose_roi(first: Box, second: Box):
        if second is None:
            return first
        elif first is None:
            return second
        else:
            return Box(
                first.x + second.x,
                first.y + second.y,
                first.width * second.width,
                first.height * second.height,
            )

    @property
    def orig_img(self):
        """Return ref to the original image (without any modification)"""
        return self._ref

    @property
    def data(self):
        """Returned data as float32"""
        return self.tdata(self.dtype)

    def tdata(self, dtype):
        """Returned data as type passed as parameter"""
        return self._cls.get_data(self._img, dtype)

    def astype(self, dtype):
        """Update internal data type (may be useful for certain operations)"""
        return self._update(self._cls.get_data(self._img, dtype))

    def transpose(self, axes):
        """Transpose image data following axes."""
        new_format = Format.Unknown
        if self.format is Format.NCHW and axes == [1, 2, 0]:
            new_format = Format.NHWC
        elif self.format is Format.NHWC and axes == [2, 0, 1]:
            new_format = Format.NCHW
        return self._update(np.transpose(self.data, axes), format=new_format)

    def convert(self, color: Color):
        """Change internal color position.
        Do nothing if already on correct color format."""
        if color == self.color:
            return self  # do nothing

        # color change depend on the format.
        if self.format not in [Format.NHWC, Format.NCHW]:
            raise ValueError(f"Unsupported format to convert colors {self.format}")
        # We only have BGR and RGB for now so we simply switch axes.
        return self._update(
            self.data.take([2, 1, 0], axis=self.format.name.index("C") - 1), color=color
        )

    def resize(self, width, height, method=ResampleMethod.Bilinear):
        """Resize the image, modify the data of the object in place.
        The interpolation parameter set the resampling filter to use, default as bilinear.
        """
        _, h, w = self.shape
        width = w if width == -1 else width
        height = h if height == -1 else height
        if method in ResampleMethod:
            img = self._cls.resize(self._img, width, height, method)
        else:
            raise ValueError(f"Unsupported method {method}")
        return self._update(img, shape=Shape(self.shape.channels, height, width))

    def crop(self, width, height):
        _, h, w = self.shape
        width = w if width == -1 else width
        height = h if height == -1 else height
        if width > w or height > h:
            raise RuntimeError("trying to crop a too small image")
        img = self._cls.crop(
            self._img,
            (w - width) // 2,
            (h - height) // 2,
            (w - width) // 2 + width,
            (h - height) // 2 + height,
        )
        # TODO: roi
        box = Box(
            float(width - w) / 2 / width,
            float(height - h) / 2 / height,
            float(width) / w,
            float(height) / h,
        )

        return self._update(img, shape=Shape(self.shape.channels, height, width), roi=box)

    def expand(self, width, height, fill=128):
        """Return the image expanded until the target size.
        w and h must be upper or equal to current size.
        """
        _, h, w = self.shape
        width = w if width == -1 else width
        height = h if height == -1 else height
        if width < w or height < h:
            raise RuntimeError("trying to expand a too large image")
        img = self._cls.expand_center(self._img, width, height, fill=fill)
        box = Box(
            float(width - w) / 2 / width,
            float(height - h) / 2 / height,
            float(width) / w,
            float(height) / h,
        )
        return self._update(img, shape=Shape(self.shape.channels, height, width), roi=box)

    def dump(self, path):
        """Write image into path"""
        self._cls.dump(self._img, path)
        return self


class FakeImg(Image):
    """Class to handle real fake image (no data).
    Fake image object can be used as imageto setup pre processing.
    """

    def __init__(self, data=None):
        super().__init__(data, "fake", None, Shape(3, -1, -1), Color.Unknown, Format.Unset)

    def tdata(self, _dtype):
        return None

    def transpose(self, axes):
        new_format = Format.Unknown
        if self.format is Format.NCHW and axes == [1, 2, 0]:
            new_format = Format.NHWC
        elif self.format is Format.NHWC and axes == [2, 0, 1]:
            new_format = Format.NCHW
        return self._update(None, format=new_format)

    def convert(self, color: Color):
        return self._update(None, color=color)

    def resize(self, width, height, _method):
        return self._update(None, shape=Shape(self.shape.channels, height, width))

    def crop(self, width, height):
        return self._update(None, shape=Shape(self.shape.channels, height, width))

    def expand(self, width, height, _fill):
        return self._update(None, shape=Shape(self.shape.channels, height, width))


class DataImg(Image):
    """Fake image from data passed as parameter, must be in HWC format.
    Helper to simplify the usage of Image with the Batch class.
    """

    def __init__(self, name, data, shape=None, color=Color.BGR, format=Format.NHWC):
        if shape is None:
            shape = data.shape
        if format == Format.NHWC:
            h, w, c = shape
            _shape = Shape(c, h, w)
        elif format == Format.NCHW:
            _shape = Shape(*shape)
        else:
            raise ValueError(f"Invalid format : {format}")
        super().__init__(np.array(data).reshape(shape), name, None, _shape, color, format)

    def tdata(self, dtype):
        return self._img.astype(dtype)


class PILImg:
    """Image class for PIL images."""

    # We import modules internally to lazy load the module so the usage of Proteus
    # does not failed by the lack of unneeded dependencies.
    # Modules are not pickable so we import it on each function instead of using a variable.
    def __init__(self, _img_path):
        raise RuntimeError("Not anymore")

    def read_img(img_path):
        from PIL import Image as PILImage

        img = PILImage.open(img_path).convert("RGB")
        w, h = img.size
        shape = Shape(3, h, w)
        return Image(img, img_path, PILImg, shape, Color.RGB, Format.NHWC)

    def dump(img, path):
        img.save(path)

    def resize(img, w, h, method):
        from PIL import Image as PILImage

        resample = {
            ResampleMethod.Bilinear: PILImage.BILINEAR,
            ResampleMethod.Bicubic: PILImage.BICUBIC,
        }
        return img.resize((w, h), resample[method])

    def crop(img, left, upper, right, lower):
        return img.crop((left, upper, right, lower))

    def expand_center(img, w, h, fill):
        from PIL import ImageOps as PILImageOps

        cw, ch = img.size
        left = (w - cw) // 2
        up = (h - ch) // 2
        right = w - cw - left
        down = h - ch - up
        return PILImageOps.expand(img, (left, up, right, down), fill)

    def get_data(img, dtype=np.float32):
        return np.array(img, dtype=dtype)


class CV2Img:
    """Image class for openCV images."""

    # We import modules internally to lazy load the module so the usage of Proteus
    # does not failed by the lack of unneeded dependencies.
    # Modules are not pickable so we import it on each function instead of using a variable.
    def __init__(self, _img_path):
        raise RuntimeError("Not anymore")

    def read_img(img_path, dtype=np.float32):
        import cv2

        # From opencv documentation : setting imread flag >0 force the output to be on 3 channels
        img = cv2.imread(img_path, 1)
        if img is None:
            raise OSError("Cannot read image from '" + img_path + "'")
        h, w, c = img.shape
        shape = Shape(c, h, w)
        return Image(img, img_path, CV2Img, shape, Color.BGR, Format.NHWC, dtype=dtype)

    def dump(img, path):
        import cv2

        cv2.imwrite(path, img)

    def resize(img, w, h, method):
        import cv2

        interpolations = {
            ResampleMethod.Bilinear: cv2.INTER_LINEAR,
            ResampleMethod.Bicubic: cv2.INTER_CUBIC,
        }
        return cv2.resize(img, (w, h), interpolation=interpolations[method])

    def crop(img, left, upper, right, lower):
        return img[upper:lower, left:right]

    def expand_center(img, w, h, fill):
        ch, cw = img.shape[:2]
        boxed_image = np.empty((h, w, 3))
        boxed_image.fill(fill)
        boxed_image[(h - ch) // 2 : (h - ch) // 2 + ch, (w - cw) // 2 : (w - cw) // 2 + cw] = img
        return boxed_image

    def get_data(img, dtype=np.float32):
        return img.astype(dtype)
