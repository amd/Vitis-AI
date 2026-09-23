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

export VAISW_INSTALL_DIR=/etc/vai
VART_X_LIB=$( ls -d /usr/lib/python3*/site-packages/vart_x/lib 2>/dev/null | head -n 1 )
export LD_LIBRARY_PATH=${VART_X_LIB:+${VART_X_LIB}:}${LD_LIBRARY_PATH}
export XRT_INI_PATH=/etc/vai/xrt.ini
export PYTHONPATH=${PYTHONPATH}${PYTHONPATH:+:}$VAISW_INSTALL_DIR/lib/python

[ "$1" = "framework" ] && export PYTHONPATH=${PYTHONPATH}:$VAISW_INSTALL_DIR/lib/python/vaisw_site

export $( grep -m 1 'NPU_IP'  /run/media/mmcblk*/version.txt 2>/dev/null ) &> /dev/null
export $( grep -m 1 'NPU_IP2' /run/media/mmcblk*/version.txt 2>/dev/null ) &> /dev/null

export NPU_XCLBIN_PATH=$( ls /run/media/mmcbl*/x_plus_ml.xclbin | head -n 1 )

# in case udev symlink is not created, do it manually
if grep -q ZynqMP /proc/device-tree/model
then
  [ -e /dev/npu_zynqmp_0 ] || ln -s uio4 /dev/npu_zynqmp_0
else
  [ -e /dev/npu_versal_0 ] || ln -s uio0 /dev/npu_versal_0
  [ "$NPU_IP2" != "" ] && [ ! -e /dev/npu_versal_1 ] && ln -s uio1 /dev/npu_versal_1
fi

if [ "1" = "$( grep processor /proc/cpuinfo  | wc -l )" ]
then
  cat <<EOF

WARNING: only 1 processor has been power up during Linux boot.
This may happen during cold reboot depending on SDCard timing.

Very likely, some executions may not work.
A hot-reboot (using 'reboot' command) is highly advised.

EOF
  sleep 1

fi

echo "Using Vitis-AI 6.3. Build date YYYY_MM_DD-HH:MM TZ @ HASH"
