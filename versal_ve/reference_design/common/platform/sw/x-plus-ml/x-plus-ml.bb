DESCRIPTION = "reciepe for x_plus_ml_app"
LICENSE = "MIT"
LOCAL_DIR="${TOPDIR}/../../../../../../examples"
SRC_URI = "file://${LOCAL_DIR}/x_plus_ml"
S = "${WORKDIR}/x_plus_ml"
do_unpack() {
        cp -rf ${LOCAL_DIR}/x_plus_ml/* ${S}/
        cp -rf ${LOCAL_DIR}/python_examples/yolox/yolox_npu_runner.py ${S}/
        cp -rf ${LOCAL_DIR}/python_examples/yolox/yolox_postprocess.py ${S}/
}

LIC_FILES_CHKSUM = "file://${LOCAL_DIR}/x_plus_ml/LICENSE;md5=fb28ea67063d1e1864f9cbebf9aa1291"

DEPENDS = "vartml-sw vart"
inherit pkgconfig
RDEPENDS:${PN} += "libvart_ml_runner.so()(64bit)"
TARGET_CPPFLAGS:append = " -I=/usr/include/xrt"
GIR_MESON_ENABLE_FLAG = "enabled"
GIR_MESON_DISABLE_FLAG = "disabled"
do_install() {
        install -d ${D}/${bindir}
        install -d ${D}/${sysconfdir}/vai
        install -d ${D}/${sysconfdir}/vai/json-config
        install -d ${D}/${sysconfdir}/vai/json-config/func-jsons
        install -m 0755 ${S}/json-config/*.json ${D}/${sysconfdir}/vai/json-config
        install -m 0755 ${S}/json-config/func-jsons/*.json ${D}/${sysconfdir}/vai/json-config/func-jsons
        install ${S}/x_plus_ml_app ${D}${bindir}
        install ${S}/yolox_npu_runner.py ${D}${bindir}
        install ${S}/yolox_postprocess.py ${D}${bindir}
        install -d ${D}${libdir}
        install -m 0755 ${S}/postprocess/libpostprocess_resnet50-1.0.so ${D}${libdir}
        install -m 0755 ${S}/postprocess/libpostprocess_yolo-1.0.so ${D}${libdir}
        install -d ${D}/${sysconfdir}/vai/labels
        install -m 0755 ${S}/postprocess/label_files/resnet50_labels.txt ${D}${sysconfdir}/vai/labels
        install -m 0755 ${S}/postprocess/label_files/ssdresnet34_labels.txt ${D}${sysconfdir}/vai/labels
        install -m 0755 ${S}/postprocess/label_files/yolo_labels.txt ${D}${sysconfdir}/vai/labels
}
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so file-rdeps"
FILES:${PN} += "${bindir}/x_plus_ml_app"
FILES:${PN} += "${libdir}/libpostprocess_resnet50-1.0.so"
FILES:${PN} += "${libdir}/libpostprocess_yolo-1.0.so"
FILES:${PN} += "${sysconfdir}/vai/labels/*.txt"
