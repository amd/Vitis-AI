# NPU stack

## Copyright and license statement
Copyright (C) 2024 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

## Folder Structure

The current directory contains all source code of the NPU SW stack.

## Build and install NPU stack for embedded solutions:

* Download sdk.sh from release package and install sdk.sh to <dest_directory>

Step 1 : Source sysroot path if not done already

```
source <dest_directory>/environment-setup-aarch64-xilinx-linux
```

Step 2 : Build NPU Stack and sample application

```
cd <Location to Vitis-AI>/src/vart_ml/
make clean
make all
make install-tar    # This install rule will compress all installable components into a tar, which one can copy to target and untar to place each component
                      in their respective locations. `vart_ml_install.tar.gz` will be generated in the current directory.
                      This file should be untar on the board using command `tar xzf vart_ml_install.tar.gz -C /`
                      This command will overwrite the VART ML SW stack on the board.

make install-sdk    # This will populate the petalinux sdk sysroot which is exported in the environment with compiled libraries and headers.
                      This will be used when the same sdk is used for compiling applications based on this repo.
                      This step is not needed if only the VART ML SW stack needs to be updated on the board.
```


Step 3 : Install the compiled VART ML SW stack on the target board.

```
cd <Location to Vitis-AI>/src/vart_ml/
scp vart_ml_install.tar.gz root@<target ip>:/tmp/
# on the board
tar xzf /tmp/vart_ml_install.tar.gz -C /
```

Step 4: Execute the sample demo application on target board for resnet50

* Enable VART ML SW stack

```
. /etc/vai.sh
```

* Run the example demo using the ResNet50 snapshot prebuilt in the SDCard.

```
vart_ml_runner.py --snapshot /run/media/mmcblk*/snapshot.$NPU_IP.resnet50.TF
```

