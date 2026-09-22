require xdna-driver.inc
require vek385-aie-variant.inc

# Local trees: USE_LOCAL_XDNA_DRIVER = "1" in local.conf (see xdna-driver-externalsrc.inc)
require ${@bb.utils.contains('USE_LOCAL_XDNA_DRIVER', '1', 'xrt-externalsrc.inc', '', d)}

S = "${@d.getVar('EXTERNALSRC') if bb.utils.contains('USE_LOCAL_XDNA_DRIVER', '1', True, False, d) else '${WORKDIR}/git'}"
LIC_FILES_CHKSUM = "file://LICENSE.amdnpu;md5=ea42c0f38f2d42aad08bd50c822460dc"

# Layer-local patches. Patch paths inside the diff are relative to
# S = ${WORKDIR}/git (the xdna-driver top-level), so the diff refers to
# 'xrt/src/runtime_src/...'.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += " \
    file://0002-xdna-driver-include-xrt-tools-in-NPU-build.patch \
    file://0003-xrt-enable-zocl-in-npu-runtime.patch \
"
# 0001 (xrt_core_static vs xrt-targets when XRT_YOCTO) was dropped: pinned XRT
# now splits pcie/linux install() and settings.cmake sets XRT_INSTALL_STATIC_LIBRARY
# OFF when XRT_YOCTO, so the old unified install() hunk no longer exists.
# 0002 only has an effect on the AIE4/NPU path (it edits CMake/xrt.cmake which
# is included by xdna-driver only when XDNA_VE2 is unset). The AIE2 path goes
# through CMake/xrt_ve2.cmake, so the patch is a no-op there. Without it the
# NPU build silently drops xrt-smi (xbutil2 lives under
# src/runtime_src/core/tools, which xrt.cmake adds to XRT_EXCLUDE_SUB_DIRECTORY).

# Use Unix Makefiles generator instead of Ninja due to $(MAKE) usage in CMakeLists.txt
OECMAKE_GENERATOR = "Unix Makefiles"

# Ensure CROSS_COMPILE and ARCH are set to match the kernel build environment
# This overrides the module Makefile's default of /lib/modules/$(uname -r)/build
# Note: ARCH must be the kernel arch (arm64), not package arch (aarch64)
export XDNA_DRV_BLD_FLAGS = "KERNEL_SRC=${STAGING_KERNEL_BUILDDIR} CROSS_COMPILE=${TARGET_PREFIX} ARCH=arm64"

# XRT comes from the xdna-driver git submodule (gitsm fetch + revision in
# SRCREV_xdna).  Populate nested XRT submodules (aiebu, elf, gsl, aie-rt, …).
python do_unpack:append() {
    if bb.utils.to_boolean(d.getVar("USE_LOCAL_XDNA_DRIVER") or "0"):
        return

    import os

    gdir = os.path.join(d.getVar("WORKDIR"), "git")
    if not os.path.isdir(gdir):
        bb.fatal("xdna-driver unpack: missing %s" % gdir)

    bb.process.run(
        ["git", "submodule", "update", "--init", "--recursive"],
        cwd=gdir,
        shell=False,
    )
}

# VEK385 AIE variant toggle (see vek385-aie-variant.inc):
#   VE2 edge (VEK385_AIE_VARIANT_VE2_MODE_VALUES, default: ve2): -DXDNA_VE2=ON,
#                    -DXRT_EDGE=1 from base recipe, amdxdna on 'of' bus.
#   NPU firmware (VEK385_AIE_VARIANT_NPU_MODE_VALUES, default: TessAI):
#                    -DXRT_NPU=ON -DXRT_DKMS=OFF, no VE2/XRT_EDGE, amdxdna npu_of.
#
#   The variant does not gate zocl: it is built for both (see zocl_%.bbappend).
#
#   Example (build/conf/local.conf):
#       VEK385_AIE_VARIANT = "TessAI"
#   Use vek385_aie_has_npu_fw(d) / vek385_aie_has_ve2_edge_fw(d) in Python snippets.
VEK385_AIE_VARIANT ??= "ve2"

# Fail fast on typos: value must match NPU_MODE or VE2_MODE lists in vek385-aie-variant.inc.
python __anonymous () {
    if not vek385_aie_variant_is_known(d):
        bb.fatal(
            "VEK385_AIE_VARIANT='%s' is not supported. Set a value listed in "
            "VEK385_AIE_VARIANT_NPU_MODE_VALUES or VEK385_AIE_VARIANT_VE2_MODE_VALUES "
            "(see meta-vitis-ai/recipes-xrt/xrt/vek385-aie-variant.inc)."
            % (d.getVar('VEK385_AIE_VARIANT') or '')
        )
}

EXTRA_OECMAKE += " \
    -DXRT_YOCTO=1 \
    -DXRT_ENABLE_HIP=ON \
    "

# VE2: -DXDNA_VE2=ON; NPU firmware path: NPU + ZOCL C++ runtime + DKMS off.
EXTRA_OECMAKE += "${@' -DXRT_NPU=ON -DXRT_NPU_ZOCL=ON -DXRT_DKMS=OFF ' if vek385_aie_has_npu_fw(d) else ' -DXDNA_VE2=ON '}"

# NPU path uses nativeLnx.cmake which visits python/; disable Python3 find (see comments above).
EXTRA_OECMAKE += "${@' -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE ' if vek385_aie_has_npu_fw(d) else ''}"

# Base recipe adds -DXRT_EDGE=1 for edge builds; drop it on the NPU firmware path.
EXTRA_OECMAKE:remove = "${@'-DXRT_EDGE=1' if vek385_aie_has_npu_fw(d) else ''}"

# Sanity: NPU firmware path must expose -DXRT_NPU=ON and not VE2/edge flags.
python __anonymous () {
    if not vek385_aie_has_npu_fw(d):
        return
    flags = d.getVar('EXTRA_OECMAKE') or ''
    if '-DXRT_NPU=ON' not in flags:
        bb.fatal("NPU firmware XRT build is missing -DXRT_NPU=ON in EXTRA_OECMAKE: %r" % flags)
    if '-DXRT_NPU_ZOCL=ON' not in flags:
        bb.fatal("NPU firmware XRT build is missing -DXRT_NPU_ZOCL=ON in EXTRA_OECMAKE: %r" % flags)
    for bad in ('-DXRT_EDGE=1', '-DXDNA_VE2=ON'):
        if bad in flags:
            bb.fatal("NPU firmware XRT build must not include %s; got EXTRA_OECMAKE=%r" % (bad, flags))
}

DEPENDS += "virtual/kernel hip systemtap"
# NOTE: systemtap MUST stay in DEPENDS -- XRT's runtime_src/core/common/
# detail/linux/trace.h #include's <sys/sdt.h> from it. The python3 pollution
# that systemtap normally causes is neutralised by
# meta-vitis-ai/recipes-kernel/systemtap/systemtap_%.bbappend (drops the
# python3-probes PACKAGECONFIG) and by -DCMAKE_DISABLE_FIND_PACKAGE_Python3
# below for the AIE4 path.

# zocl is built and shipped (see zocl_%.bbappend), but it is kept out of xrt's
# runtime deps on this platform: the base recipe's RDEPENDS is replaced below,
# and image composition goes through packagegroup-vaiml, which installs zocl
# directly.
#
# Deliberately do NOT RDEPEND on xdna-shim here, even though XRT loads the
# shim at runtime on AIE4:
#   * XRT loads libxrt_driver_xdna.so via dlopen() at runtime (not via
#     DT_NEEDED), so there is no link-time requirement that forces xdna-shim
#     to be installed -- missing shim just means no NPU support, not a
#     broken xrt binary.
#   * xdna-shim RDEPENDS on xrt (its DT_NEEDED includes libxrt_core.so.2 /
#     libxrt_coreutil.so.2 from this package). Adding xrt -> xdna-shim here
#     would make the RDEPENDS graph cyclic, which trips do_package_qa's
#     [build-deps] check and confuses debian.bbclass's shlib-rename order.
#   * Image composition is handled by packagegroup-vaiml, which already
#     pulls in xdna-shim (and xdna-shim-test) directly on AIE4, so nothing
#     is lost at rootfs time.
RDEPENDS:${PN} = "bash boost-system boost-filesystem"

INSANE_SKIP:${PN} += "arch"
PACKAGE_CLASSES = "package_rpm"
# Use SPDX-correct identifiers; "GPLv2" is flagged as obsolete by Yocto's
# obsolete-license QA check.
LICENSE = "GPL-2.0-only & Apache-2.0"

# xdna-driver's src/shim/CMakeLists.txt installs libxrt_driver_xdna into TWO
# places when the xrt recipe runs `make all`:
#
#   1. ${libdir}/libxrt_driver_xdna.so*           (canonical install for packaging)
#      install(TARGETS xrt_driver_xdna LIBRARY DESTINATION ${XDNA_PKG_LIB_DIR})
#
#   2. /bins/lib/libxrt_driver_xdna.so*           (developer convenience copy)
#      install(TARGETS xrt_driver_xdna DESTINATION ${XDNA_BIN_DIR}/${XDNA_PKG_LIB_DIR})
#      where xdna-driver/CMakeLists.txt hard-codes set(XDNA_BIN_DIR /bins).
#
# The same /bins/lib/ also collects libxrt_core.so* and libxrt_coreutil.so*.
# Without intervention this fails do_package twice over:
#
#   * QA "installed and not shipped" for everything under /bins/lib/.
#   * "Recipe xrt is trying to install files into a shared area when those
#     files already exist" on libxrt-driver-xdna2 / -lic / -dbg / -dev / -src,
#     because xdna-shim (AIE4-only recipe) owns libxrt_driver_xdna and the
#     auto-shlibs splitter would otherwise create duplicate per-shlib packages
#     from the xrt recipe.
#
# Fix: in AIE4, drop both copies of libxrt_driver_xdna from the xrt sysroot
# image so that xdna-shim is the only owner; in any variant, drop the
# /bins tree (developer-only artifacts that were never meant for packaging).
#
# Safe in AIE2 mode -- src/shim isn't built there, so neither
# ${libdir}/libxrt_driver_xdna.so* nor /bins/ exist and the `rm -f`/`rm -rf`
# are no-ops.
#
# externalsrc skips do_patch; for local trees ensure CMake/xrt.cmake matches
# files/0002-xdna-driver-include-xrt-tools-in-NPU-build.patch (no EXCLUDE_FROM_ALL,
# tools not in XRT_EXCLUDE_SUB_DIRECTORY). Without that, cmake --install only puts
# libxrt_driver_xdna in ${libdir} and libxrt_core* under /bins/lib.
do_install:append () {
    # Promote dev copies of libxrt_core* from /bins/lib before removing /bins.
    if [ -d ${D}/bins/lib ]; then
        install -d ${D}${libdir}
        for f in ${D}/bins/lib/libxrt_core.so* ${D}/bins/lib/libxrt_coreutil.so*; do
            [ -e "$f" ] || continue
            base=$(basename "$f")
            if [ -L "$f" ]; then
                ln -sf "$(readlink "$f")" ${D}${libdir}/"$base"
            else
                install -m 0755 "$f" ${D}${libdir}/
            fi
        done
    fi
    rm -rf ${D}/bins
    rm -f  ${D}${libdir}/libxrt_driver_xdna.so \
           ${D}${libdir}/libxrt_driver_xdna.so.*
    rmdir ${D}${libdir} 2>/dev/null || true

    # Upstream XRT (NPU branch) installs two files that don't fit the FHS
    # layout Yocto packages expect:
    #   xrt/src/CMake/version.cmake:170     -> ${prefix}/version.json
    #   xrt/src/CMake/nativeLnx.cmake:106   -> ${prefix}/license/LICENSE
    # Both are dev/CI breadcrumbs from the upstream tarball/dkms layout and
    # have proper Yocto equivalents already (LICENSE_* is shipped under
    # ${datadir}/licenses/xrt/, the package version is recorded in pkgdata).
    # Drop them so do_package doesn't QA-fail on installed-vs-shipped.
    rm -f  ${D}${prefix}/version.json
    rm -rf ${D}${prefix}/license

    # XRT's tools/CMakeLists.txt installs three xrt-smi completion scripts
    # (bash, csh, and a csh wrapper) under ${datadir}/completions/. We don't
    # need shell completion on this platform; the csh ones additionally
    # trigger a Yocto file-rdeps QA error because their #!/bin/csh -f
    # shebang has no provider in RDEPENDS:xrt. Drop the whole directory.
    rm -rf ${D}${datadir}/completions
}
