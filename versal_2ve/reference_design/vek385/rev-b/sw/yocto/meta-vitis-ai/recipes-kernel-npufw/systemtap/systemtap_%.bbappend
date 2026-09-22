# The base systemtap recipe ships with
#   PACKAGECONFIG ??= "translator sqlite monitor python3-probes ..."
# which enables SDT probes for Python3. That pulls setuptools3-base into
# systemtap via `inherit_defer setuptools3-base`, which in turn drops target
# python3 dev headers into the sysroot of every recipe that DEPENDS on
# systemtap. That sysroot pollution makes XRT's
#   xrt/src/python/pybind11/CMakeLists.txt
# find Python3 and then fail on find_package(pybind11 REQUIRED) for AIE4/NPU
# builds that don't set XRT_EDGE=1.
#
# We don't use Python SDT probes here, so drop that PACKAGECONFIG flag. The
# sys/sdt.h header we actually need is produced by the default translator/
# runtime build and is unaffected.
PACKAGECONFIG:remove = "python3-probes"

# Without python3-probes, stap-profile-annotate is still installed under ${PN}
# with a #!/usr/bin/python3 shebang; file-rdeps QA requires an explicit runtime dep.
RDEPENDS:${PN}:append:class-target = " python3-core"
