DESCRIPTION = "Install .whl files into the rootfs"
LICENSE = "Apache-2.0 & MIT"
SRC_URI += "file://onnxruntime.pc.in"
SRC_URI += "file://LICENSE.voe"
SRC_URI += "file://LICENSE.onnxruntime"

LIC_FILES_CHKSUM = "\
    file://LICENSE.voe;md5=af44a4e9483dc26b49d5708a4d8c19eb \
    file://LICENSE.onnxruntime;md5=0f7e3b1308cb5c00b372a6e78835732d \
"

S = "${WORKDIR}"
LOCAL_DIR="${WORKDIR}/wheels"
PYPI_AMD_VAI_INDEX = "https://pypi.amd.com/vai/6.2/simple"

inherit python3native

DEPENDS += "python3 glog python3-pip-native python3-setuptools-native  opencv jansson"

# Skip *all* QA checks for this package name
INSANE_SKIP:pn-ryzenai-wheels = "all"

# Or disable do_package_qa entirely:
do_package_qa[noexec] = "1"

RDEPENDS:${PN} = ""
EXCLUDE_FROM_SHLIBS = "1"

INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"


do_configure[network] = "1"

do_configure() {
  PIP="${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip"

  install -d "${LOCAL_DIR}"
  find "${LOCAL_DIR}" -type f -name "*.whl" -exec rm -f {} \;

  bbnote "Downloading onnxruntime-vitisai and voe wheels from ${PYPI_AMD_VAI_INDEX}"
  ${PIP} download --no-deps --no-cache-dir --extra-index-url "${PYPI_AMD_VAI_INDEX}" \
    -d "${LOCAL_DIR}" \
    onnxruntime-vitisai voe

  bbnote "Downloading flexmlrt wheel for linux_aarch64 from ${PYPI_AMD_VAI_INDEX}"
  ${PIP} download --platform linux_aarch64 --no-deps --no-cache-dir --extra-index-url "${PYPI_AMD_VAI_INDEX}" \
    -d "${LOCAL_DIR}" \
    flexmlrt

  if ls ${LOCAL_DIR}/*.whl >/dev/null 2>&1; then
    for whl in "${LOCAL_DIR}/"*.whl; do
      new_name=$(echo "$whl" | sed 's/linux_aarch64/any/')
      if [ "$whl" != "$new_name" ]; then
        mv "$whl" "$new_name"
      fi
    done
  else
    bberror "No .whl files found in ${LOCAL_DIR}"
    exit 1
  fi
}

do_install () {
  install -d ${D}${PYTHON_SITEPACKAGES_DIR}
  for whl in ${WORKDIR}/wheels/*.whl; do
    echo "Installing wheel: $whl"
    ${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip install --no-deps \
    --prefix=${D}/usr "$whl"
  done

  # Copy FlexMLClient.h header from installed flexmlrt wheel
  if [ -f ${D}${PYTHON_SITEPACKAGES_DIR}/flexmlrt/include/FlexMLClient.h ]; then
    bbnote "Copying FlexMLClient.h from installed flexmlrt wheel"
    install -d ${D}${includedir}
    cp ${D}${PYTHON_SITEPACKAGES_DIR}/flexmlrt/include/FlexMLClient.h ${D}${includedir}/
  else
    bbwarn "FlexMLClient.h not found in installed flexmlrt wheel at ${D}${PYTHON_SITEPACKAGES_DIR}/flexmlrt/include/FlexMLClient.h"
  fi

  # Copy ONNX Runtime headers from installed wheel
  install -d ${D}${includedir}/onnxruntime/
  if [ -d ${D}${PYTHON_SITEPACKAGES_DIR}/onnxruntime/include/onnxruntime/core ]; then
    bbnote "Copying onnxruntime headers from installed wheel"
    cp -r ${D}${PYTHON_SITEPACKAGES_DIR}/onnxruntime/include/onnxruntime/core  ${D}${includedir}/onnxruntime/
  else
    bbwarn "Headers not found in installed wheel at ${D}${PYTHON_SITEPACKAGES_DIR}/onnxruntime/include/onnxruntime/core/session"
  fi

  for csh_file in $(find "${D}" -type f -name '*.csh'); do
    bbnote "Patching csh script: ${csh_file}"
    # Replace the first line if it references any csh shebang
    sed -i '1s|^#!.*csh|#!/bin/bash|' "${csh_file}"
  done

  install -d ${D}${libdir}
  # Dynamically find and copy the onnxruntime library with actual version
  # Try both onnxruntime and onnxruntime_vitisai packages
  ONNX_LIB=$(find ${D}${PYTHON_SITEPACKAGES_DIR}/onnxruntime/capi -name "libonnxruntime.so.*.*.*" -type f 2>/dev/null | head -1)
  if [ -z "$ONNX_LIB" ]; then
    ONNX_LIB=$(find ${D}${PYTHON_SITEPACKAGES_DIR}/onnxruntime_vitisai/capi -name "libonnxruntime.so.*.*.*" -type f 2>/dev/null | head -1)
  fi

  if [ -n "$ONNX_LIB" ]; then
    ONNX_VERSION=$(basename "$ONNX_LIB" | sed 's/libonnxruntime.so.//')
    bbnote "Found ONNX Runtime library version: ${ONNX_VERSION}"
    install -m 755 $(readlink -f "$ONNX_LIB") ${D}${libdir}/libonnxruntime.so.${ONNX_VERSION}
    ln -sf libonnxruntime.so.${ONNX_VERSION} ${D}${libdir}/libonnxruntime.so.1
    ln -sf libonnxruntime.so.1 ${D}${libdir}/libonnxruntime.so
  else
    bbwarn "libonnxruntime.so not found in wheel, skipping library installation"
    ONNX_VERSION="1.24.3"
  fi

     # pkg-config file
    install -d ${D}${libdir}/pkgconfig
    sed -e 's|@prefix@|${prefix}|g' \
        -e 's|@exec_prefix@|${exec_prefix}|g' \
        -e 's|@libdir@|${libdir}|g' \
        -e 's|@includedir@|${includedir}|g' \
        -e "s|@version@|${ONNX_VERSION}|g" \
        ${WORKDIR}/onnxruntime.pc.in > ${D}${libdir}/pkgconfig/onnxruntime.pc
}
INSANE_SKIP:${PN} += "already-stripped"
FILES_SOLIBSDEV = ""
# Avoid firmware going into sysroot
SYSROOT_DIRS_IGNORE += "${nonarch_base_libdir}/firmware"
FILES:${PN} += "${PYTHON_SITEPACKAGES_DIR}/*"
FILES:${PN} += "${libdir}/libonnxruntime.so*"
FILES:${PN}-dev += "${includedir}/onnxruntime/"
FILES:${PN}-dev += "${includedir}/FlexMLClient.h"
FILES:${PN}-dev += "${libdir}/libonnxruntime.so* {libdir}/pkgconfig/*.pc"
FILES:${PN}-dev += "${libdir}/libopencv*"
FILES:${PN}-dev += "${includedir}/opencv4"
FILES:${PN}-dev += "${libdir}/libjansson*"
FILES:${PN}-dev += "${includedir}/jansson.h"
