DESCRIPTION = "MLDebugger - NPU Debug tool for VAIML/X2 ML designs"
HOMEPAGE = "https://github.com/Xilinx/MLDebugger"
LICENSE = "Apache-2.0"
PV = "0.1.0+git${SRCPV}"

# Fetch the full MLDebugger sources. llvm-objdump.aarch64 is a git-lfs blob;
# git-lfs may be blocked by policy, so do_fetch optionally pulls it from
# GitHub's media CDN (DL_DIR cache + checksum) and do_unpack overlays it.

require mldebugger-git.inc

SRC_URI = "${MLDEBUGGER_GIT_URI}"
SRCREV = "ba0397e66b1f79e969461ccdb2c5e114be094d7a"
# LFS oid sha256 at SRCREV above; update both when bumping SRCREV.
LLVM_OBJDUMP_URI = "https://media.githubusercontent.com/media/Xilinx/MLDebugger/${SRCREV}/src/mldebug/bin/llvm-objdump.aarch64;downloadfilename=llvm-objdump.aarch64;sha256sum=24ebd9f873f783685f611aaddf2507b966911c6251c1ab76a00ae539d1d78228"
S = "${WORKDIR}/git"
LIC_FILES_CHKSUM = "file://${S}/LICENSE;md5=339c1da88443bff3a1b84d59a9bdefa6"

inherit python3-dir

# MLDebugger is pure-python (argparse based) plus a prebuilt aarch64 XRT
# backend extension. It needs the Python standard library and, at runtime,
# libxrt_coreutil (pulled in via xrt). zip/unzip are used by helper scripts.
# libgcc/libstdc++/glibc are required by the prebuilt llvm-objdump.aarch64
# binary and xrt_backend*.so (file-rdeps QA).
RDEPENDS:${PN} = " \
    python3-core \
    python3-modules \
    xrt \
    zip \
    unzip \
    libgcc \
    libstdc++ \
    glibc \
"

python do_fetch:append() {
    import bb.fetch2 as fetch2

    uri = d.expand("${LLVM_OBJDUMP_URI}")
    try:
        fetch2.Fetch([uri], d).download()
    except fetch2.BBFetchException as exc:
        bb.warn("mldebug-xdp: optional llvm-objdump.aarch64 fetch failed (%s); disassembly features may be unavailable" % exc)
}

python do_unpack:append() {
    import bb.fetch2 as fetch2
    import os
    import shutil

    dest = os.path.join(d.getVar("S"), "src/mldebug/bin/llvm-objdump.aarch64")

    # A host with git-lfs available resolves the blob during do_unpack already.
    if os.path.isfile(dest):
        with open(dest, "rb") as objdump:
            if objdump.read(23) != b"version https://git-lfs":
                return

    uri = d.expand("${LLVM_OBJDUMP_URI}")
    try:
        # download() stores under DL_DIR; the URI is not in SRC_URI, so nothing
        # copies it into WORKDIR for us.
        downloaded = fetch2.Fetch([uri], d).localpath(uri)
    except fetch2.BBFetchException:
        downloaded = None

    if not downloaded or not os.path.isfile(downloaded):
        # Keeping the pointer stub would package a text file under the name of
        # an executable, which fails at exec time instead of at import time.
        if os.path.isfile(dest):
            os.remove(dest)
        bb.warn("mldebug-xdp: llvm-objdump.aarch64 unavailable; mldebug disassembly will not work")
        return

    bb.utils.mkdirhier(os.path.dirname(dest))
    shutil.copy2(downloaded, dest)
    os.chmod(dest, 0o755)
}

do_compile[noexec] = "1"

do_install() {
    # Install the mldebug python package into site-packages
    install -d ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug
    cp -rf ${S}/src/mldebug/. ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug/

    # Drop non-aarch64 prebuilt artifacts to keep the rootfs small
    find ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug -name "*.pyd" -delete
    find ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug -name "*win_amd64*" -delete
    find ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug -name "*x86_64-linux-gnu*" -delete
    rm -f ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug/bin/c++filt.exe
    rm -f ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug/bin/llvm-objdump.exe
    rm -f ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug/bin/llvm-objdump.elf

    # Ensure the aarch64 objdump is executable
    if [ -f ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug/bin/llvm-objdump.aarch64 ]; then
        chmod 0755 ${D}${PYTHON_SITEPACKAGES_DIR}/mldebug/bin/llvm-objdump.aarch64
    fi

    # Console launcher matching the pyproject entry point
    # (mldebug = "mldebug.mldebug_cli:app")
    install -d ${D}${bindir}
    printf '%s\n' '#!/bin/sh' 'exec python3 -m mldebug "$@"' > ${D}${bindir}/mldebug
    chmod 0755 ${D}${bindir}/mldebug
}

FILES:${PN} += " \
    ${PYTHON_SITEPACKAGES_DIR}/mldebug \
    ${bindir}/mldebug \
"

# Prebuilt binaries (aarch64 .so and llvm-objdump) must not be stripped or
# split, and should not participate in the usual shared-lib QA checks.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
EXCLUDE_FROM_SHLIBS = "1"
INSANE_SKIP:${PN} += "already-stripped ldflags textrel arch"
