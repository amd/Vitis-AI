DESCRIPTION = "VAIML dependencies"

PACKAGE_ARCH = "${TUNE_PKGARCH}"

inherit packagegroup

VAIML_DEP = " \
        libstdc++-dev\
        libstdc++\
        zocl\
        opencl-clhpp-dev\
        opencl-headers\
        libdrm\
        libdrm-dev\
        xrt\
        ncurses\
        ncurses-terminfo\
        valgrind\
        perf\
        gdb\
        binutils\
        python3-json\
        python3-shell\
        python3-core\
        libpython3\
        python3-multiprocessing\
        boost\
        amdxdna\
        python3-pybind11\
        xtensor-dev\
        xtl-dev\
        glog\
        protobuf\
        nlohmann-json\
        spdlog-dev\
        libeigen-dev\
        python3-setuptools\
        python3-build \
        python3-wheel \
        python3 \
        python3-pip \
        python3-numpy \
        opencv \
        jansson \
        ryzenai-wheels \
        vart-x \
        vart-ml \
        vvas-utils \
        vvas-gst-plugins \
        vvas-accel-sw-libs \
        vvas-examples \
        cpp-examples \
        cert-ve2 \
        vaiml-models \
        vaiprofile-collect-data \
"

RDEPENDS:${PN} = "${VAIML_DEP}"
