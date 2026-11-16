# Linux toolchains for scripts/linux_check.py: the distro and compilers of the CI Linux jobs.
FROM ubuntu:24.04

RUN apt-get -o Acquire::Retries=5 update \
    && DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y --no-install-recommends \
        ca-certificates clang-19 clang-tools-19 cmake g++-14 git make ninja-build python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --break-system-packages --no-cache-dir "conan>=2.33,<3"
