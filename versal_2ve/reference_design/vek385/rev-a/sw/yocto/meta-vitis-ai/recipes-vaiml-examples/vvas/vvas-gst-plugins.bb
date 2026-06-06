SUMMARY = "VVAS GStreamer Plugins"
DESCRIPTION = "GStreamer plugins and gst-libs for the VVAS SDK (xinfer, xabrscaler, xoverlay, etc.)"
SECTION = "multimedia"
LICENSE = "Apache-2.0"

SRC_URI = "git://github.com/amd/VVAS.git;protocol=https;branch=release/6.2"
SRCREV = "7b724ea7b896fbf95ba56faec4c447ca04e0687a"
LIC_FILES_CHKSUM = "file://../LICENSE;md5=31db053139540d9d251012082be3c4f7"

S = "${WORKDIR}/git/vvas-gst-plugins"

# ryzenai-wheels is needed at build-time: the xinfer plugin links against
# libonnxruntime.so (cc.find_library in sys/infer/meson.build).

DEPENDS = " \
    vart-x \
    vvas-utils \
    glib-2.0 \
    glib-2.0-native \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    ryzenai-wheels \
    vart-ml \
    opencv \
    jansson \
"

inherit meson pkgconfig

TARGET_CPPFLAGS:append = " -I=/usr/include/xrt"

EXTRA_OEMESON = ""

SOLIBS = ".so"
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so"
# file-rdeps: xinfer links libonnxruntime.so.1 whose versioned soname
# (VERS_1.23.3) is not declared in ryzenai-wheels RPROVIDES; the library
# is present at runtime via the RDEPENDS below.
INSANE_SKIP:${PN} += "file-rdeps"
RDEPENDS:${PN} += "ryzenai-wheels"

# /usr/vvas/ contains config.h and VERSION installed by the upstream
# meson.build (configure_file + install_headers).
FILES:${PN} += " \
    ${prefix}/vvas/ \
    ${libdir}/*.so* \
    ${libdir}/gstreamer-1.0/*.so \
    ${sysconfdir}/vvas/ \
"

FILES:${PN}-dev += " \
    ${includedir}/gstreamer-1.0/gst/vvas/ \
    ${libdir}/pkgconfig/*.pc \
    ${libdir}/gstreamer-1.0/pkgconfig/*.pc \
"
