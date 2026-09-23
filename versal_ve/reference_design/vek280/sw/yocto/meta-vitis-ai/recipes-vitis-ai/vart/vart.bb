SUMMARY = "VART-X runtime installed as a Python wheel into site-packages"
LICENSE = "CLOSED"

S         = "${WORKDIR}"
WHEEL_DIR = "${WORKDIR}/wheels"

SRC_URI = "file://wheels"

inherit meson pkgconfig python3native deploy
do_configure[noexec] = "1"
do_compile[noexec]   = "1"

DEPENDS += " \
    glib-2.0 \
    glib-2.0-native \
    xrt \
    opencv \
    jansson \
    ne10 \
    protobuf \
    python3 \
    python3-native \
    python3-pip-native \
    python3-setuptools-native \
    python3-wheel-native \
"
do_install[depends]        += "python3-pip-native:do_populate_sysroot"
do_install[file-checksums] += "${VART_X_WHL}:False"

INSANE_SKIP:${PN}           = "dev-so already-stripped"
do_package_qa[noexec]       = "1"
EXCLUDE_FROM_SHLIBS         = "1"
INHIBIT_PACKAGE_STRIP       = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
SOLIBS                      = ".so"
FILES_SOLIBSDEV             = ""

export VART_X_WHL
BB_ENV_PASSTHROUGH_ADDITIONS += "VART_X_WHL"
VART_X_WHL ?= ""

# Override VART_X_WHL_SERVER + VART_X_WHL_HOST in local.conf for air-gapped builds.
VART_X_WHL_SERVER   ?= "https://pypi.amd.com/vai/6.3/simple"
VART_X_WHL_HOST     ?= "pypi.amd.com"
VART_X_WHEEL_CACHE  ?= "${DL_DIR}/vart-x-wheels"


do_fetch[network] = "1"
do_fetch[depends] += "python3-pip-native:do_populate_sysroot"

do_fetch() {
    if ls ${WHEEL_DIR}/vart_x*.whl >/dev/null 2>&1; then
        bbnote "vart: wheel already available — skipping network fetch"
        return 0
    fi

    if [ -n "${VART_X_WHL}" ]; then
        if [ -f "${VART_X_WHL}" ]; then
            bbnote "vart: VART_X_WHL points to a valid wheel file — skipping network fetch"
            return 0
        elif [ -d "${VART_X_WHL}" ] && ls "${VART_X_WHL}"/vart_x*.whl >/dev/null 2>&1; then
            bbnote "vart: VART_X_WHL points to a directory containing a wheel — skipping network fetch"
            return 0
        fi
        bbwarn "vart: VART_X_WHL='${VART_X_WHL}' is set but no vart_x*.whl found (missing file or empty directory) — falling through to network fetch"
    fi

    install -d "${VART_X_WHEEL_CACHE}"
    bbnote "vart: downloading wheel from ${VART_X_WHL_SERVER}"
    ${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip download \
        --no-deps \
        --no-cache-dir \
        --trusted-host "${VART_X_WHL_HOST}" \
        --index-url "${VART_X_WHL_SERVER}" \
        -d "${VART_X_WHEEL_CACHE}" \
        vart-x || bbfatal "vart: failed to download vart-x wheel from ${VART_X_WHL_SERVER}.\n  To use a pre-built wheel: export VART_X_WHL=/path/to/vart_x-*.whl before invoking bitbake.\n  To use a different server: export VART_X_WHL_SERVER and VART_X_WHL_HOST before invoking bitbake."

    ls ${VART_X_WHEEL_CACHE}/vart_x*.whl >/dev/null 2>&1 || \
        bbfatal "vart: download appeared to succeed but no vart_x*.whl found in ${VART_X_WHEEL_CACHE}"
}

# Wheel resolution priority: WHEEL_DIR (built) > VART_X_WHL > VART_X_WHEEL_CACHE (fetched) > fatal
python do_stage_wheel() {
    import glob, os, shutil

    wdir = d.getVar('WHEEL_DIR')

    if glob.glob(os.path.join(wdir, 'vart_x*.whl')):
        bb.note("vart: wheel already staged in WHEEL_DIR")
        return
    bb.utils.mkdirhier(wdir)

    def _stage(src):
        if not src:
            return None
        if os.path.isdir(src):
            cands = sorted(glob.glob(os.path.join(src, 'vart_x*.whl')), key=os.path.getmtime)
            src = cands[-1] if cands else None
        if src and os.path.isfile(src) and src.endswith('.whl'):
            shutil.copy(src, wdir)
            return src
        return None

    whl = d.getVar('VART_X_WHL') or ''
    if whl:
        staged = _stage(whl)
        if staged:
            bb.note("vart: staged wheel from VART_X_WHL='%s' -> %s" % (whl, os.path.basename(staged)))
            return
        bb.warn("vart: VART_X_WHL='%s' is set but no vart_x*.whl found — falling through to download cache" % whl)

    cache = d.getVar('VART_X_WHEEL_CACHE')
    staged = _stage(cache)
    if staged:
        bb.note("vart: staged wheel from download cache: %s" % os.path.basename(staged))
        return

    bb.fatal(
        "vart: no vart_x*.whl found. Resolve using one of the following options:\n"
        "  1. Export VART_X_WHL=/path/to/vart_x-*.whl before invoking bitbake\n"
        "  2. Ensure network access to %s\n"
        "     To use a different server: export VART_X_WHL_SERVER and VART_X_WHL_HOST before invoking bitbake"
        % d.getVar('VART_X_WHL_SERVER')
    )
}
addtask stage_wheel after do_fetch before do_install
do_stage_wheel[vardeps]        += "VART_X_WHL VART_X_WHL_SERVER VART_X_WHL_HOST VART_X_WHEEL_CACHE"
do_stage_wheel[file-checksums] += "${VART_X_WHL}:False"

do_install() {
    if ! ls ${WHEEL_DIR}/vart_x*.whl > /dev/null 2>&1; then
        bbfatal "vart: no vart_x*.whl in WHEEL_DIR (${WHEEL_DIR}) — do_stage_wheel should have caught this"
    fi

    install -d ${D}${PYTHON_SITEPACKAGES_DIR}
    for whl in ${WHEEL_DIR}/vart_x*.whl; do
        bbnote "vart: installing wheel: $(basename $whl)"
        ${STAGING_BINDIR_NATIVE}/python3-native/python3 -m pip install \
            --no-deps --prefix=${D}/usr "$whl" || \
            bbfatal "vart: pip install failed for $(basename $whl)"
    done

    PKG_DIR="${D}${PYTHON_SITEPACKAGES_DIR}/vart_x"
    [ -d "$PKG_DIR" ] || bbfatal "vart: vart_x not found in site-packages after wheel install — wheel may be malformed"

    install -d ${D}${libdir}/pkgconfig
    for pc in vart-x vvas-core; do
        PC="$PKG_DIR/lib/pkgconfig/${pc}.pc"
        if [ -f "$PC" ]; then
            pc_rel=$(realpath -m --relative-to="${D}${libdir}/pkgconfig" "$PC")
            ln -sf "$pc_rel" ${D}${libdir}/pkgconfig/${pc}.pc
        fi
    done
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${WHEEL_DIR}/vart_x*.whl ${DEPLOYDIR}/
}
addtask deploy after do_install before do_package

FILES:${PN}      = "${PYTHON_SITEPACKAGES_DIR}/vart_x ${PYTHON_SITEPACKAGES_DIR}/vart_x-*"
FILES:${PN}-dev += "${libdir}/pkgconfig/vart-x.pc ${libdir}/pkgconfig/vvas-core.pc"
