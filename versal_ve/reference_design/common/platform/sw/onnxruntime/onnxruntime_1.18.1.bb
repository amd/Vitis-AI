DESCRIPTION = "ONNX Runtime development package install recipe"
HOMEPAGE="https://github.com/microsoft/onnxruntime"

# Go fetch the tarball from github
SRC_URI = "https://github.com/microsoft/onnxruntime/releases/download/v${PV}/onnxruntime-linux-aarch64-${PV}.tgz"
SRC_URI[sha256sum] = "c1dcd8ab29e8d227d886b6ee415c08aea893956acf98f0758a42a84f27c02851"

S = "${WORKDIR}/onnxruntime-linux-aarch64-${PV}"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${S}/LICENSE;md5=0f7e3b1308cb5c00b372a6e78835732d"

do_install() {
        install -d ${D}/${libdir}
        install ${S}/lib/libonnxruntime.so.${PV} ${D}/${libdir}
        cd ${D}/${libdir}
        ln -s -r ./libonnxruntime.so.${PV} libonnxruntime.so
        ln -s -r ./libonnxruntime.so.${PV} libonnxruntime.so.1

        install -d ${D}/${includedir}/onnxruntime
        install ${S}/include/*.h ${D}/${includedir}/onnxruntime
}

PROVIDES = "libonnxruntime"
RPROVIDES:${PN} += "libonnxruntime.so()(64bit)"
FILES_SOLIBSDEV = ""
FILES:${PN} += "${libdir}/libonnxruntime*.so*"
INSANE_SKIP:${PN} += "dev-so"
INSANE_SKIP:${PN} += "already-stripped"
FILES:${PN}-dev += " ${includedir}/onnxruntime/*"
