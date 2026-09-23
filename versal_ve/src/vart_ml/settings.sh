# ===========================================================
# Copyright 2024 Advanced Micro Devices Inc.
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
# ===========================================================

main() {
    local BASEDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    export VAISW_INSTALL_DIR=$(dirname $BASEDIR)
    export PATH=$BASEDIR/vart_ml_tools:$PATH
    export PATH=$BASEDIR/demo:$PATH
    export PYTHONPATH=$BASEDIR/lib/python/vaisw_site:$BASEDIR/lib/python:$BASEDIR/vart_ml_py_api:$PYTHONPATH
    export LD_LIBRARY_PATH=$BASEDIR/io:$LD_LIBRARY_PATH
    export LD_LIBRARY_PATH=$BASEDIR/npu_runner:$LD_LIBRARY_PATH
    export LD_LIBRARY_PATH=$BASEDIR/utils:$LD_LIBRARY_PATH
    export LD_LIBRARY_PATH=$BASEDIR/vart_ml_runner:$LD_LIBRARY_PATH
    export LD_LIBRARY_PATH=$BASEDIR/onnx_runner:$LD_LIBRARY_PATH
}

main
