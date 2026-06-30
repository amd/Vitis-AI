REPO = "gitsm://github.com/amd/xdna-driver;protocol=https"
BRANCHARG = "${@['nobranch=1', 'branch=${BRANCH}'][d.getVar('BRANCH', True) != '']}"
SRC_URI = "${REPO};${BRANCHARG};name=xdna"

BRANCH = "vai_6.2"
SRCREV_xdna = "a8e7fa38266526f98142db39ae5016792c696978"
