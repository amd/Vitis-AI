SUMMARY = "VVAS Example Pipeline Configs"
DESCRIPTION = "Inference and metaconvert config JSON files, label files \
(imagenet-classes-1000.txt) and example shell scripts consumed by the \
VVAS_PIPELINES regression and the VEK385 quickstart guides."
SECTION = "multimedia"
LICENSE = "Apache-2.0"

SRC_URI = "git://github.com/amd/VVAS.git;protocol=https;branch=release/6.3"
SRCREV = "1dc196ad45582ee264910968bf59b120b6234fad"
LIC_FILES_CHKSUM = "file://../LICENSE;md5=31db053139540d9d251012082be3c4f7"

S = "${WORKDIR}/git/vvas-examples"

inherit meson

# create_pfm_sw.sh exports MIPI from build.cfg into local.conf. Pass that
# platform choice into vvas-examples so MIPI camera scripts/configs are only
# installed in MIPI rootfs builds.
EXTRA_OEMESON:append = " -Dmipi-camera=${@bb.utils.contains('MIPI', '1', 'true', 'false', d)}"

# Data-only package: vvas-examples/meson.build only does install_data() of
# JSONs, label .txt and .sh scripts under /etc/vvas/. No compiled artefacts.
FILES:${PN} += " \
    ${sysconfdir}/vvas/configs/infer/ \
    ${sysconfdir}/vvas/configs/metaconvert/ \
    ${sysconfdir}/vvas/examples/ \
"

# /etc/vvas/examples/*.sh use #!/bin/bash
RDEPENDS:${PN} += "bash"
