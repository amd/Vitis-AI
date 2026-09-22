DESCRIPTION = "VAI Profile collect-data script for XDP profiling"
LICENSE = "Apache-2.0"

require ${THISDIR}/../mldebugger/mldebugger-git.inc

SRC_URI = "${MLDEBUGGER_GIT_URI}"
SRCREV = "d433522409805bfd139896a83110e7f736fcc866"

S = "${WORKDIR}/git"
LIC_FILES_CHKSUM = "file://${S}/LICENSE;md5=339c1da88443bff3a1b84d59a9bdefa6"

inherit allarch

RDEPENDS:${PN} = "zip"

do_compile[noexec] = "1"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/src/mldebug/scripts/vaiprofile-collect-data ${D}${bindir}/vaiprofile-collect-data
}

FILES:${PN} = "${bindir}/vaiprofile-collect-data"
