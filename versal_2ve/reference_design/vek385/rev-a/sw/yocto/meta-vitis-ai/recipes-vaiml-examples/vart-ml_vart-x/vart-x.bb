SUMMARY = "VART-X native libs and headers from wheel"
LICENSE = "CLOSED"

S = "${WORKDIR}"
LOCAL_DIR = "${WORKDIR}/wheels"
PYPI_AMD_VAI_INDEX = "https://pypi.amd.com/vai/6.3/simple"
VART_X_WHEEL_CACHE = "${DL_DIR}/vart-x-wheels"

inherit python3native

DEPENDS += "python3-pip-native unzip-native glib-2.0 glib-2.0-native xrt opencv protobuf jansson ne10"
do_install[depends] += "unzip-native:do_populate_sysroot"
INSANE_SKIP:pn-vart-x = "all"
do_package_qa[noexec] = "1"
EXCLUDE_FROM_SHLIBS = "1"

do_fetch[network] = "1"
do_fetch[depends] += "python3-pip-native:do_populate_sysroot"

do_fetch() {
    PIP="${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip"

    install -d "${VART_X_WHEEL_CACHE}"

    if ! ls ${VART_X_WHEEL_CACHE}/vart_x*.whl >/dev/null 2>&1; then
        bbnote "Downloading vart-x wheel from ${PYPI_AMD_VAI_INDEX}"
        ${PIP} download --no-deps --no-cache-dir \
            --index-url "${PYPI_AMD_VAI_INDEX}" \
            -d "${VART_X_WHEEL_CACHE}" \
            vart-x
    else
        bbnote "Using cached vart-x wheel from ${VART_X_WHEEL_CACHE}"
    fi

    if ! ls ${VART_X_WHEEL_CACHE}/vart_x*.whl >/dev/null 2>&1; then
        bbfatal "Failed to download vart-x wheel from ${PYPI_AMD_VAI_INDEX}"
    fi
}

do_configure() {
    install -d "${LOCAL_DIR}"
    find "${LOCAL_DIR}" -type f -name "*.whl" -exec rm -f {} \;
    cp ${VART_X_WHEEL_CACHE}/vart_x*.whl ${LOCAL_DIR}/
    bbnote "Using vart-x wheel: $(ls ${LOCAL_DIR}/*.whl)"
}

do_install() {

  ${STAGING_BINDIR_NATIVE}/unzip -q -o \
        ${WORKDIR}/wheels/vart_x*.whl -d ${WORKDIR}/
# Install the wheel into rootfs site-packages (python3.X dir auto-selected via PYTHON_SITEPACKAGES_DIR).
  install -d ${D}${PYTHON_SITEPACKAGES_DIR}
  for whl in ${WORKDIR}/wheels/vart_x*.whl; do
    bbnote "Installing wheel: $whl"
    ${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip install --no-deps \
      --prefix=${D}/usr "$whl"
  done

  PKG_DIR="${D}${PYTHON_SITEPACKAGES_DIR}/vart_x"
  if [ ! -d "$PKG_DIR" ]; then
      bbfatal "vart_x package not found in site-packages after wheel install"
  fi

  # Labels go to /etc/vai/labels (referenced by post-process configs); drop the
  # wheel copy to keep a single copy.
  if [ -d "$PKG_DIR/labels" ]; then
     install -d ${D}${sysconfdir}/vai/labels
     cp -rf $PKG_DIR/labels/* ${D}${sysconfdir}/vai/labels/
     rm -rf $PKG_DIR/labels
  fi

  # Symlink .pc onto the default pkg-config path (no PKG_CONFIG_PATH needed).
  install -d ${D}${libdir}/pkgconfig
  for pc in vart-x vvas-core; do
    PC="$PKG_DIR/lib/pkgconfig/${pc}.pc"
    if [ -f "$PC" ]; then
      pc_rel=$(realpath -m --relative-to="${D}${libdir}/pkgconfig" "$PC")
      ln -sf "$pc_rel" ${D}${libdir}/pkgconfig/${pc}.pc
    fi
  done
}

SOLIBS = ".so"
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so already-stripped"
# Board + SDK: everything under site-packages (nothing in /usr); labels at /etc/vai.
FILES:${PN} = "${PYTHON_SITEPACKAGES_DIR}/vart_x ${PYTHON_SITEPACKAGES_DIR}/vart_x-* ${sysconfdir}/vai/labels/**"
# SDK only: expose the site-packages .pc on the default pkg-config search path.
FILES:${PN}-dev += "${libdir}/pkgconfig/vart-x.pc ${libdir}/pkgconfig/vvas-core.pc"
