# Copyright(C) 2023-2026 Advanced Micro Devices Inc. All Rights Reserved.
from typing import Optional
from typing import Optional
# flake8: noqa


class Ansi:
    EscSeq = "\033["
    reColor = r"(?:\033\[\d*(?:;\d+)*m)"

    # Composing
    Regular = "0"
    Bold = "1"
    Italic = "3"
    Underline = "4"
    Strikethrouh = "9"

    # Colors
    _Black = 0
    _Red = 1
    _Green = 2
    _Yellow = 3
    _Blue = 4
    _Magenta = 5
    _Cyan = 6
    _White = 7

    def _foreground(color):
        return str(30 + color)

    def _background(color):
        return str(40 + color)

    # Foreground
    FBlack = _foreground(_Black)
    FRed = _foreground(_Red)
    FGreen = _foreground(_Green)
    FYellow = _foreground(_Yellow)
    FBlue = _foreground(_Blue)
    FMagenta = _foreground(_Magenta)
    FCyan = _foreground(_Cyan)
    FWhite = _foreground(_White)

    # Background
    BBlack = _background(_Black)
    BRed = _background(_Red)
    BGreen = _background(_Green)
    BYellow = _background(_Yellow)
    BBlue = _background(_Blue)
    BMagenta = _background(_Magenta)
    BCyan = _background(_Cyan)
    BWhite = _background(_White)

    def __init__(self, *args):
        """Make ansi escape sequence for composing"""
        self.s = "{}{}m".format(self.EscSeq, ";".join(args))

    def __str__(self):
        return self.s

    def __format__(self, fmt):
        return f"{self.s:{fmt}}"


class TermColors:
    # Reset
    Color_Off = Ansi(Ansi.Regular)  # Text Reset
    Bold = Ansi(Ansi.Bold)
    Italic = Ansi(Ansi.Italic)
    Underline = Ansi(Ansi.Underline)

    # Regular Colors
    Black = Ansi(Ansi.Regular, Ansi.FBlack)  # Black
    Red = Ansi(Ansi.Regular, Ansi.FRed)  # Red
    Green = Ansi(Ansi.Regular, Ansi.FGreen)  # Green
    Yellow = Ansi(Ansi.Regular, Ansi.FYellow)  # Yellow
    Blue = Ansi(Ansi.Regular, Ansi.FBlue)  # Blue
    Purple = Ansi(Ansi.Regular, Ansi.FMagenta)  # Purple
    Cyan = Ansi(Ansi.Regular, Ansi.FCyan)  # Cyan
    White = Ansi(Ansi.Regular, Ansi.FWhite)  # White

    # Bold
    BBlack = Ansi(Ansi.Bold, Ansi.FBlack)  # Black
    BRed = Ansi(Ansi.Bold, Ansi.FRed)  # Red
    BGreen = Ansi(Ansi.Bold, Ansi.FGreen)  # Green
    BYellow = Ansi(Ansi.Bold, Ansi.FYellow)  # Yellow
    BBlue = Ansi(Ansi.Bold, Ansi.FBlue)  # Blue
    BPurple = Ansi(Ansi.Bold, Ansi.FMagenta)  # Purple
    BCyan = Ansi(Ansi.Bold, Ansi.FCyan)  # Cyan
    BWhite = Ansi(Ansi.Bold, Ansi.FWhite)  # White

    # Underline
    UBlack = Ansi(Ansi.Underline, Ansi.FBlack)  # Black
    URed = Ansi(Ansi.Underline, Ansi.FRed)  # Red
    UGreen = Ansi(Ansi.Underline, Ansi.FGreen)  # Green
    UYellow = Ansi(Ansi.Underline, Ansi.FYellow)  # Yellow
    UBlue = Ansi(Ansi.Underline, Ansi.FBlue)  # Blue
    UPurple = Ansi(Ansi.Underline, Ansi.FMagenta)  # Purple
    UCyan = Ansi(Ansi.Underline, Ansi.FCyan)  # Cyan
    UWhite = Ansi(Ansi.Underline, Ansi.FWhite)  # White

    # Background
    On_Black = Ansi(Ansi.BBlack)  # Black
    On_Red = Ansi(Ansi.BRed)  # Red
    On_Green = Ansi(Ansi.BGreen)  # Green
    On_Yellow = Ansi(Ansi.BYellow)  # Yellow
    On_Blue = Ansi(Ansi.BBlue)  # Blue
    On_Purple = Ansi(Ansi.BMagenta)  # Purple
    On_Cyan = Ansi(Ansi.BCyan)  # Cyan
    On_White = Ansi(Ansi.BWhite)  # White

    # High Intensity
    def _high_intensity(color):
        return str(60 + int(color))

    IBlack = Ansi(Ansi.Regular, _high_intensity(Ansi.FBlack))  # Black
    IRed = Ansi(Ansi.Regular, _high_intensity(Ansi.FRed))  # Red
    IGreen = Ansi(Ansi.Regular, _high_intensity(Ansi.FGreen))  # Green
    IYellow = Ansi(Ansi.Regular, _high_intensity(Ansi.FGreen))  # Yellow
    IBlue = Ansi(Ansi.Regular, _high_intensity(Ansi.FGreen))  # Blue
    IPurple = Ansi(Ansi.Regular, _high_intensity(Ansi.FGreen))  # Purple
    ICyan = Ansi(Ansi.Regular, _high_intensity(Ansi.FGreen))  # Cyan
    IWhite = Ansi(Ansi.Regular, _high_intensity(Ansi.FGreen))  # White

    # Bold High Intensity
    BIBlack = Ansi(Ansi.Bold, _high_intensity(Ansi.FBlack))  # Black
    BIRed = Ansi(Ansi.Bold, _high_intensity(Ansi.FRed))  # Red
    BIGreen = Ansi(Ansi.Bold, _high_intensity(Ansi.FGreen))  # Green
    BIYellow = Ansi(Ansi.Bold, _high_intensity(Ansi.FGreen))  # Yellow
    BIBlue = Ansi(Ansi.Bold, _high_intensity(Ansi.FGreen))  # Blue
    BIPurple = Ansi(Ansi.Bold, _high_intensity(Ansi.FGreen))  # Purple
    BICyan = Ansi(Ansi.Bold, _high_intensity(Ansi.FGreen))  # Cyan
    BIWhite = Ansi(Ansi.Bold, _high_intensity(Ansi.FGreen))  # White

    # High Intensity backgrounds
    On_IBlack = Ansi(Ansi.Regular, _high_intensity(Ansi.BBlack))  # Black
    On_IRed = Ansi(Ansi.Regular, _high_intensity(Ansi.BRed))  # Red
    On_IGreen = Ansi(Ansi.Regular, _high_intensity(Ansi.BGreen))  # Green
    On_IYellow = Ansi(Ansi.Regular, _high_intensity(Ansi.BGreen))  # Yellow
    On_IBlue = Ansi(Ansi.Regular, _high_intensity(Ansi.BGreen))  # Blue
    On_IPurple = Ansi(Ansi.Regular, _high_intensity(Ansi.BGreen))  # Purple
    On_ICyan = Ansi(Ansi.Regular, _high_intensity(Ansi.BGreen))  # Cyan
    On_IWhite = Ansi(Ansi.Regular, _high_intensity(Ansi.BGreen))  # White

    @classmethod
    def switch_to_c256_on_black(cls):
        cls.Red = Ansi(Ansi.Regular, f"38;2;255;46;46")
        cls.Green = Ansi(Ansi.Regular, f"38;2;96;225;74")
        cls.Yellow = Ansi(Ansi.Regular, f"38;2;233;216;71")
        cls.Blue = Ansi(Ansi.Regular, f"38;2;162;166;252")
        cls.Purple = Ansi(Ansi.Regular, f"38;2;247;164;221")
        cls.Cyan = Ansi(Ansi.Regular, f"38;2;157;240;248")

        cls.BRed = Ansi(Ansi.Bold, f"38;2;255;46;46")
        cls.BGreen = Ansi(Ansi.Bold, f"38;2;96;225;74")
        cls.BYellow = Ansi(Ansi.Bold, f"38;2;233;216;71")
        cls.BBlue = Ansi(Ansi.Bold, f"38;2;162;166;252")
        cls.BPurple = Ansi(Ansi.Bold, f"38;2;247;164;221")
        cls.BCyan = Ansi(Ansi.Bold, f"38;2;157;240;248")

    @staticmethod
    def c256(i):
        return Ansi(Ansi.Regular, f"38;5;{i}")


class bcolors:
    OKPURPLE = TermColors.BPurple
    OKBLUE = TermColors.BBlue
    OKGREEN = TermColors.BGreen
    WARNING = TermColors.BYellow
    ERROR = TermColors.BRed
    ENDC = TermColors.Color_Off


class ColorString(str):
    """Simple string class with coloration. Can be used as a str."""

    keys = dict((getattr(TermColors, x), x) for x in TermColors.__dict__ if not x.startswith("_"))

    def __new__(cls, str_, *args):
        return str.__new__(cls, str_)

    def __init__(self, _, color_):
        # reference the internal string to avoid calling super when needed.
        self.s = super().__str__()
        self.color = color_

    def __format__(self, fmt):
        return f"{self.color}{self.s:{fmt}}{TermColors.Color_Off}"

    def __str__(self):
        return "{}{}{}".format(self.color, self.s, TermColors.Color_Off)

    def __repr__(self):
        return "ColorString({!r}, {})".format(self.s, self.keys.get(self.color, repr(self.color)))

    def __len__(self) -> int:
        return len(self.s)
