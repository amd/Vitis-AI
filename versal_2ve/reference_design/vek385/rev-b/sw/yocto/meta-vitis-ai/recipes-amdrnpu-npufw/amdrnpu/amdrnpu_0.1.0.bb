SUMMARY = "AMD amdrnpu driver, userspace library, and test CLI"
DESCRIPTION = "Builds amdrnpu.ko, libamdrnpu, and amdrnpu_test \
from PhysicalAI/tessai-agent. The amdrnpu DT node is built permanently \
into the system DTB (see meta-vek385 device-tree custom.dtsi)."
HOMEPAGE = ""
SECTION = "kernel/modules"

LICENSE = "GPL-2.0-only & MIT & Apache-2.0"
LIC_FILES_CHKSUM = " \
    file://amdrnpu_drv.c;beginline=1;endline=2;md5=5f1db1a4251755b003fcaedee0489dba \
    file://../../../lib/src/amdrnpu_lib.c;beginline=1;endline=1;md5=234d7d4edd08962c0144e4604050e0b6 \
"

require conf/amdrnpu-git.inc

inherit module pkgconfig

DEPENDS += "cmake-native libdrm"

AMDRNPU_LIB_B = "${WORKDIR}/build-lib"

# The driver dir is still named tessai-agent upstream (only the path is not yet
# renamed; all file/module/lib names are already amdrnpu).
S = "${AMDRNPU_ROOT}/drivers/accel/tessai-agent"
B = "${S}"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

PACKAGES =+ "${PN}-lib ${PN}-examples"
ALLOW_EMPTY:${PN} = "1"
RDEPENDS:${PN} = "kernel-module-amdrnpu"
RRECOMMENDS:${PN} = "${PN}-lib ${PN}-examples"

FILES:${PN}-lib = " \
    ${libdir}/libamdrnpu.so.0 \
    ${libdir}/libamdrnpu.so.0.1.0 \
"
FILES:${PN}-dev += " \
    ${libdir}/libamdrnpu.so \
    ${includedir}/amdrnpu/* \
"
# The libamdrnpu.so dev symlink resolves to libamdrnpu.so.0 in ${PN}-lib, so
# ${PN}-dev must pull ${PN}-lib to avoid a dangling symlink (e.g. in the SDK
# sysroot). += preserves any auto-generated -dev dependencies.
RDEPENDS:${PN}-dev += "${PN}-lib"
FILES:${PN}-examples = "${bindir}/amdrnpu_test"
RDEPENDS:${PN}-examples = "${PN}-lib"
RRECOMMENDS:${PN}-lib = "kernel-module-amdrnpu"

do_compile() {
	module_do_compile
}

do_compile:append() {
	# module_do_compile sets CC with flags; cmake needs a bare compiler path.
	unset CC CXX CPP
	CC_PATH="$(which ${TARGET_PREFIX}gcc)"
	CXX_PATH="$(which ${TARGET_PREFIX}g++)"
	rm -rf "${AMDRNPU_LIB_B}"
	cmake -G "Unix Makefiles" \
		-B "${AMDRNPU_LIB_B}" \
		-S "${AMDRNPU_ROOT}/lib" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
		-DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_SYSROOT="${STAGING_DIR_HOST}" \
		-DCMAKE_FIND_ROOT_PATH="${STAGING_DIR_HOST}" \
		-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
		-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
		-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
		-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
		-DCMAKE_C_COMPILER:FILEPATH="${CC_PATH}" \
		-DCMAKE_CXX_COMPILER:FILEPATH="${CXX_PATH}" \
		-DCMAKE_C_FLAGS="${CFLAGS}" \
		-DCMAKE_CXX_FLAGS="${CXXFLAGS}" \
		-DCMAKE_EXE_LINKER_FLAGS="${LDFLAGS}" \
		-DCMAKE_SHARED_LINKER_FLAGS="${LDFLAGS}" \
		-DCMAKE_MODULE_LINKER_FLAGS="${LDFLAGS}" \
		-DCMAKE_INSTALL_PREFIX="${prefix}" \
		-DCMAKE_INSTALL_LIBDIR="${baselib}" \
		-DCMAKE_INSTALL_BINDIR="${bindir}" \
		-DCMAKE_INSTALL_INCLUDEDIR="${includedir}" \
		-DAMDRNPU_UAPI_DIR=${AMDRNPU_ROOT}/include/uapi \
		-DAMDRNPU_LIB_BUILD_SHARED=ON \
		-DAMDRNPU_LIB_BUILD_STATIC=OFF \
		-DAMDRNPU_LIB_BUILD_EXAMPLES=ON
	oe_runmake -C "${AMDRNPU_LIB_B}"
}

do_install:append() {
	DESTDIR=${D} cmake --install "${AMDRNPU_LIB_B}"
}

# amdrnpu DT node is compiled into the system DTB, so the driver can be
# autoloaded once the carveouts are present.
# KERNEL_MODULE_AUTOLOAD += "amdrnpu"
