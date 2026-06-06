SUMMARY = "VVAS Accelerator Software Libraries"
DESCRIPTION = "Software-stub accelerator library (libvvas_xstub) and sample config JSON files for VVAS"
SECTION = "multimedia"
LICENSE = "Apache-2.0"

SRC_URI = "git://github.com/amd/VVAS.git;protocol=https;branch=release/6.2"
SRCREV = "7b724ea7b896fbf95ba56faec4c447ca04e0687a"
LIC_FILES_CHKSUM = "file://../LICENSE;md5=31db053139540d9d251012082be3c4f7"

S = "${WORKDIR}/git/vvas-accel-sw-libs"
RDEPENDS:${PN} += "vart-x"
DEPENDS = " \
    vart-x \
    vvas-utils \
    vvas-gst-plugins \
    glib-2.0 \
    glib-2.0-native \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    opencv \
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
    ${sysconfdir}/vvas/ \
"
