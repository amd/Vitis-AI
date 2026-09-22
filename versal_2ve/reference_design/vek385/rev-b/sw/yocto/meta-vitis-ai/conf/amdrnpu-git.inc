# Git fetch from PhysicalAI/tessai-agent @ main (required by amdrnpu recipes).

AMDRNPU_REPO ??= ""
AMDRNPU_BRANCH ??= "main"

# Head of the amdrnpu source repository @ main
SRCREV_amdrnpu ??= "4526c36ea06f46bd118adc711af472355fd7894e"

SRC_URI:append = " \
    ${AMDRNPU_REPO};branch=${AMDRNPU_BRANCH};name=amdrnpu \
"

SRCREV_FORMAT = "amdrnpu"
SRCREV:amdrnpu = "${SRCREV_amdrnpu}"

AMDRNPU_ROOT = "${WORKDIR}/git"
