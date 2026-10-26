FROM ubuntu:24.04@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates clang-18 llvm-18 lld-18 make nasm python3 mtools git \
    qemu-system-x86 gdb gcc g++ binutils curl xz-utils bzip2 \
    libgmp-dev libmpfr-dev libmpc-dev texinfo bison flex \
    && rm -rf /var/lib/apt/lists/*
ENV CXX=clang++-18 LLVM_PROFDATA=llvm-profdata-18 LLVM_COV=llvm-cov-18
WORKDIR /work

# Pin source releases and verify downloads. No manual cross-compiler setup.
FROM base AS compiler
COPY build_toolchain.sh /tmp/build_toolchain.sh
RUN sh /tmp/build_toolchain.sh && rm /tmp/build_toolchain.sh
ENV PATH=/opt/cross/bin:$PATH

FROM compiler AS toolchain
RUN apt-get update && apt-get install -y --no-install-recommends gdb-multiarch \
    && rm -rf /var/lib/apt/lists/*
ENV GDB=gdb-multiarch TEST_LD=x86_64-elf-ld TEST_OBJCOPY=x86_64-elf-objcopy

FROM base AS host
RUN apt-get update && apt-get install -y --no-install-recommends libclang-rt-18-dev \
    && rm -rf /var/lib/apt/lists/*

# Interactive builds share the exact compiler stage used by CI.
FROM toolchain AS development
RUN apt-get update && apt-get install -y --no-install-recommends novnc websockify \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir /output && chown 1000:1000 /output
USER 1000:1000
