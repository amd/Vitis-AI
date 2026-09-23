SUMMARY = "Configure uio_pdrv_genirq for NPU UIO devices"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://uio-npu.conf \
           file://uio-npu-load.conf"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/modprobe.d
    install -m 0644 ${WORKDIR}/uio-npu.conf ${D}${sysconfdir}/modprobe.d/uio-npu.conf

    install -d ${D}${sysconfdir}/modules-load.d
    install -m 0644 ${WORKDIR}/uio-npu-load.conf ${D}${sysconfdir}/modules-load.d/uio-npu.conf
}

FILES:${PN} = "${sysconfdir}/modprobe.d/uio-npu.conf \
               ${sysconfdir}/modules-load.d/uio-npu.conf"
