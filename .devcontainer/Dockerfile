FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="3.22.2"

# tsduckとffmpegのインストール
RUN apt-get update
RUN apt-get install -y --no-install-recommends \
    cmake flex bison dos2unix curl tar zip linux-libc-dev dpkg-dev python3 \
    ca-certificates \
    clang-19 \
    ffmpeg \
    git \
    libedit-dev \
    libssl-dev \
    libreadline-dev \
    lsb-release \
    python-is-python3 \
    sudo \
    zlib1g-dev

# RUN git clone https://github.com/tsduck/tsduck.git /tmp/tsduck && git -C /tmp/tsduck checkout v3.40-4165
RUN git clone https://github.com/tsduck/tsduck.git /tmp/tsduck

WORKDIR /tmp/tsduck
RUN LLLVM=1 NOTEST=1 NODEKTEC=1 NOHIDES=1 NOVATEK=1 NOOPENSSL=1 NOZLIB=1 NOCURL=1 NOPCSC=1 NOSRT=1 NORIST=1 NOJAVA=1 NOPYTHON=1 NOHWACCEL=1 ./scripts/install-prerequisites.sh
RUN make CC=clang-19 CXX=clang++-19 LLVM=1 NOTEST=1 NODEKTEC=1 NOHIDES=1 NOVATEK=1 NOOPENSSL=1 NOZLIB=1 NOCURL=1 NOPCSC=1 NOSRT=1 NORIST=1 NOJAVA=1 NOHWACCEL=1 -j$(nproc) default
RUN make CC=clang-19 CXX=clang++-19 LLVM=1 NOTEST=1 NODEKTEC=1 NOHIDES=1 NOVATEK=1 NOOPENSSL=1 NOZLIB=1 NOCURL=1 NOPCSC=1 NOSRT=1 NORIST=1 NOJAVA=1 NOHWACCEL=1 install

# emscripten用にcmake入れ直す
# Optionally install the cmake for vcpkg
COPY ./reinstall-cmake.sh /tmp/

RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
        chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
    fi \
    && rm -f /tmp/reinstall-cmake.sh

# [Optional] Uncomment this section to install additional vcpkg ports.
# RUN su vscode -c "${VCPKG_ROOT}/vcpkg install <your-port-name-here>"

# [Optional] Uncomment this section to install additional packages.
# RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
#     && apt-get -y install --no-install-recommends <your-package-list-here>
