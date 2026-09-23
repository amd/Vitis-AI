# vitis-ai

### Setup.

Clone the repo locally.

Source the required tools (2026.1). What you source depends on the target platform:

* **vek280 — AMD-EDF / Yocto flow (PetaLinux-free):** only the Vitis tools are needed.
```
source <path-to-installed-Vitis-2026.1>/settings64.sh
```

Download NPU IP from amd.com:
```
source npu_ip/settings.sh IP_NAME

To get IP_NAME, run command source npu_ip/settings.sh LIST
```

### How to build

```
make -C versal_ve/reference_design/vek280 all
```

For more details refer user guide ug1703_vitis_ai_developer_guide_WtMkX.pdf.

### What the vek280 build does (AMD-EDF flow)
```
0. Downloads the NPU IP from xilinx.com, and skips if already present.
1. Builds the Vivado extensible XSA (hw/).
2. Builds the AMD-EDF/Yocto SW via bitbake: device-tree, BOOT assets (u-boot/ATF), rootfs WIC, SDK.
   (PetaLinux-free — no .bsp; boot artifacts come from the EDF yocto-manifests.)
3. Vitis v++ link + package: overlay xclbin + EDF BOOT.BIN.
4. Downloads the Vitis docker to generate the sample ResNet50 snapshot (skippable: SKIP_SNAPSHOT=1).
5. Creates the "output" directory at versal_ve/reference_design/vek280 with the final binaries:
   *_vitis_assembled.wic, x_plus_ml.xclbin, BOOT.BIN, version.txt, npu_*_utilization.rpt, sdk.sh.
```
