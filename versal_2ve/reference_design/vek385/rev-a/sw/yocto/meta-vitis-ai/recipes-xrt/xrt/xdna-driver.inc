REPO = "gitsm://github.com/amd/xdna-driver;protocol=https"
BRANCHARG = "${@['nobranch=1', 'branch=${BRANCH}'][d.getVar('BRANCH', True) != '']}"
SRC_URI = "${REPO};${BRANCHARG};name=xdna"

BRANCH = "vai_6.3"
SRCREV_xdna = "fd1c9dede69e29b68fb0b14844a66d9f7c7c5dc2"
