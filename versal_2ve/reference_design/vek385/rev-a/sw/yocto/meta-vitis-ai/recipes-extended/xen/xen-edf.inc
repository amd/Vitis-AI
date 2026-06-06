FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "\
    file://versal-xen-efi-config \
    file://versal-2ve-2vm-xen-efi-config \
    file://enable-overlay.cfg \
    "

# Any machine or soc specific override requires a different package_arch
PACKAGE_ARCH:versal = "${MACHINE_ARCH}"
PACKAGE_ARCH:versal-2ve-2vm = "${MACHINE_ARCH}"

do_install:append:versal() {
    install -m 0644 ${WORKDIR}/versal-xen-efi-config ${D}/boot/xen.cfg
}

do_install:append:versal-2ve-2vm() {
    install -m 0644 ${WORKDIR}/versal-2ve-2vm-xen-efi-config ${D}/boot/xen.cfg
}

FILES:${PN}-efi += "/boot/xen.cfg"

do_deploy:append:versal() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${WORKDIR}/versal-xen-efi-config ${DEPLOYDIR}/xen.cfg
}

do_deploy:append:versal-2ve-2vm() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${WORKDIR}/versal-2ve-2vm-xen-efi-config ${DEPLOYDIR}/xen.cfg
}
