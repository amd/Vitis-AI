DSCRIPTION = "Recipe for installing prebuilt vaiml models"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "\
    file://LICENSE.onnxruntime;md5=0f7e3b1308cb5c00b372a6e78835732d \
"
SRC_URI = "file://vaiml_models \
           file://LICENSE.onnxruntime"

S = "${WORKDIR}"

do_install() {
    # Create necessary directories if they don't already exist
    install -d ${D}/etc/vai/models
    cp -rf ${WORKDIR}/vaiml_models/* ${D}/etc/vai/models/
}

FILES_${PN} += "/etc/vai/models/**"
