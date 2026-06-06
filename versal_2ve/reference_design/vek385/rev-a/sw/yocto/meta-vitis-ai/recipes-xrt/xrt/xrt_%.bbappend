require xdna-driver.inc
S="${WORKDIR}/git"
LIC_FILES_CHKSUM = "file://LICENSE.amdnpu;md5=ea42c0f38f2d42aad08bd50c822460dc"

# Use Unix Makefiles generator instead of Ninja due to $(MAKE) usage in CMakeLists.txt
OECMAKE_GENERATOR = "Unix Makefiles"

# Ensure CROSS_COMPILE and ARCH are set to match the kernel build environment
# This overrides the module Makefile's default of /lib/modules/$(uname -r)/build
# Note: ARCH must be the kernel arch (arm64), not package arch (aarch64)
export XDNA_DRV_BLD_FLAGS = "KERNEL_SRC=${STAGING_KERNEL_BUILDDIR} CROSS_COMPILE=${TARGET_PREFIX} ARCH=arm64"

EXTRA_OECMAKE += " \
    -DXDNA_VE2=ON \
    -DXRT_EDGE=1 \
    -DXRT_YOCTO=1 \
    -DXRT_ENABLE_HIP=1 \
    "
DEPENDS += "virtual/kernel hip systemtap"
RDEPENDS:${PN} += "pvt"

INSANE_SKIP:${PN} += "arch"
PACKAGE_CLASSES = "package_rpm"
LICENSE = "GPL-2.0-only & Apache-2.0"
