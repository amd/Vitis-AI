# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

# If available in the python path, this file should be loaded at the start of
# the python executable.

import os

if os.environ.get("VAISW_DISABLE_ROCM_BENCHMARK", "").lower() in {"1", "true", "yes", "on"}:
    import rocm_no_benchmark  # noqa: F401

import vaisw_wrapper  # noqa: F401

# py_filter: /unused_imports
