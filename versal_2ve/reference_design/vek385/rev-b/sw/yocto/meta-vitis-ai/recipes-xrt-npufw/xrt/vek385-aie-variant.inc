# Shared VEK385 AIE mode helpers for meta-vitis-ai.
#
# Use the Python helpers below instead of comparing VEK385_AIE_VARIANT to
# hard-coded strings (e.g. 'TessAI') in recipes.
#
# To add or rename a mode, adjust the space-separated lists in local.conf or a
# distro fragment:
#   VEK385_AIE_VARIANT_NPU_MODE_VALUES   — NPU firmware path (xdna npu_of, …)
#   VEK385_AIE_VARIANT_VE2_MODE_VALUES   — Versal AIE-2 / VE2 edge path (xdna of bus, …)
#
# The variant selects the amdxdna bus binding and the XRT build flavour only.
# zocl and the xilinx-aie kernel driver are built for every variant.

VEK385_AIE_VARIANT_NPU_MODE_VALUES ??= "TessAI"
VEK385_AIE_VARIANT_VE2_MODE_VALUES ??= "ve2"


def vek385_aie_has_npu_fw(d):
    """True when this variant uses the AMD NPU firmware / driver stack."""
    v = (d.getVar("VEK385_AIE_VARIANT") or "").strip()
    vals = (d.getVar("VEK385_AIE_VARIANT_NPU_MODE_VALUES") or "").split()
    return v in vals


def aie_has_npu_fw(d):
    """Short alias for vek385_aie_has_npu_fw(d); use in ${@...} and python snippets."""
    return vek385_aie_has_npu_fw(d)


def vek385_aie_has_ve2_edge_fw(d):
    """True when this variant uses the Versal AIE-2 (VE2) edge stack (amdxdna of bus, …)."""
    v = (d.getVar("VEK385_AIE_VARIANT") or "").strip()
    vals = (d.getVar("VEK385_AIE_VARIANT_VE2_MODE_VALUES") or "").split()
    return v in vals


def vek385_aie_variant_is_known(d):
    v = (d.getVar("VEK385_AIE_VARIANT") or "").strip()
    if not v:
        return False
    return vek385_aie_has_npu_fw(d) or vek385_aie_has_ve2_edge_fw(d)
