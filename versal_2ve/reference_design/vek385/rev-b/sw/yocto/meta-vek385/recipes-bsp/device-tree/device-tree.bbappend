FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

EXTRA_DT_INCLUDE_FILES:linux += "custom.dtsi"
EXTRA_DT_INCLUDE_FILES:linux += "${@bb.utils.contains('MIPI', '1', 'custom_isp.dtsi', '', d)}"
EXTRA_DT_INCLUDE_FILES:linux += "${@bb.utils.contains('NPU_FW', '1', 'custom_npufw.dtsi', '', d)}"
