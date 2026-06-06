REPO = "gitsm://github.com/amd/xdna-driver;protocol=https"
BRANCHARG = "${@['nobranch=1', 'branch=${BRANCH}'][d.getVar('BRANCH', True) != '']}"
SRC_URI = "${REPO};${BRANCHARG};name=xdna"

BRANCH = "vai_6.2"
SRCREV_xdna = "1a5979a475595b8ad79de26e19247908539cbb6a"
