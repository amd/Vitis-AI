SUMMARY = "VEK385 board setup scripts and systemd service"
DESCRIPTION = "Installs on-target setup scripts (overlay programming, runtime \
environment, UFS configuration) and a systemd service that runs them \
automatically on boot. The modified setup_overlay.sh supports FAT32 \
staging for Windows-flashed SD cards."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://setup_overlay.sh \
    file://runtime_env.sh \
    file://configure_ufs.sh \
    file://ufsconfig_64gb \
    file://vek385-setup.service \
"

RDEPENDS:${PN} = "bash"

inherit systemd

SYSTEMD_SERVICE:${PN} = "vek385-setup.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    # Overlay scripts
    install -d ${D}/overlay
    install -m 0755 ${WORKDIR}/setup_overlay.sh ${D}/overlay/
    install -m 0755 ${WORKDIR}/runtime_env.sh ${D}/overlay/

    # UFS configuration
    install -d ${D}${sysconfdir}/ufs_config
    install -m 0755 ${WORKDIR}/configure_ufs.sh ${D}${sysconfdir}/ufs_config/
    install -m 0644 ${WORKDIR}/ufsconfig_64gb ${D}${sysconfdir}/ufs_config/

    # Systemd service
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/vek385-setup.service ${D}${systemd_system_unitdir}/

    # Runtime environment variables for login shells
    install -d ${D}${sysconfdir}/profile.d
    grep '^export ' ${WORKDIR}/runtime_env.sh > ${D}${sysconfdir}/profile.d/vek385-runtime.sh || true
    chmod 0644 ${D}${sysconfdir}/profile.d/vek385-runtime.sh

    # Same paths for the dynamic linker. profile.d only covers login shells, so
    # non-login shells, "sudo su" and systemd services would otherwise fail to
    # resolve the VAI libraries -- sudo strips LD_LIBRARY_PATH unconditionally.
    install -d ${D}${sysconfdir}/ld.so.conf.d
    sed -n 's/^export LD_LIBRARY_PATH=//p' ${WORKDIR}/runtime_env.sh \
        | tr ':' '\n' | sed '/^$/d' \
        > ${D}${sysconfdir}/ld.so.conf.d/vai-runtime.conf
    chmod 0644 ${D}${sysconfdir}/ld.so.conf.d/vai-runtime.conf
    if [ ! -s ${D}${sysconfdir}/ld.so.conf.d/vai-runtime.conf ]; then
        bbfatal "runtime_env.sh exports no LD_LIBRARY_PATH"
    fi
}

FILES:${PN} += " \
    /overlay \
    ${sysconfdir}/ufs_config \
    ${sysconfdir}/profile.d/vek385-runtime.sh \
    ${sysconfdir}/ld.so.conf.d/vai-runtime.conf \
"
