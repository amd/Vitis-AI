#!/bin/bash
#
# Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Description:
#   bash script to initialize repo and install yocto repo
#

install_repo()
{
    echo "Installing repo...."
    curl https://storage.googleapis.com/git-repo-downloads/repo > repo
    chmod a+x repo
    mkdir -p "$HOME/bin"
    mv repo "$HOME/bin/"
    export PATH="$HOME/bin:$PATH"
}

initialize_repo()
{
    echo "Initializing yocto repo..."
    echo "repo init -u $YOCTO_REPO_URL -b $YOCTO_BRANCH -m $MANIFEST_OPT"
    yes ""| repo init -u $YOCTO_REPO_URL -b $YOCTO_BRANCH -m $MANIFEST_OPT
    echo "Repo initialized successfully!"
    repo sync
}

YOCTO_REPO_URL="${YOCTO_REPO_URL:-https://github.com/Xilinx/yocto-manifests.git}"
YOCTO_BRANCH="${YOCTO_BRANCH:-refs/tags/amd-edf-rel-v25.11}"
MANIFEST_FILE="${MANIFEST_FILE:-default-edf.xml}"

# If MANIFEST_PATH is set, include it; else keep empty
if [ -n "$MANIFEST_PATH" ]; then
  MANIFEST_OPT="$MANIFEST_PATH/$MANIFEST_FILE"
else
  MANIFEST_OPT="$MANIFEST_FILE"
fi

if ! command -v repo &> /dev/null; then
    echo "Repo command not found. Installing repo..."
    install_repo
elif [[ $(repo --version 2>&1 | grep -oP 'repo launcher version \K[0-9.]+') < 2.5 ]]; then
    echo "Repo version is less than 2.5. Reinstalling repo..."
    install_repo
fi

initialize_repo
