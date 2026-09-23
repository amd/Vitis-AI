#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

set -e

BASEDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

cd $BASEDIR/..

rm -f mipso.log
rm -f ../dist/log/*
rm -rf batcher/build_*
