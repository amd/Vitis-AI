#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

# script to format vaisw config option to .ini format
#
# Syntax
# $0 option1 option2 option3
#
# N.B. "option" must be of the form "section.name=value"
#
# e.g.:
#
#   >$0 section.name=value
# is transformed to (not that new line is inserted at the end)
#   >[section]
#   >	name=value
#   >

while [ "$1" != "" ]
do
  IFS=. read section name_and_value <<< "$1"
  # printf seems to be more consistent across various shell than echo
  printf "[$section]\n\t$name_and_value\n"
  shift
done
