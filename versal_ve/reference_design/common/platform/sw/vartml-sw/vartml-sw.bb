DESCRIPTION = "vart_ml sw stack receipe"
LOCAL_DIR = "${TOPDIR}/../../../../../../src"
SRC_URI = "file://${LOCAL_DIR}/vart_ml"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${LOCAL_DIR}/vart_ml/LICENSE;md5=fb28ea67063d1e1864f9cbebf9aa1291"
inherit pkgconfig

DEPENDS = "xrt opencv python3 python3-pybind11 onnxruntime"
RDEPENDS:${PN} = "python3-core"

do_unpack() {
        cp -rf ${LOCAL_DIR}/vart_ml ${WORKDIR}
}
S = "${WORKDIR}/vart_ml"

# Set OECORE_TARGET_ARCH environment variable
do_compile:prepend() {
    export OECORE_TARGET_ARCH=aarch64
}

# Disable parallel make
PARALLEL_MAKE = ""

do_install() {
        install -d ${D}/${libdir}
        install -d ${D}/${libdir}/pkgconfig
        install -d ${D}/${includedir}/npu_runner
        install -d ${D}/${includedir}/utils
        install -d ${D}/${includedir}/io
        install -d ${D}/${includedir}/onnx_runner
        install -d ${D}/${includedir}/vart_ml_runner
        install  ${S}/utils/*.so ${D}${libdir}/
        install  ${S}/io/*.so ${D}${libdir}/
        install  ${S}/npu_runner/*.so ${D}${libdir}/
        install  ${S}/onnx_runner/*.so ${D}${libdir}/
        install  ${S}/vart_ml_runner/*.so ${D}${libdir}/
        install  ${S}/utils/*.h ${D}${includedir}/utils
        install  ${S}/utils/*.inc ${D}${includedir}/utils
        install  ${S}/io/*.h ${D}${includedir}/io
        install  ${S}/npu_runner/*.h ${D}${includedir}/npu_runner
        install  ${S}/onnx_runner/*.h ${D}${includedir}/onnx_runner
        install  ${S}/vart_ml_runner/*.hpp ${D}${includedir}/vart_ml_runner
        install -d ${D}/${bindir}
        install -m 0755 ${S}/demo/vart_ml_runner.py ${D}/${bindir}
        find ${S}/demo -type f -executable -exec install -m 0755 {} ${D}/${bindir}/ \;
        install ${S}/vart_ml_tools/vart_ml_tools ${D}/${bindir}
        install -d ${D}/${sysconfdir}/vai
        install -d ${D}/${sysconfdir}/vai/lib
        install -d ${D}/${sysconfdir}/vai/labels
        cp ${S}/vai.sh ${D}${sysconfdir}/
        ${LOCAL_DIR}/vart_ml/update_vai.sh ${D}${sysconfdir}/vai.sh
        cp ${S}/xrt.ini ${D}${sysconfdir}/vai/
        cp -R ${S}/lib/* ${D}${sysconfdir}/vai/lib/
        cp -R ${S}/demo/labels ${D}/${sysconfdir}/vai/labels/
        sed -e 's|@prefix@|${prefix}|g' \
            -e 's|@exec_prefix@|${exec_prefix}|g' \
            -e 's|@libdir@|${libdir}|g' \
            -e 's|@includedir@|${includedir}|g' \
            -e 's|@version@|1.0|g' \
            ${S}/vartml-sw.pc.in > ${D}/${libdir}/pkgconfig/vartml-sw.pc
}
PROVIDES = "libnpu_runner"
PROVIDES += "libio"
RPROVIDES:${PN} += "libnpu_runner.so()(64bit)"
RPROVIDES:${PN} += " libio.so()(64bit)"
RPROVIDES:${PN} += " libonnx_runner.so()(64bit)"
RPROVIDES:${PN} += " libvart_ml_runner.so()(64bit)"
FILES_SOLIBSDEV = ""
FILES:${PN} += " ${libdir}/libutils.so"
FILES:${PN} += " ${libdir}/libio.so"
FILES:${PN} += " ${libdir}/libuio.so"
FILES:${PN} += " ${libdir}/libdevmem.so"
FILES:${PN} += " ${libdir}/libxrt.so"
FILES:${PN} += " ${libdir}/libonnx_runner.so"
FILES:${PN} += " ${libdir}/libnpu_runner.so"
FILES:${PN} += " ${libdir}/libvart_ml_runner.so"
FILES:${PN} += " ${sysconfdir}/lib/*"
INSANE_SKIP:${PN} += "dev-so"
FILES:${PN}-dev += " ${includedir}/npu_runner/* ${includedir}/io/* ${includedir}/utils/* ${libdir}/pkgconfig/*.pc"
