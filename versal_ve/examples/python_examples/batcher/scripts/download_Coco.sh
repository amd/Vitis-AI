#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

wget "http://images.cocodataset.org/zips/val2017.zip" && unzip val2017.zip -d $1/WIP && rm -f val2017.zip || exit 1
wget "http://images.cocodataset.org/annotations/annotations_trainval2017.zip" && unzip annotations_trainval2017.zip -d $1/WIP && rm -f annotations_trainval2017.zip || exit 1

mv $1/WIP/* $1/
rmdir $1/WIP
