require recipes-xrt/xrt/xdna-driver.inc
require recipes-xrt/xrt/vek385-aie-variant.inc

SUMMARY     = "AMD XDNA shim/xrt sanity tests (shim_test.elf, xrt_test.elf)"
DESCRIPTION = "Builds the standalone AIE4/NPU sanity tests from \
xdna-driver/test/{shim_test,xrt_test}. Produces /usr/bin/shim_test.elf \
and /usr/bin/xrt_test.elf (with an xrt_test wrapper script). These exec \
xrt-smi-equivalent paths against the userspace shim (libxrt_driver_xdna) \
talking to the amdxdna kernel driver over RPMsg."

LICENSE = "GPL-2.0-only & Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE.amdnpu;md5=ea42c0f38f2d42aad08bd50c822460dc"

COMPATIBLE_MACHINE = "versal2|versal-2ve-2vm"

# AIE4 only. In AIE2/VE2 the test binaries don't apply (the shim layout is
# different) and the source tree is built via the VE2 wrapper.
VEK385_AIE_VARIANT ??= "ve2"
python __anonymous () {
    if not vek385_aie_has_npu_fw(d):
        raise bb.parse.SkipRecipe(
            "xdna-shim-test is only built when vek385_aie_has_npu_fw (NPU firmware path)"
        )
}

# Same source tree as xrt / xdna-shim / amdxdna -- the xdna-driver top-level
# CMakeLists.txt drives everything. test/ is added under
#   if(NOT SKIP_KMOD)
#     add_subdirectory(drivers)
#     add_subdirectory(test)
#   endif()
# in CMake/native.cmake, so we must run with SKIP_KMOD=OFF to even see the
# test target. The kernel module that the same condition would also build is
# kept out of the make invocation by a tight OECMAKE_TARGET_COMPILE below.
S = "${WORKDIR}/git"

inherit cmake pkgconfig

OECMAKE_GENERATOR = "Unix Makefiles"

# DEPENDS mirrors xdna-shim; in addition pull in xrt because the test ELFs
# link against xrt_coreutil and #include <xrt/...>. We rely on the xrt recipe
# (NPU firmware path / vek385_aie_has_npu_fw) for those headers/libs in the sysroot.
DEPENDS = " \
    xrt \
    libdrm \
    boost \
    opencl-headers \
    virtual/opencl-icd \
    opencl-clhpp \
    util-linux \
    protobuf \
    protobuf-native \
    rapidjson \
    elfutils \
    libffi \
    libdfx \
    systemtap \
    git-replacement-native \
    coreutils-native \
"

# Runtime: bash for the xrt_test wrapper, xdna-shim provides
# libxrt_driver_xdna.so, amdxdna provides the kernel driver.
RDEPENDS:${PN} = "bash xrt xdna-shim amdxdna pvt"

# SKIP_KMOD=OFF: needed so that add_subdirectory(test) is reached.
# The companion add_subdirectory(drivers) would normally pull amdxdna.ko
# into the ALL target, but OECMAKE_TARGET_COMPILE below restricts make to
# only the two test ELF targets, so the kbuild step never runs here.
EXTRA_OECMAKE += " \
    -DSKIP_KMOD=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
"

# Same Python3-pollution defence as xdna-shim/xrt: stop XRT's bundled
# pybind11 hookup from finding python3 in the sysroot and demanding
# pybind11. See xrt_%.bbappend for the full rationale.
EXTRA_OECMAKE += " -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE "

# Build only the two test executables. CMake's intra-target deps will pull
# in xrt_coreutil/aiebu_static automatically, but the kernel-module custom
# target ('driver') is *not* in this list and therefore never gets built.
OECMAKE_TARGET_COMPILE = "shim_test.elf xrt_test.elf"

# Skip cmake's `install` step entirely. Upstream installs the binaries to
# ${XDNA_BIN_DIR}/bin = /bins/bin/, and also drops side-effect installs of
# libxrt_driver_xdna / xrt_core / xrt_coreutil into /bins/lib/ from
# src/shim. Both fight with the xdna-shim and xrt recipes. Instead, hand-
# place just the two ELFs and the wrapper into ${bindir}.
OECMAKE_TARGET_INSTALL = ""

do_install () {
    install -d ${D}${bindir}
    install -m 0755 ${B}/test/shim_test/shim_test.elf ${D}${bindir}/shim_test.elf
    install -m 0755 ${B}/test/xrt_test/xrt_test.elf   ${D}${bindir}/xrt_test.elf
    install -m 0755 ${B}/test/xrt_test/xrt_test       ${D}${bindir}/xrt_test

    ln -sfn ${datadir}/amdxdna/local_shim_test_data ${D}${prefix}/local_shim_test_data
}

FILES:${PN} = " \
    ${bindir}/shim_test.elf \
    ${bindir}/xrt_test.elf \
    ${bindir}/xrt_test \
    ${prefix}/local_shim_test_data \
"

# Test ELFs are built with -Wl,-rpath,$ORIGIN/../lib pointing at /usr/lib
# where libxrt_coreutil and libxrt_driver_xdna live; that's expected.
INSANE_SKIP:${PN} += "rpaths"

PACKAGE_CLASSES = "package_rpm"
