SUMMARY = "VART-X native libs and headers from wheel"
LICENSE = "CLOSED"

S = "${WORKDIR}"
LOCAL_DIR = "${WORKDIR}/wheels"
PYPI_AMD_VAI_INDEX = "https://pypi.amd.com/vai/6.2/simple"

inherit python3native

DEPENDS += "python3-pip-native unzip-native glib-2.0 glib-2.0-native xrt opencv protobuf jansson ne10"
do_install[depends] += "unzip-native:do_populate_sysroot"
INSANE_SKIP:pn-vart-x = "all"
do_package_qa[noexec] = "1"
EXCLUDE_FROM_SHLIBS = "1"

do_configure[network] = "1"

do_configure() {
    PIP="${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip"

    install -d "${LOCAL_DIR}"
    find "${LOCAL_DIR}" -type f -name "*.whl" -exec rm -f {} \;

    bbnote "Downloading vart-x wheel from ${PYPI_AMD_VAI_INDEX}"
    ${PIP} download --no-deps --no-cache-dir --extra-index-url "${PYPI_AMD_VAI_INDEX}" \
        -d "${LOCAL_DIR}" \
        vart-x

    if ! ls ${LOCAL_DIR}/*.whl >/dev/null 2>&1; then
        bberror "No vart-x wheel found in ${LOCAL_DIR}"
        exit 1
    fi
}

do_install() {
  install -d ${D}${includedir}
  install -d ${D}${libdir}

  ${STAGING_BINDIR_NATIVE}/unzip -q -o \
        ${WORKDIR}/wheels/vart_x*.whl -d ${WORKDIR}/

    # Find extracted package dir (handles vart_x / vart-x)
    PKG_DIR=$(find ${WORKDIR} -maxdepth 2 -type d -name "vart_x" | head -1)

    if [ -z "$PKG_DIR" ]; then
        bbfatal "Could not find vart package in extracted wheel"
    fi

    bbnote "Using package dir: $PKG_DIR"

    if [ -d "$PKG_DIR/include" ]; then
        cp -r $PKG_DIR/include/* ${D}${includedir}/
    fi

    if [ -d "$PKG_DIR/lib" ]; then
       cp -rf $PKG_DIR/lib/* ${D}${libdir}/
    fi
}

# Runtime (rootfs)
SOLIBS = ".so"
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so"
FILES:${PN} += "${libdir}/vart/*.so  ${libdir}/vvas_core/**"

FILES:${PN}-dev += " ${includedir}/vart/* ${libdir}/pkgconfig/*.pc"

# Skip minimal QA warnings if needed
INSANE_SKIP:${PN} += "already-stripped"
