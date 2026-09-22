require recipes-xrt/xrt/xdna-driver.inc
require recipes-xrt/xrt/vek385-aie-variant.inc

SUMMARY     = "AMD XDNA userspace shim (libxrt_driver_xdna)"
DESCRIPTION = "Builds the AIE4/NPU userspace shim plugin from xdna-driver/src/shim. \
Produces libxrt_driver_xdna.so which XRT loads at runtime to talk to the \
amdxdna kernel driver over RPMsg."

LICENSE = "GPL-2.0-or-later & Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE.amdnpu;md5=ea42c0f38f2d42aad08bd50c822460dc"

COMPATIBLE_MACHINE = "versal2|versal-2ve-2vm"

# This recipe is only meaningful for AIE4. In AIE2 the XRT recipe builds the
# VE2 shim itself (via -DXDNA_VE2=ON -DXRT_EDGE=1), so skip the recipe there
# to avoid double-building / packaging conflicts.
VEK385_AIE_VARIANT ??= "ve2"
python __anonymous () {
    if not vek385_aie_has_npu_fw(d):
        raise bb.parse.SkipRecipe(
            "xdna-shim is only built when vek385_aie_has_npu_fw (NPU firmware path)"
        )
}

# Use the full xdna-driver tree (top-level CMakeLists.txt) so the CMake
# glue in CMake/xrt.cmake pulls in the 'xrt' git submodule and builds the
# shim (src/shim) + just enough of XRT (xrt_core / xrt_coreutil) that the
# shim links against. Driver and tests are skipped via SKIP_KMOD=ON.
S = "${WORKDIR}/git"

inherit cmake pkgconfig

# Use Unix Makefiles for consistency with amdxdna / xrt recipes.
OECMAKE_GENERATOR = "Unix Makefiles"

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
# systemtap is needed for <sys/sdt.h>, which xrt/src/runtime_src/core/common/
# detail/linux/trace.h pulls in. The python3 pollution that systemtap normally
# causes (via PACKAGECONFIG[python3-probes] -> setuptools3-base -> target
# python3) is disabled globally by the systemtap_%.bbappend in this layer, and
# any residual Python3 in the sysroot is ignored at configure time via
# -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE (see EXTRA_OECMAKE below).
#
# NOTE: xrt is in DEPENDS (build-time) as well as RDEPENDS (below). Two
# reasons:
#   1) QA check [build-deps] requires any package listed in RDEPENDS to
#      also appear in DEPENDS (otherwise QA prints
#        xdna-shim rdepends on xrt, but it isn't a build dependency?
#      at do_package_qa time).
#   2) Task ordering. do_package_qa does file-rdeps validation by looking up
#      each DT_NEEDED soname against the pkgdata/shlibs2 DB that recipes
#      write during do_packagedata. Without an explicit DEPENDS edge there
#      is no guarantee that xrt:do_packagedata runs before
#      xdna-shim:do_package_qa, so the lookup can miss xrt's contributions
#      and fail even though RDEPENDS is correct.
# We do NOT rely on xrt's sysroot output during compile (the xdna-driver
# tree has its own xrt/ submodule that src/shim builds privately against),
# but the DEPENDS edge is essentially free -- xrt builds anyway for the
# image -- and buys us the QA + ordering correctness above.

# Bidirectional with xrt: xrt_%.bbappend (AIE4) has
#   RDEPENDS:${PN} += "xdna-shim"
# and we add xrt here because libxrt_driver_xdna.so's DT_NEEDED lists
# libxrt_core.so.2 / libxrt_coreutil.so.2, both of which live in the main
# xrt package (meta-xilinx-core's xrt_%.bb: FILES:${PN} += "${libdir}/
# lib*.so.*" keeps every libxrt_*.so.* together and defeats debian.bbclass
# auto-renaming into lib<name><soversion>). The resulting runtime cycle is
# physical reality -- neither xrt nor the shim is usable without the other
# on AIE4 -- and rpm/dpkg install both in one transaction.
RDEPENDS:${PN} = "amdxdna bash boost-system boost-filesystem xrt"

# SKIP_KMOD=ON  -> do NOT build xdna-driver/drivers/ or test/; those come from
#                  the amdxdna and (future) shim-test recipes instead.
# No XDNA_VE2   -> top-level CMake takes the AIE4/NPU branch, builds
#                  xdna-driver/src/shim and just the XRT bits the shim needs.
EXTRA_OECMAKE += " \
    -DSKIP_KMOD=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
"

# Ignore Python3 during configure. XRT's xrt/src/python/pybind11/CMakeLists.txt
# does find_package(Python3 COMPONENTS Development Interpreter) (not REQUIRED)
# and only proceeds to find_package(pybind11 2.6.0 REQUIRED) if HAS_PYTHON was
# set. CMAKE_DISABLE_FIND_PACKAGE_<Pkg>=TRUE forces the non-REQUIRED call to
# return NOT FOUND, HAS_PYTHON is never set, and the pybind11 check is skipped.
# This is independent of whatever transitive DEPENDS happens to drag python3
# dev headers into the recipe sysroot.
EXTRA_OECMAKE += " -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE "

# Only build the shim library; this also drags in xrt_core / xrt_coreutil
# as transitive deps from the xdna-driver XRT submodule.
OECMAKE_TARGET_COMPILE = "xrt_driver_xdna"

# Do NOT run cmake's `install` target.
#
# `cmake --install ${B}` unconditionally walks every install() rule in the
# whole project, regardless of what was in `make all` / OECMAKE_TARGET_COMPILE.
# The xdna-driver XRT submodule carries install() rules for a growing set of
# XRT libraries (xdp_core, xrt_hwemu, xbutil/xrt-smi, ...) that assume those
# targets were built. Since OECMAKE_TARGET_COMPILE is pinned to xrt_driver_xdna,
# most of them aren't, and `cmake --install` dies with e.g.:
#
#   CMake Error at xrt/src/runtime_src/xdp/profile/cmake_install.cmake:57 (file):
#     file INSTALL cannot find
#     ".../build/xrt/src/runtime_src/xdp/profile/libxdp_core.so.2.23.0":
#     No such file or directory.
#
# Skipping the cmake install and hand-placing exactly what this recipe is
# supposed to ship (libxrt_driver_xdna.so*) sidesteps that entirely. It also
# drops the old /bins/lib/ shuffle: src/shim's install() also created copies
# under XDNA_BIN_DIR=/bins/lib/ for dev convenience, which we then had to
# move back to ${libdir} and scrub. The build tree already has the right
# library at ${B}/src/shim/, so just copy from there. Same pattern xdna-shim-
# test uses.
OECMAKE_TARGET_INSTALL = ""

do_install () {
    install -d ${D}${libdir}
    # DO NOT use `cp -a` here: it preserves the build-user's numeric uid
    # (the uid of whoever launched bitbake, e.g. 18965 on a cluster NFS
    # mount). pseudo records that uid in its ownership DB, and later
    # do_package_write_rpm -> package_rpm.bbclass:get_attr() does
    #   pwd.getpwuid(stat_f.st_uid)
    # against the recipe-sysroot's /etc/passwd, which only contains system
    # and target users. The build-user uid is not there, so get_attr raises
    #   KeyError: 'getpwuid(): uid not found: 18965'
    # and the whole recipe fails at packaging time.
    #
    # Use `install` for the real file (it chowns to 0:0 via pseudo on copy)
    # and recreate the .so / .so.<soname> symlinks explicitly. Symlinks copy
    # is fine via `ln -s`; pseudo doesn't track a meaningful uid for them,
    # but get_attr uses os.stat(follow_symlinks=False) so the symlink's own
    # uid still has to be sane -- `ln -s` gives it the current (pseudo-root)
    # uid, which resolves cleanly.
    for f in ${B}/src/shim/libxrt_driver_xdna.so*; do
        [ -e "$f" ] || continue
        base=$(basename "$f")
        if [ -L "$f" ]; then
            target=$(readlink "$f")
            ln -sf "$target" ${D}${libdir}/"$base"
        else
            install -m 0755 "$f" ${D}${libdir}/
        fi
    done
}

FILES_SOLIBSDEV = ""
FILES:${PN} += " \
    ${libdir}/libxrt_driver_xdna.so \
    ${libdir}/libxrt_driver_xdna.so.* \
"
FILES:${PN}-dev += " \
    ${includedir}/xdna* \
"
INSANE_SKIP:${PN} += "dev-so"

PACKAGE_CLASSES = "package_rpm"
