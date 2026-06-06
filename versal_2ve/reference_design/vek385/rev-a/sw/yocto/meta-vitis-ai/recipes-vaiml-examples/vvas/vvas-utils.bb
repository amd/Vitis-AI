SUMMARY = "VVAS Utilities"
DESCRIPTION = "Core utility libraries (libvvasutil, libvvasstructure) for the VVAS SDK"
SECTION = "multimedia"
LICENSE = "Apache-2.0"

SRC_URI = "git://github.com/amd/VVAS.git;protocol=https;branch=release/6.2"
SRCREV = "7b724ea7b896fbf95ba56faec4c447ca04e0687a"
LIC_FILES_CHKSUM = "file://../LICENSE;md5=31db053139540d9d251012082be3c4f7"

S = "${WORKDIR}/git/vvas-utils"
RDEPENDS:${PN} += "vart-x"
DEPENDS = " \
    vart-x \
    glib-2.0 \
    glib-2.0-native \
    jansson \
    xrt \
"

inherit meson pkgconfig

TARGET_CPPFLAGS:append = " -I=/usr/include/xrt"

SOLIBS = ".so"
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so"
FILES:${PN} += " \
    ${libdir}/*.so* \
"

FILES:${PN}-dev += " \
    ${includedir}/vvas/ \
    ${libdir}/pkgconfig/*.pc \
"
