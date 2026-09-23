## VEK280 top-level build (AMD-EDF / Yocto, 2026.1)

Requires the 2026.1 Vitis tools on PATH (for sdtgen / v++ / xclbinutil / bootgen) plus the Yocto
host build tools. This is a **PetaLinux-free AMD-EDF flow** — no `.bsp` and no `BSP_PATH`: the boot
artifacts (u-boot, ATF, device-tree, rootfs) are produced by bitbake from the EDF yocto-manifests.
See the "In AMD internal networks" section of `README_AMD.md` for the exact 2026.1 Vitis tool path.


### Prerequisites

Before running `make` for the first time, provision the required host tools by running:

```bash
bash setup_yocto_host.sh
```

This installs all required build-time host packages (including `bmaptool`, `wic`, `chrpath`,
`diffstat`, and others). `make` assumes these are already on PATH and will fail fast if any
are missing.

> **Note:** `setup_yocto_host.sh` requires `sudo` and is intended to be run once per build
> host.

### Usage
```
make all          # or: ./create_image.sh
make clean        # or: ./clean_image.sh   (granular: ./clean_image.sh hw vitis edf output)
```
`SKIP_SNAPSHOT=1 make all` bypasses the ResNet50 snapshot stage.

The top-level `Makefile` is a thin launcher; all build/clean orchestration lives in the scripts
(`create_image.sh` / `clean_image.sh`).

`make all` runs the following stages (see `create_image.sh`):
```
0. ResNet50 TF2 snapshot generation (docker)            [skippable: SKIP_SNAPSHOT=1]
1. create_pfm_hw.sh    — Vivado extensible XSA (hw/)
2. create_pfm_sw.sh    — AMD-EDF/Yocto SW: device-tree, BOOT assets, rootfs WIC, SDK
3. create_vitis_app.sh — v++ link + v++ -p package (overlay xclbin + EDF BOOT.BIN)
4. image_assemble.sh   — wic cp BOOT.BIN + xclbin + snapshots -> bootable WIC
```
On success, the `output/` directory contains the final deliverables:
`*_vitis_assembled.wic`, `x_plus_ml.xclbin`, `BOOT.BIN`, `version.txt`,
`npu_*_utilization.rpt`, and (unless `NO_SDK_BUILD=1`) `sdk.sh`.
