#### builder ####
FROM debian:bookworm-slim AS arm-toolchain-builder

WORKDIR /opt

ARG TARGETARCH
ARG ARM_COMPILER_VERSION=15.2.rel1
ENV ARM_COMPILER_DIR=/opt/arm-gnu-toolchain
ENV DEBIAN_FRONTEND=noninteractive

RUN set -eux; \
    case "$TARGETARCH" in \
        amd64) echo "ARCH=x86_64" > /arch ;; \
        arm64) echo "ARCH=aarch64" > /arch ;; \
        *) echo "Unsupported TARGETARCH: $TARGETARCH"; exit 1 ;; \
    esac;

RUN apt-get update && apt-get install -y \
         curl \
         xz-utils \
         ca-certificates \
         binutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

RUN . /arch && export ARM_COMPILER_ARCHIVE="arm-gnu-toolchain-${ARM_COMPILER_VERSION}-${ARCH}-arm-none-eabi.tar.xz"; \
    curl -LO https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_COMPILER_VERSION}/binrel/${ARM_COMPILER_ARCHIVE} \
    && mkdir -p /opt \
    && tar -xf ${ARM_COMPILER_ARCHIVE} -C /opt/ \
    && mv /opt/arm-gnu-toolchain-${ARM_COMPILER_VERSION}-${ARCH}-arm-none-eabi ${ARM_COMPILER_DIR} \
    && rm ${ARM_COMPILER_ARCHIVE}

WORKDIR ${ARM_COMPILER_DIR}

# Remove docs, manpages, info pages, samples, gdb extras
RUN rm -rf \
    share/doc \
    share/info \
    share/man \
    share/gdb \
    share/gcc-* \
    share/gcc-arm-none-eabi \
    include/gdb

# Remove unused ARM-profile libraries
RUN rm -rf arm-none-eabi/lib/arm

# Keep ONLY: thumb/v7e-m+dp/hard (Cortex-M7 fpv5-d16 hard-float) and thumb/v6-m/nofp (Cortex-M0+ for tamp)
RUN cd arm-none-eabi/lib/thumb \
 && find . -mindepth 1 -maxdepth 1 \
      ! -name v6-m \
      ! -name v7e-m+dp \
      -exec rm -rf {} + \
 \
 && find v7e-m+dp -mindepth 1 -maxdepth 1 \
      ! -name hard \
      -exec rm -rf {} + \
 \
 && find v6-m -mindepth 1 -maxdepth 1 \
      ! -name nofp \
      -exec rm -rf {} +

# Remove unnecessary binaries, keep only commonly used embedded tools
RUN cd bin \
 && find . -type f \
      ! -name 'arm-none-eabi-gcc' \
      ! -name 'arm-none-eabi-g++' \
      ! -name 'arm-none-eabi-as' \
      ! -name 'arm-none-eabi-ld' \
      ! -name 'arm-none-eabi-ar' \
      ! -name 'arm-none-eabi-ranlib' \
      ! -name 'arm-none-eabi-objcopy' \
      ! -name 'arm-none-eabi-objdump' \
      ! -name 'arm-none-eabi-size' \
      ! -name 'arm-none-eabi-strip' \
      ! -name 'arm-none-eabi-nm' \
      ! -name 'arm-none-eabi-readelf' \
      -delete

# Strip binaries and libraries
RUN find . -type f -executable \
      -exec strip --strip-unneeded {} \; || true

RUN find . -name '*.a' \
      -exec strip -g {} \; || true


#### runtime ####
FROM python:3.12-slim-bookworm

ENV ARM_COMPILER_DIR=/opt/arm-gnu-toolchain
ENV GCC_PATH="${ARM_COMPILER_DIR}/bin"
ENV PATH="${GCC_PATH}:${PATH}"
ENV PYTHONDONTWRITEBYTECODE=1

COPY --from=arm-toolchain-builder \
    ${ARM_COMPILER_DIR} \
    ${ARM_COMPILER_DIR}

COPY ./requirements.txt /requirements.txt
COPY ./external/zelda3/requirements.txt /external/zelda3/requirements.txt

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        make \
        patch \
        sudo \
        wget \
        xxd \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-compile --no-cache-dir -r /requirements.txt \
    && rm -rf /requirements.txt /external

RUN useradd -m \
        -d /opt/workdir \
        -G sudo,plugdev \
        builder \
    && echo "builder ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

USER builder
WORKDIR /opt/workdir
CMD ["/bin/bash"]
