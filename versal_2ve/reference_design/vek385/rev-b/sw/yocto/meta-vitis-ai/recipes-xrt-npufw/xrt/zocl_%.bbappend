require recipes-xrt/xrt/xdna-driver.inc
S="${WORKDIR}/git/xrt/src/runtime_src/core/edge/drm/zocl"
LIC_FILES_CHKSUM = "file://LICENSE;md5=7d040f51aae6ac6208de74e88a3795f8"
INSANE_SKIP:${PN} += "arch"
PACKAGE_CLASSES = "package_rpm"
LICENSE = "GPLv2 & Apache-2.0"

# zocl is built for every VEK385_AIE_VARIANT, including the NPU firmware path.
# It reaches the AIE array through the in-tree xilinx-aie driver
# (aie_partition_request()), so CONFIG_XILINX_AIE must stay enabled -- see
# linux-xlnx_%.bbappend. zocl and amdxdna therefore coexist in one image;
# arbitrating the array between a zocl application and an amdxdna application
# is left to the user at runtime.
