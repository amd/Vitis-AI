SUMMARY = "vart-ml native libs and headers from wheel"
LICENSE = "CLOSED"

S = "${WORKDIR}"
LOCAL_DIR = "${WORKDIR}/wheels"
PYPI_AMD_VAI_INDEX = "https://pypi.amd.com/vai/6.3/simple"
VART_ML_WHEEL_CACHE = "${DL_DIR}/vart-ml-wheels"

inherit python3native

DEPENDS += "python3-pip-native unzip-native"
do_install[depends] += "unzip-native:do_populate_sysroot"
INSANE_SKIP:pn-vart-ml = "all"

do_package_qa[noexec] = "1"

EXCLUDE_FROM_SHLIBS = "1"

do_fetch[network] = "1"
do_fetch[depends] += "python3-pip-native:do_populate_sysroot"

do_fetch() {
    PIP="${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip"
    install -d "${VART_ML_WHEEL_CACHE}"
    if ! ls ${VART_ML_WHEEL_CACHE}/vart_ml*.whl >/dev/null 2>&1; then
        bbnote "Downloading vart-ml wheel from ${PYPI_AMD_VAI_INDEX}"
        ${PIP} download --no-deps --no-cache-dir \
            --index-url "${PYPI_AMD_VAI_INDEX}" \
            -d "${VART_ML_WHEEL_CACHE}" \
            vart-ml
    else
        bbnote "Using cached vart-ml wheel from ${VART_ML_WHEEL_CACHE}"
    fi

    if ! ls ${VART_ML_WHEEL_CACHE}/vart_ml*.whl >/dev/null 2>&1; then
        bbfatal "Failed to download vart-ml wheel from ${PYPI_AMD_VAI_INDEX}"
    fi
}

do_configure() {
    install -d "${LOCAL_DIR}"
    find "${LOCAL_DIR}" -type f -name "*.whl" -exec rm -f {} \;
    cp ${VART_ML_WHEEL_CACHE}/vart_ml*.whl ${LOCAL_DIR}/
    bbnote "Using vart-ml wheel: $(ls ${LOCAL_DIR}/*.whl)"
}

do_install() {

    ${STAGING_BINDIR_NATIVE}/unzip -q -o \
        ${WORKDIR}/wheels/vart_ml*.whl -d ${WORKDIR}/
    # Install the wheel into rootfs site-packages (python3.X dir auto-selected via PYTHON_SITEPACKAGES_DIR).
    install -d ${D}${PYTHON_SITEPACKAGES_DIR}
    for whl in ${WORKDIR}/wheels/vart_ml*.whl; do
      bbnote "Installing wheel: $whl"
      ${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip install --no-deps \
        --prefix=${D}/usr "$whl"
    done

    PKG_DIR="${D}${PYTHON_SITEPACKAGES_DIR}/vart_ml"
    if [ ! -d "$PKG_DIR" ]; then
        bbfatal "vart_ml package not found in site-packages after wheel install"
    fi

    # Symlink vart-ml.pc onto the default pkg-config path (no PKG_CONFIG_PATH needed).
    if [ -f "$PKG_DIR/lib/pkgconfig/vart-ml.pc" ]; then
        install -d ${D}${libdir}/pkgconfig
        vart_ml_pc_rel=$(realpath -m --relative-to="${D}${libdir}/pkgconfig" \
            "$PKG_DIR/lib/pkgconfig/vart-ml.pc")
        ln -sf "$vart_ml_pc_rel" ${D}${libdir}/pkgconfig/vart-ml.pc
    fi
}
SOLIBS = ".so"
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so already-stripped"
FILES:${PN} = "${PYTHON_SITEPACKAGES_DIR}/vart_ml ${PYTHON_SITEPACKAGES_DIR}/vart_ml-*"
FILES:${PN}-dev += "${libdir}/pkgconfig/*.pc"
