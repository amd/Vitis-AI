# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

import os
import sys

from vaisw_import import CatchCheckError
from vaisw_import import VaiswImportModule

try:
    if int(os.environ.get("VAISW_ENABLE_PYWRAPPER", 1)):
        sys.meta_path.insert(0, VaiswImportModule())
except CatchCheckError as e:
    raise RuntimeError(f"vaisw_wrapper must imported before importing {e.module!r}") from None


def main():
    import argparse
    import runpy
    from pathlib import Path

    wrapper = Path(sys.argv[0])
    parser = argparse.ArgumentParser(
        description="vaisw wrapper for python.",
        prog=f"{os.path.basename(sys.executable)} -m {wrapper.stem}",
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("-c", metavar="cmd", help="program passed as a single string")
    group.add_argument(
        "py_script",
        type=str,
        metavar="script.py [args]...",
        nargs="?",
        help="The script to launch through the wrapper with its arguments.",
    )
    parser.add_argument("args", nargs=argparse.REMAINDER, metavar="ARG", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.c:
        exec(args.c)
    else:
        sys.argv[:] = args.args
        sys.argv.insert(0, args.py_script)
        runpy.run_path(args.py_script, run_name="__main__")  # run script as main


if __name__ == "__main__":
    main()
