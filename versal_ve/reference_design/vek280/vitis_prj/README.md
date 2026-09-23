## Vitis X+ML reference design for VEK280 (EDF / PetaLinux-free)

Source the 2026.1 Vitis tools in the bash shell (no PetaLinux), and skips sourcing if they
are already available.

Make sure the hw, sw is built before the vitis_prj build.

Builds the X+ML reference design: `link` connects the Vitis PL/AIE kernels (pre-processing,
NPU VSS) to the platform; `package` runs the EDF `v++ -p` (overlay xclbin + BOOT.BIN with the
EDF u-boot/ATF/zocl dtb).

### Usage
```
make all
```
On successful build, `x_plus_ml.xclbin` + `BOOT.BIN` are in `vitis_prj/package/hw_outputs/`.

For more details refer to the top-level `README.md` and the AMD-EDF migration notes.
