# Sources for VART ML

## Steps to Build NPU Stack
To build NPU stack below steps need to be followed.

* Download sdk.sh from release package and install sdk.sh to <path_to_sdk_installation>

* All paths mentioned in this README refer to the base directory CWD, unless explicitly stated otherwise.


Step 1 : Source sysroot path if not done already
```
	source <path_to_sdk_installation>/environment-setup-cortexa72-cortexa53-xilinx-linux
``` 
## Build and install NPU stack for embedded solutions:
* **vart_ml** directory contains npu stack

Step 2 : Build NPU Stack
```
	make vart_ml && make vart_ml_install_tar
	# This step will update the petalinux sdk sysroot with the compiled libraries and headers. This step will also compress all installable components into a tar file, which needs to be copied to the target as explained in the next step

```
Step 3 : Copy the compiled components to target board using below commands. Ensure target board is connected to ethernet and acquired an IP address.
```
	scp vart_ml/vart_ml_install.tar.gz root@<target ip>:~/
```
Step 4: Untar the NPU components on target using the commands mentioned below to use the npu stack.
```
        cd ~/
	tar -xvf vart_ml_install.tar.gz  -C /
```
