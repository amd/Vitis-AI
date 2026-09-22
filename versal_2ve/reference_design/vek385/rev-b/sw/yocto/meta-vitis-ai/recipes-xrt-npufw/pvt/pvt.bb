DESCRIPTION = "Platform validation test data"
LICENSE = "CLOSED"

INHIBIT_LICENSE_CHECK = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INSANE_SKIP:${PN} += "arch staticdev"

RDEPENDS:${PN} += "bash"

PV = "1.0+git"
SRC_URI = "git://github.com/Xilinx/VTD.git;protocol=https;nobranch=1;lfs=0"
FETCHCMD_git = "git -c gc.autoDetach=false -c core.pager=cat -c safe.bareRepository=all -c clone.defaultRemoteName=origin -c filter.lfs.process= -c filter.lfs.smudge= -c filter.lfs.clean= -c filter.lfs.required=false"

SRCREV = "5bb66d5114729237e7003ad78a3b586bbeb796b1"

S = "${WORKDIR}/git"

INHIBIT_PACKAGE_STRIP_FILES = " \
    ${PKGD}${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ctrl.elf \
    ${PKGD}${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/ctrl.elf \
    ${PKGD}${datadir}/amdxdna/local_shim_test_data/npu3/vadd/vadd.elf \
    ${PKGD}${datadir}/amdxdna/local_shim_test_data/npu3/nop/nop.elf \
"

do_install () {
	install -d ${D}${datadir}/amdxdna/bins/t50
	cp -a ${S}/archive/ve2/t50/. ${D}${datadir}/amdxdna/bins/t50/

	chmod 0755 ${D}${datadir}/amdxdna/bins/t50/platform_validation.sh
	chmod 0755 ${D}${datadir}/amdxdna/bins/t50/ps_aie_connections/ps_aie_connections.sh
	chmod 0755 ${D}${datadir}/amdxdna/bins/t50/ps_aie_cert_wakeup/ps_aie_cert_wakeup.sh

	for d in ps_aie_ddr_connections shim_dma_bandwidth; do
		if [ -f "${D}${datadir}/amdxdna/bins/t50/$d/ctrl.elf" ]; then
			chmod 0644 "${D}${datadir}/amdxdna/bins/t50/$d/ctrl.elf"
		fi
	done

	install -m 0644 ${S}/archive/ve2/xrt_smi_ve2.a ${D}${datadir}/amdxdna/bins/

	install -d ${D}${datadir}/amdxdna/local_shim_test_data/npu3/vadd
	install -m 0644 ${S}/archive/ve2/vadd/vadd.elf \
		${D}${datadir}/amdxdna/local_shim_test_data/npu3/vadd/vadd.elf

	install -d ${D}${datadir}/amdxdna/local_shim_test_data/npu3/nop
	install -m 0644 ${S}/archive/ve2/latency/nop.elf \
		${D}${datadir}/amdxdna/local_shim_test_data/npu3/nop/nop.elf

	ln -sfn npu3 ${D}${datadir}/amdxdna/local_shim_test_data/npu3b

	chown -hR root:root ${D}${datadir}/amdxdna
}

pkg_postinst:${PN} () {
	if [ -f "${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ctrl.elf" ]; then
		chmod 0755 "${datadir}/amdxdna/bins/t50/ps_aie_ddr_connections/ctrl.elf"
	fi
	if [ -f "${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/ctrl.elf" ]; then
		chmod 0755 "${datadir}/amdxdna/bins/t50/shim_dma_bandwidth/ctrl.elf"
	fi
}

FILES:${PN} = "${datadir}/amdxdna/bins/ ${datadir}/amdxdna/local_shim_test_data/"
