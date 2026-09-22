DESCRIPTION = "Install release_cert_ve2.elf to /lib/firmware/amdnpu"
LICENSE = "CLOSED"

INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_LICENSE_CHECK = "1"

INSANE_SKIP:${PN} += "arch"

SYSROOT_DIRS = ""
S = "${WORKDIR}"

SRC_URI += " \
    file://release_cert_ve2.elf \
    file://log6_cert_ve2.elf \
    file://log_buffer_reader.py \
    file://log_map.json \
"

do_install () {
    install -d ${D}${nonarch_base_libdir}/firmware/amdnpu

    for f in release_cert_ve2.elf log6_cert_ve2.elf log_buffer_reader.py log_map.json; do
        if [ -f ${WORKDIR}/$f ]; then
            cp ${WORKDIR}/$f ${D}${nonarch_base_libdir}/firmware/amdnpu/$f
        else
            bberror "File $f not found at expected path"
            exit 1
        fi
    done
}
# Avoid firmware going into sysroot
SYSROOT_DIRS_IGNORE += "${nonarch_base_libdir}/firmware"
FILES:${PN} += "${nonarch_base_libdir}/firmware"
