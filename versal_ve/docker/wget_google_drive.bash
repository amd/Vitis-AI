#! /bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

# wrapper script to download a file from Google Drive
# Syntax
# $0 https://drive.google.com/uc?id=0B4pXCfnYmG1WUUdtRHNnLWdaMEU save.dat

link="$1"
outfile="$2"

tmpfile=$( mktemp )

clean() {
  rm "$tmpfile"
}

trap clean EXIT

# Default redirection doesn't take "confirm" field into account; manually changing
# "https://drive.google.com/uc?id=<file_id>" to "https://drive.usercontent.google.com/download?id=<file_id>"
outlink="https://drive.usercontent.google.com/download?$( echo $link | sed -rn 's/.*\/uc\?(.*)/\1/p' )"

wget --no-check-certificate --load-cookies "$tmpfile" -O "$outfile" \
  "$outlink&export=download&confirm=$(
  wget --quiet --save-cookies "$tmpfile" --keep-session-cookies \
  --no-check-certificate -O - "$link&export=download" | \
  sed -rn 's/.*name="confirm" value="([0-9A-Za-z_]+)".*/\1\n/p' )"

