FROM mirakc/mirakc:latest

RUN apt-get update && apt-get install -y --no-install-recommends \
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


RUN git clone https://github.com/tsduck/tsduck.git /tmp/tsduck && git -C /tmp/tsduck checkout v3.40-4165

WORKDIR /tmp/tsduck
RUN LLLVM=1 NOTEST=1 NODEKTEC=1 NOHIDES=1 NOVATEK=1 NOOPENSSL=1 NOZLIB=1 NOCURL=1 NOPCSC=1 NOSRT=1 NORIST=1 NOJAVA=1 NOPYTHON=1 NOHWACCEL=1 ./scripts/install-prerequisites.sh
RUN make CC=clang-19 CXX=clang++-19 LLVM=1 NOTEST=1 NODEKTEC=1 NOHIDES=1 NOVATEK=1 NOOPENSSL=1 NOZLIB=1 NOCURL=1 NOPCSC=1 NOSRT=1 NORIST=1 NOJAVA=1 NOPYTHON=1 NOHWACCEL=1 -j$(nproc) default
RUN make CC=clang-19 CXX=clang++-19 LLVM=1 NOTEST=1 NODEKTEC=1 NOHIDES=1 NOVATEK=1 NOOPENSSL=1 NOZLIB=1 NOCURL=1 NOPCSC=1 NOSRT=1 NORIST=1 NOJAVA=1 NOPYTHON=1 NOHWACCEL=1 install

COPY config.yml /etc/mirakc/config.yml
ENV MIRAKC_CONFIG=/etc/mirakc/config.yml
EXPOSE 40772

ENTRYPOINT ["/usr/local/bin/mirakc"]
