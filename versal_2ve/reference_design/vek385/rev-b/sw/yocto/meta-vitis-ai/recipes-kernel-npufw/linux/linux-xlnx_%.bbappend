# Optional local linux-xlnx tree (set USE_LOCAL_LINUX_XLNX = "1" in local.conf)
require ${@bb.utils.contains('USE_LOCAL_LINUX_XLNX', '1', 'linux-xlnx-externalsrc.inc', '', d)}

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI:append = " file://bsp_vaiml.cfg"
KERNEL_FEATURES:append = " bsp_vaiml.cfg"

SRC_URI:append = " file://0001-xilinx-ai-engine-Protect-AXI-MM-access-during-reset-.patch"
SRC_URI:append = " file://0002-xilinx-ai-engine-Make-AXI-MM-reset-lock-conditional-.patch"
SRC_URI:append = " file://0003-driver-xilinx-ai-engine-Added-support-to-disable-int.patch"
SRC_URI:append = " file://0004-driver-xilinx-ai-engine-Added-support-to-disable-axi.patch"
SRC_URI:append = " file://0005-xilinx-ai-engine-fix-UC-AXIMM-transaction-timeout-on.patch"
SRC_URI:append = " file://0006-xilinx-ai-engine-Fix-aie_part_pm_ops-function-argume.patch"
SRC_URI:append = " file://0007-xilinx-ai-engine-Granularize-error-handling-init-options.patch"
SRC_URI:append = " file://0001-xilinx-ai-engine-Add-USER_EVENT1-initialization-support.patch"

# xilinx-aie (CONFIG_XILINX_AIE=m in bsp_vaiml.cfg above) is built for every
# VEK385_AIE_VARIANT, including the NPU firmware path: zocl requests its AIE
# partition through this driver, so disabling it would break x_plus_ml even
# though the NPU itself is driven by amdxdna.
