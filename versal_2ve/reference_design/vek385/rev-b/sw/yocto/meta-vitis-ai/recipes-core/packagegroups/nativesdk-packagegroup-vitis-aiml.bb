DESCRIPTION = "Vitis AI/ML packages for SDK"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup
inherit_defer nativesdk

PACKAGEGROUP_DISABLE_COMPLEMENTARY = "1"

VITIS_AI_ML_NATIVESDK_PACKAGES = " \
	python3-build \
	python3-numpy \
	python3-pip \
	python3-pybind11 \
	python3-setuptools \
	python3-wheel \
        nativesdk-protobuf \
        nativesdk-protobuf-c \
        nativesdk-protobuf-compiler \
        nativesdk-protobuf-dev \
        python3-protobuf \
	"

RDEPENDS:${PN} = "${VITIS_AI_ML_NATIVESDK_PACKAGES}"
