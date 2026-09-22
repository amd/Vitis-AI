require recipes-xrt/xrt/xdna-driver.inc
require recipes-xrt/xrt/vek385-aie-variant.inc

SUMMARY = "Xilinx Runtime(XRT) driver module"
DESCRIPTION = "Xilinx Runtime driver module provides aie management and compute unit schedule"

S = "${WORKDIR}/git/src/driver/amdxdna"
LICENSE = "GPL-2.0-or-later & Apache-2.0"
LIC_FILES_CHKSUM = "file://../../../LICENSE.amdnpu;md5=ea42c0f38f2d42aad08bd50c822460dc"

COMPATIBLE_MACHINE = "zynqmp|versal|versal2|versal-2ve-2vm"

PREFERRED_PROVIDER_virtual/opencl-icd ??= "opencl-icd-loader"
PACKAGECONFIG ??= "${PREFERRED_PROVIDER_virtual/opencl-icd}"
PACKAGECONFIG[ocl-icd] = ",,ocl-icd,ocl-icd"
PACKAGECONFIG[opencl-icd-loader] = ",,opencl-icd-loader,opencl-icd-loader"

DEPENDS = "libdrm opencl-headers virtual/opencl-icd opencl-clhpp boost util-linux git-replacement-native protobuf-native protobuf elfutils libffi rapidjson systemtap libdfx"
RDEPENDS:${PN} = "libdrm bash boost-system boost-filesystem systemtap"
#Driver version of XDNA. We need to update this manually when the XRT submodule is updated.
XDNA_DRIVER_VERSION = "2.23.0"
EXTRA_OEMAKE += "XDNA_DRIVER_VERSION=${XDNA_DRIVER_VERSION}"
TARGET_CXXFLAGS:append = "${@bb.utils.contains('PACKAGECONFIG', 'opencl-icd-loader', ' -DOPENCL_ICD_LOADER=on', '', d)}"

# AIE variant toggle (see xrt_%.bbappend for full docs):
#   ve2 (default) -> XDNA_BUS_TYPE=of     (VE2 / xilinx-aie path)
#   NPU firmware path -> XDNA_BUS_TYPE=npu_of (see vek385_aie_has_npu_fw).
VEK385_AIE_VARIANT ??= "ve2"
EXTRA_OEMAKE += "XDNA_BUS_TYPE=${@'npu_of' if vek385_aie_has_npu_fw(d) else 'of'}"

inherit module

MODULES_MODULE_SYMVERS_LOCATION = "build/driver/amdxdna"

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/
    install -m 0644 ${S}/build/driver/amdxdna/amdxdna.ko ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/
}
MODULE_NAME = "amdxdna"
KERNEL_MODULE_AUTOLOAD += "amdxdna"
