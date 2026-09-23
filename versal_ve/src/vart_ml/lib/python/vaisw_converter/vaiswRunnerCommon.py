# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

from collections.abc import Sequence

import numpy as np
from vaisw_import import VaiswStub

UserConfig = VaiswStub.vaisw.UserConfig

DEFAULT_NETWORK_NAME = "wrp_network"


class VaiswCommon:
    def __init__(self, networkName: str = None):
        self._init = False
        if networkName is not None:
            self.networkName = networkName
        elif UserConfig.isDefaultValue("runSession.networkName"):
            self.networkName = DEFAULT_NETWORK_NAME
        else:
            self.networkName = UserConfig.get("runSession.networkName")

        self._outputs = []
        self._inputs = []

    def convert_float32_or_uint8(self, value):
        if isinstance(value, Sequence):
            if isinstance(value[0], np.ndarray) and value[0].dtype != np.float32:
                if value[0].dtype in [np.uint8, np.int8, np.int32, np.int64]:
                    return np.asarray(value, dtype=value[0].dtype)
                return np.asarray(value, dtype=np.float32)
            else:
                return np.asarray(value)
        elif isinstance(value, np.ndarray) and value.dtype != np.float32:
            if value.dtype in [np.uint8, np.int8, np.int32, np.int64]:
                return value.astype(value.dtype)
            return value.astype(np.float32)
        else:
            return value

    def force_float32(self, vec):
        return [np.asarray(i, dtype=np.float32) for i in vec]
