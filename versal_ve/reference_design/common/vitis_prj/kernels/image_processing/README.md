# Image Processing HLS kernel Build

The pre-built HLS kernel(image_processing.xo) is provided to integrate into design.

If required, user can also build the kernel from sources, by following steps.

Source the required tools:
```
source <path-to-installed-Petalinux-v2025.1>/settings.sh
source <path-to-installed-Vits-2025.1.1>/settings64.sh
```
Make sure the Vitis platform is ready to integrate the kernel.
### If platform is not available, build the Vitis platformi

cd ../../vek280_platform
make all BSP_PATH=<BSP file Path>
Example: make all BSP_PATH=/proj/xilinx-vek280-xsct-v2025.1-final.bsp

### Usage
```
rm -rf image_processing.xo
make clean
make all 
```
The above steps will generate image_processing.xo from the sources.

