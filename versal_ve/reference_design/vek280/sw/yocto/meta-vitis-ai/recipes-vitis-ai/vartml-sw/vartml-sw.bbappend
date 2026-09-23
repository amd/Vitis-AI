FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

do_install:append() {
        install -d ${D}/${sysconfdir}/vai
        install -d ${D}/${sysconfdir}/vai/bitstream
        install  ${THISDIR}/${PN}/fpga_info_* ${D}/${sysconfdir}/vai/bitstream
}
