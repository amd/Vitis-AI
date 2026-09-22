REPO = "gitsm://github.com/wendyliang25/xdna-driver;protocol=https"
BRANCHARG = "${@['nobranch=1', 'branch=${BRANCH}'][d.getVar('BRANCH', True) != '']}"
SRC_URI = "${REPO};${BRANCHARG};name=xdna"

BRANCH = "aie4-soc-ipc"
# Head of https://github.com/wendyliang25/xdna-driver/tree/aie4-soc-ipc
SRCREV_xdna = "ad5373601a2210ae8dd9912f19259fa0f24c27f3"
