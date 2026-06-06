DESCRIPTION = "reciepe for X+ML CPP applications"
LICENSE = "Apache-2.0"

SRC_URI = "git://github.com/amd/Vitis-AI.git;branch=main;protocol=https"
SRCREV = "c84f1e6b30f705b3b29e486a66f329d18184ca4f"
S = "${WORKDIR}/git/versal_2ve/examples/cpp_examples"
LIC_FILES_CHKSUM = "file://${S}/LICENSE;md5=8ca1557542e93162af35eedb71a8e499"

DEPENDS = "vart-ml vart-x ryzenai-wheels"
#RDEPENDS:${PN} += "libonnxruntime.so()(64bit)"
RDEPENDS:${PN} = "vart-x vart-ml ryzenai-wheels"

inherit pkgconfig
do_compile() {
    oe_runmake all
}

do_install() {
  install -d ${D}${libdir}
  install -d ${D}${bindir}
  INSTALL_DIR=${S}/install
  cp -rL ${INSTALL_DIR}/* ${D}/
  rm -f ${D}/applications.tar.gz
  install -d ${D}${sysconfdir}/vai/common/utils
  install -d ${D}${sysconfdir}/vai/python
  install -m 0755 ${S}/common/utility_timer/lib/libutility_timer.so ${D}${libdir}
  install -m 0644 ${S}/common/utils/*.py ${D}${sysconfdir}/vai/common/utils/
  install -m 0644 ${S}/../python_examples/*.py ${D}${sysconfdir}/vai/python/
}
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so"

FILES:${PN} = "\
    ${bindir} \
    ${libdir} \
    ${sysconfdir} \
"
FILES:${PN}-dev = "\
    ${includedir} \
    ${libdir}/pkgconfig \
"
