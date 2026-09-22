FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI:append = " \
    file://0001-v4l2src-vvas-tensor-formats.patch \
    file://0002-v4l2src-tile-offset.patch \
"
