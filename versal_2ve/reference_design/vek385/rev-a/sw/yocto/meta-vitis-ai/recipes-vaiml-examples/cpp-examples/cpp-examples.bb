DESCRIPTION = "reciepe for X+ML CPP applications"
LICENSE = "Apache-2.0"

SRC_URI = "git://github.com/amd/Vitis-AI.git;branch=main;protocol=https"
SRCREV = "d85190a0ab80b1275f974dfddb7602c1be49c67a"
S = "${WORKDIR}/git/versal_2ve/examples/cpp_examples"
LIC_FILES_CHKSUM = "file://${S}/LICENSE;md5=8ca1557542e93162af35eedb71a8e499"

DEPENDS = " vart-ml vart-x ryzenai-wheels cmake-native nlohmann-json"
RDEPENDS:${PN} = "vart-x vart-ml ryzenai-wheels"

inherit pkgconfig
# llm_tokenizer and llm_sampler are CMake projects driven by wrapper Makefiles
# that hardcode empty linker flags after $(CMAKE_CONFIG_OPTS), so the values
# cannot be overridden from the make command line. Rewrite them to the Yocto
# LDFLAGS, otherwise the shared libraries link without --hash-style=gnu and
# do_package_qa fails with a missing GNU_HASH. The stale build/ directory is
# removed as well, since the wrappers only run cmake when build/Makefile is
# absent and would otherwise reuse the cached flags from an earlier run.
CMAKE_WRAPPER_DIRS = "common/utils/llm_tokenizer common/utils/llm_sampler"

do_compile:prepend() {
    for d in ${CMAKE_WRAPPER_DIRS}; do
        [ -f "${S}/$d/Makefile" ] || continue
        sed -i \
            -e 's|-DCMAKE_SHARED_LINKER_FLAGS=""|-DCMAKE_SHARED_LINKER_FLAGS="$(LDFLAGS)"|g' \
            -e 's|-DCMAKE_EXE_LINKER_FLAGS=""|-DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)"|g' \
            -e 's|-DCMAKE_MODULE_LINKER_FLAGS=""|-DCMAKE_MODULE_LINKER_FLAGS="$(LDFLAGS)"|g' \
            "${S}/$d/Makefile"
        if grep -q 'LINKER_FLAGS=""' "${S}/$d/Makefile"; then
            bbfatal "$d/Makefile still clears the linker flags; the rewrite no longer matches upstream."
        fi
        rm -rf "${S}/$d/build" "${S}/$d/lib"
    done
}
# CMAKE_FLAGS feeds CMAKE_CXX_FLAGS in both wrappers; without it the cross
# --sysroot is dropped and nlohmann/json.hpp is not found.
do_compile() {
    oe_runmake all CMAKE_FLAGS="${CXXFLAGS}"
}

do_install() {
  install -d ${D}${libdir}
  install -d ${D}${bindir}
  INSTALL_DIR=${S}/install
  cp -rL ${INSTALL_DIR}/* ${D}/
  rm -f ${D}/applications.tar.gz
  install -d ${D}${sysconfdir}/vai/common/utils
  install -d ${D}${sysconfdir}/vai/python
  install -m 0755 ${S}/common/utility_timer/lib/libutility_timer.so ${D}${libdir}
  install -m 0644 ${S}/common/utils/*.py ${D}${sysconfdir}/vai/common/utils/
  install -m 0644 ${S}/../python_examples/*.py ${D}${sysconfdir}/vai/python/
}
FILES_SOLIBSDEV = ""
INSANE_SKIP:${PN} += "dev-so"

FILES:${PN} = "\
    ${bindir} \
    ${libdir} \
    ${sysconfdir} \
"
FILES:${PN}-dev = "\
    ${includedir} \
    ${libdir}/pkgconfig \
"
