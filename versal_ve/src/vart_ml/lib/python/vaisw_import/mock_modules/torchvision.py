#!/usr/bin/env python3

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from .wrappers import ModuleWrapper
from .wrappers import NetworkWrapper
from .wrappers import disable_wrap


class TorchvisionComposeWrapper(NetworkWrapper):
    """Protect tranformation from the wrapper"""

    def __call__(self, *args, **kwargs):
        with disable_wrap():
            return super().__call__(*args, **kwargs)


class TorchvisionTransformsWrapper(ModuleWrapper):
    """Wrapper for torchvision.transforms module"""

    # The tranforms module only contains function to apply tranformations
    # into images. Thus we may need to protect all the internals but it is
    # fastidous and the Compose is a good start.
    Compose = TorchvisionComposeWrapper
