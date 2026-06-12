SUMMARY = "vart-ml native libs and headers from wheel"
LICENSE = "CLOSED"

S = "${WORKDIR}"
LOCAL_DIR = "${WORKDIR}/wheels"
PYPI_AMD_VAI_INDEX = "https://pypi.amd.com/vai/6.2/simple"

inherit python3native

DEPENDS += "python3-pip-native unzip-native"
do_install[depends] += "unzip-native:do_populate_sysroot"
INSANE_SKIP:pn-vart-ml = "all"

# Or disable do_package_qa entirely:
do_package_qa[noexec] = "1"

EXCLUDE_FROM_SHLIBS = "1"

do_configure[network] = "1"

do_configure() {
    PIP="${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip"

    install -d "${LOCAL_DIR}"
    find "${LOCAL_DIR}" -type f -name "*.whl" -exec rm -f {} \;

    bbnote "Downloading vart-ml wheel from ${PYPI_AMD_VAI_INDEX}"
    ${PIP} download --no-deps --no-cache-dir --extra-index-url "${PYPI_AMD_VAI_INDEX}" \
        -d "${LOCAL_DIR}" \
        vart-ml

    if ! ls ${LOCAL_DIR}/*.whl >/dev/null 2>&1; then
        bberror "No vart-ml wheel found in ${LOCAL_DIR}"
        exit 1
    fi
}

do_install() {
    install -d ${D}${includedir}
    install -d ${D}${libdir}
    install -d ${D}${bindir}

    ${STAGING_BINDIR_NATIVE}/unzip -q -o \
        ${WORKDIR}/wheels/vart_ml*.whl -d ${WORKDIR}/
    # Find extracted package dir (handles vart_ml / vart-ml)
    PKG_DIR=$(find ${WORKDIR} -maxdepth 2 -type d -name "vart_ml" | head -1)
    if [ -z "$PKG_DIR" ]; then
        bbfatal "Could not find vart package in extracted wheel"
    fi

    bbnote "Using package dir: $PKG_DIR"
    if [ -d "$PKG_DIR/include" ]; then
        cp -rf $PKG_DIR/include/* ${D}${includedir}/
    fi

    if [ -d "$PKG_DIR/lib" ]; then
       cp -rL $PKG_DIR/lib/* ${D}${libdir}/
    fi
    if [ -d "$PKG_DIR/bin" ]; then
       cp -rL $PKG_DIR/bin/* ${D}${bindir}/
    fi

    install -d ${D}${libdir}/pkgconfig
    cp -rf $PKG_DIR/lib/pkgconfig/vart-ml.pc ${D}${libdir}/pkgconfig/

}
SOLIBS = ".so"
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so"
FILES:${PN} = "${libdir}/** ${bindir}"
FILES:${PN}-dev += " ${includedir} ${libdir}/pkgconfig/*.pc"
