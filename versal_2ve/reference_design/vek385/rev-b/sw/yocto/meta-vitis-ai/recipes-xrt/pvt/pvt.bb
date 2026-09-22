DESCRIPTION = "Platform validation test data"
LICENSE = "CLOSED"

INHIBIT_LICENSE_CHECK = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INSANE_SKIP:${PN} += "arch staticdev"

RDEPENDS:${PN} += "bash"

PV = "1.0+git"
SRC_URI = "git://github.com/Xilinx/VTD.git;protocol=https;nobranch=1;lfs=0;subpath=archive/ve2;destsuffix=git/archive/ve2"
SRCREV = "84bd906fd8b1b8c2982d58ac380bf17220dc3a64"

S = "${WORKDIR}/git"

INHIBIT_PACKAGE_STRIP_FILES = " \
    ${PKGD}${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ps_aie_ddr_ctrl.elf \
    ${PKGD}${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/shim_dma_ctrl.elf \
    ${PKGD}${datadir}/amdxdna/local_shim_test_data/npu_ve2/vadd/vadd.elf \
    ${PKGD}${datadir}/amdxdna/local_shim_test_data/npu_ve2/nop/nop.elf \
"

do_install () {
	for tier in t50 t20 t10; do
		install -d ${D}${datadir}/amdxdna/bins/${tier}
		cp -a ${S}/archive/ve2/${tier}/. ${D}${datadir}/amdxdna/bins/${tier}/
	done

	chmod 0755 ${D}${datadir}/amdxdna/bins/t50/platform_validation.sh
	chmod 0755 ${D}${datadir}/amdxdna/bins/t50/ps_aie_connections/ps_aie_connections.sh
	chmod 0755 ${D}${datadir}/amdxdna/bins/t50/ps_aie_cert_wakeup/ps_aie_cert_wakeup.sh

	if [ -f "${D}${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ps_aie_ddr_ctrl.elf" ]; then
		chmod 0644 "${D}${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ps_aie_ddr_ctrl.elf"
	fi
	if [ -f "${D}${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/shim_dma_ctrl.elf" ]; then
		chmod 0644 "${D}${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/shim_dma_ctrl.elf"
	fi

	# npu_ve2 shim_test full-ELF data (vadd + ifm/ofm/wts bins, nop).
	# shim_test resolves these under <bindir>/../local_shim_test_data via a
	# symlink created by xrt.
	install -d ${D}${datadir}/amdxdna/local_shim_test_data/npu_ve2
	cp -a ${S}/archive/ve2/t50/vadd ${D}${datadir}/amdxdna/local_shim_test_data/npu_ve2/vadd
	cp -a ${S}/archive/ve2/nop  ${D}${datadir}/amdxdna/local_shim_test_data/npu_ve2/nop

	chown -hR root:root ${D}${datadir}/amdxdna
}

pkg_postinst:${PN} () {
	if [ -f "${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ps_aie_ddr_ctrl.elf" ]; then
		chmod 0755 "${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ps_aie_ddr_ctrl.elf"
	fi
	if [ -f "${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/shim_dma_ctrl.elf" ]; then
		chmod 0755 "${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/shim_dma_ctrl.elf"
	fi
}

FILES:${PN} = "${datadir}/amdxdna/"
