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
    -DXDNA_BUILD_SHIM_TEST=ON \
    "
DEPENDS += "virtual/kernel systemtap"
RDEPENDS:${PN} += "pvt"

# The VE2 xrt build already compiles the whole xdna-driver tree; with
# -DXDNA_BUILD_SHIM_TEST=ON above, CMake/native.cmake also builds and installs
# test/shim_test/shim_test.{elf,sh}. Upstream's cmake install drops them into
# ${XDNA_BIN_DIR}/bin (/bins/bin, a dev-only tree that nothing packages).
# Relocate them into ${bindir} and drop /bins so they ship under a normal
# prefix. This replaces the standalone xdna-shim-test recipe.
do_install:append () {
    install -d ${D}${bindir}
    mv ${D}/bins/bin/shim_test.elf ${D}${bindir}/shim_test.elf
    mv ${D}/bins/bin/shim_test.sh  ${D}${bindir}/shim_test.sh
    rm -rf ${D}/bins

    # shim_test resolves its data dir as <dir-of-argv0>/../local_shim_test_data.
    # With the ELF in ${bindir} (/usr/bin), that is ${prefix}/local_shim_test_data;
    # point it at the shared data installed by pvt.
    ln -sfn ${datadir}/amdxdna/local_shim_test_data ${D}${prefix}/local_shim_test_data
}

# Ship the shim test as its own optional package so production images can omit
# it. The npu_ve2 full-ELF test data comes from the pvt recipe (VTD source),
# which also provides the platform validation data and is already installed
# alongside xrt.
PACKAGES =+ "${PN}-shim-test"
FILES:${PN}-shim-test = " \
    ${bindir}/shim_test.elf \
    ${bindir}/shim_test.sh \
    ${prefix}/local_shim_test_data \
"
RDEPENDS:${PN}-shim-test = "bash pvt"
# shim_test.elf is built with -Wl,-rpath,$ORIGIN/../lib pointing at /usr/lib
# where libxrt_coreutil / libxrt_driver_xdna live; that's expected.
INSANE_SKIP:${PN}-shim-test += "rpaths"

# Pull the shim sanity test (and, transitively, its npu_ve2 test data) into any
# image that already installs xrt. This keeps the image wiring inside the
# recipes we own (recipes-xrt) without editing the shared packagegroup.
RDEPENDS:${PN} += "${PN}-shim-test"

INSANE_SKIP:${PN} += "arch"
PACKAGE_CLASSES = "package_rpm"
LICENSE = "GPL-2.0-only & Apache-2.0"
