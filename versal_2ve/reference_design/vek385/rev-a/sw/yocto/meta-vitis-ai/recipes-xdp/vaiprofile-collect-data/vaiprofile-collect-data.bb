DESCRIPTION = "VAI Profile collect-data script for XDP profiling"
LICENSE = "Apache-2.0"

SRC_URI = "git://github.com/Xilinx/MLDebugger.git;branch=vai_6_2;protocol=https;lfs=0"
SRCREV = "35ab0a57095fa67604e123800ebe3e42e2712fef"
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
