# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

"""Consul of rome. Defines language..."""

import argparse
import inspect
import os
import re
import sys
import textwrap
from enum import Enum

from . import Image
from . import utils
from .colors import ColorString
from .colors import TermColors
from .GoldManager import Gold
from .PostProcess import FuncList as PostProcessFuncs
from .Proteus import FuncList as PreProcessFuncs


class ArgumentReader:
    """Class to handle command line options."""

    class RunMode(Enum):
        DEFAULT = "default"
        GOLD = "Gold"
        # CREATE_GOLD = "CreateGold"
        DEMO_BIN = "Demo"
        NONE = "None"

        def __str__(self):
            return self.value

    @staticmethod
    def _FileorFolder(path):
        """Check that the path lead to a file or a folder"""
        if not os.path.isfile(path) and not os.path.isdir(path):
            raise argparse.ArgumentTypeError(f"'{path}' is not a file or a folder")
        return path

    class __CustomHelpFormatter(argparse.HelpFormatter):
        def _get_help_string(self, action):
            if isinstance(action.help, str):
                return action.help
            else:
                return "\n\n".join([textwrap.dedent(s) for s in action.help])

        def _split_lines(self, text, _width):
            return textwrap.dedent(text).splitlines()

    def _GenHelp(self, funcs):
        """Generate help for function processing options."""
        ghelp = []
        for f in funcs:
            spec = inspect.getfullargspec(f.__init__)
            func = f.__name__
            if len(spec.args) > 1:
                func = "({} {}{}{})".format(
                    func,
                    TermColors.Italic,
                    " ".join(a for a in spec.args if a != "self"),
                    TermColors.Color_Off,
                )
            ghelp.append(
                "{} : {}{}".format(
                    func,
                    "\n" if f.__doc__.count("\n") > 0 else "",
                    f.__doc__.format(self=f, spec=spec),
                )
            )
        return ghelp

    def __init__(self, argv, name="batcher.py"):
        parser = argparse.ArgumentParser(
            prog=name, formatter_class=self.__CustomHelpFormatter, add_help=False
        )

        required_group = parser.add_argument_group(
            ColorString("Required arguments", TermColors.Bold)
        )
        required_group.add_argument(
            "--modelPath",
            default=None,
            required=True,
            help="""\
                                    Path to model folder.
                                    The folder must contains the files labels, network and trained_model.
                                    """,
        )

        option_group = parser.add_argument_group(ColorString("Optional arguments", TermColors.Bold))
        option_group.add_argument(
            "-h", "--help", action="help", help="show this help message and exit"
        )
        option_group.add_argument(
            "-v",
            "--verbose",
            action="count",
            default=3,
            help="Increase verbosity (repeat for more increase). q and v options can be mixed.",
        )
        option_group.add_argument(
            "-q",
            "--quiet",
            action="count",
            default=0,
            help="Decrease verbosity (repeat for more decrease). q and v options can be mixed.",
        )
        option_group.add_argument(
            "--mode",
            default=self.RunMode.DEFAULT,
            choices=list(self.RunMode),
            type=self.RunMode,
            help="""\
                                  Define the mode in which the batcher will run.
                                  The definition of modes are the following :
                                  - {mode.DEFAULT:<{align}} : Simply print the result of the network.
                                  - {mode.NONE:<{align}} : Just run the network without processing the output.
                                  - {mode.GOLD:<{align}} : The gold mode.
                                    Compare the result of the prediction with a reference (gold) file.
                                    See the --goldFile option to specify the file.
                                  - {mode.DEMO_BIN:<{align}} : The demo mode.
                                    Run for a specific time and create the json files that can be shown
                                    in a live demo. See the Demo options below for more details on how to
                                    control it.
                                  """.format(mode=self.RunMode, align=7),
        )
        option_group.add_argument(
            "--imgPath",
            default=None,
            help="""\
                                  Path to folder containing test images.
                                  Can be a path to one file to test this specific file.""",
        )
        option_group.add_argument(
            "--nbImages",
            default=1000,
            type=lambda x: None if x.lower() == "all" else int(x),
            help="""\
                                  Number of image to be processed.
                                  Should be an integer or "all" for all images in the imgPath.
                                  Can be a negative value to remove images from the full list.
                                  """,
        )
        option_group.add_argument(
            "--imgOffset", type=int, default=0, help="number of images to skip at the beginning."
        )
        option_group.add_argument(
            "--batchSize", type=int, default=20, help="Size of batch to be processed at each time"
        )

        option_group.add_argument(
            "--outputNames",
            type=str,
            default=None,
            nargs="+",
            metavar="LAYER",
            help="""\
                                  Names of the output layers of the network.
                                  If not set they will be chosen automatically.""",
        )
        option_group.add_argument(
            "--predictFile",
            type=argparse.FileType("w"),
            default=sys.stdout,
            help="Write prediction into this file instead of stdout",
        )
        option_group.add_argument(
            "--statsFile",
            type=argparse.FileType("w"),
            default=None,
            help="output some statistics into this file if set.",
        )
        option_group.add_argument(
            "--predictMode",
            type=str,
            default="TOP_N",
            help="To be change when we have better way to setup postprocessing",
        )
        option_group.add_argument(
            "--training", action="store_true", help="Use when we want to train the model."
        )
        option_group.add_argument(
            "--labels",
            type=str,
            default=None,
            help="The labels file path. Can be relative to the modelPath or absolute.",
        )
        option_group.add_argument(
            "--repeat", type=int, default=1, help="Number of times input images are repeated"
        )
        option_group.add_argument(
            "--preLoadImages", action="store_true", help="Pre-loading images before inference"
        )
        option_group.add_argument(
            "--imgDtype", type=str, default="float32", help="Data type of the input images"
        )

        # New option for network version (for now, only used for PyTorch)
        option_group.add_argument(
            "--networkVersion",
            type=str,
            default=None,
            help="Network version. If not specified, will use `network`",
        )

        preprocess_group = parser.add_argument_group(
            ColorString("Pre Process options", TermColors.Bold)
        )
        preprocess_group.add_argument(
            "--imageShape",
            nargs=3,
            type=int,
            default=None,
            metavar=("CHANNELS", "HEIGHT", "WIDTH"),
            help="Resize input image to this shape in dim CxHxW",
        )
        img_readers = {"PIL": Image.PILImg, "OpenCV": Image.CV2Img}
        preprocess_group.add_argument(
            "--imgReader",
            choices=img_readers,
            default="OpenCV",
            help="Choose the image reader framework.",
        )
        preprocess_group.add_argument(
            "--ppThreads",
            type=int,
            default=1,
            help="Set the number of threads for the preprocessing",
        )
        preprocess_group.add_argument(
            "--randomize",
            action="store_true",
            help="""\
                                      Randomize the input images order.
                                      This means that the input images may not be the same every time. The
                                      randomization is done on the full image set from the `imgPath' option
                                      and the `nbImages' limitation is done afterward.
                                      """,
        )
        preprocess_group.add_argument(
            "--preProcess",
            nargs="+",
            default=[],
            dest="preProcessingProg",
            help=textwrap.dedent("""\
                                      Pre process chain. Describe the chain of functions that will be applied
                                      by the pre processing. Each function can have parameter when written
                                      in list form ("(func param param...)").
                                      The possible functions are :
                                        {}""").format("\n  ".join(self._GenHelp(PreProcessFuncs))),
        )

        postprocess_group = parser.add_argument_group(
            ColorString("Post Process options", TermColors.Bold)
        )
        postprocess_group.add_argument(
            "--postProcess",
            nargs="+",
            default=["TOP_N"],
            dest="postProcessingProg",
            help=textwrap.dedent("""\
                                       Post process chain. Describe the chain of functions that will be applied
                                       by the post processing. Each function can have parameter when written
                                       in list form ("(func param param...)").
                                       The possible functions are :
                                         {}""").format(
                "\n  ".join(self._GenHelp(PostProcessFuncs))
            ),
        )

        gold_group = parser.add_argument_group(
            "{} (valid for {} and {} modes)".format(
                ColorString("Gold mode options", TermColors.Bold),
                self.RunMode.GOLD,
                self.RunMode.DEMO_BIN,
            )
        )
        gold_group.add_argument(
            "--goldFile",
            default=None,
            type=self._FileorFolder,
            help="The gold file path. Can be a folder when using some framework.",
        )
        gold_group.add_argument("--testList", type=str, help="List of test to run")
        gold_group.add_argument(
            "--gform",
            choices=list(Gold.Form),
            default=Gold.Form.PESSIMISTIC,
            type=lambda x: Gold.Form[x],
            help="Set the form of the gold comparison.",
        )
        gold_group.add_argument(
            "--displayImages",
            action="store_true",
            default=False,
            help="Display input and output images.",
        )

        demo_group = parser.add_argument_group(
            "{} (valid for {} mode only)".format(
                ColorString("Demo mode options", TermColors.Bold), self.RunMode.DEMO_BIN
            )
        )
        demo_group.add_argument(
            "--demoPath", type=str, help="Set the path to the folder where demo files will be dump."
        )
        demo_group.add_argument(
            "--demoTime", type=int, default=60, help="Duration of the demo (in seconds)."
        )

        snapshot_group = parser.add_argument_group("Snapshot batcher arguments")
        snapshot_group.add_argument(
            "--embedded", action="store_true", help="Enable embedded framework."
        )
        snapshot_group.add_argument(
            "--snapshot",
            type=str,
            default="",
            help="""Path to the snapshot. If not provided, vaisw will use
                                          snapshot.directory from vaisw.ini.""",
        )

        # old options kept for backward compatibility
        option_group.add_argument(
            "--execMode", type=str, default="FPGA", choices=["FPGA", "CPU"], help=argparse.SUPPRESS
        )

        parser.parse_args(argv, namespace=self)

        # extra setup based on user options
        self.postProcessingProg = " ".join(self.postProcessingProg)
        self.preProcessingProg = " ".join(self.preProcessingProg)
        self.displayResults = (
            self.mode == self.RunMode.DEFAULT or self.predictFile != sys.stdout
        ) and self.mode != self.RunMode.NONE
        self.modelPathsList = re.split("[+=]", self.modelPath)
        self.snapshotPathsList = re.split("[+:]", self.snapshot)
        if self.imageShape is not None:
            self.imageShape = utils.Shape._make(self.imageShape)
        self.verbose = self.verbose - self.quiet
        self.verbose = max(0, min(self.verbose, len(utils.log.level) - 1))
        utils.log.set_verbose(utils.log.level[self.verbose])
        self.imgReader = img_readers[self.imgReader]

    def __repr__(self):
        r = "Cicero("
        for i, k in enumerate(sorted(self.__dict__)):
            if i > 0:
                r += ", "
            r += f"{k}={self.__dict__[k]!r}"
        return r + ")"
