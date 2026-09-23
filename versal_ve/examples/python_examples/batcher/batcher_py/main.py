#!/usr/bin/env python3
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================


import os
import sys

# Adding the upper directory into path to correctly import the batcher
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import batcher_py

if __name__ == "__main__":
    framework = None
    script_name = os.path.basename(__file__)
    if "_" in script_name:
        # get framework from script name first.
        # Script name have the form <framework>_...
        framework = script_name.split("_")[0]
    else:
        # or get it from env variable
        framework = os.environ["VAISW_FRAMEWORK"]
    if framework is None:
        raise TypeError(
            "Please call any *_batcher.py link instead of main.py "
            "or set the VAISW_FRAMEWORK env variable to specify which "
            "framework you want to run."
        )
    batcher_py.main(framework, sys.argv[1:], exec_name=script_name)
