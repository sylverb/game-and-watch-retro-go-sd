FROM debian:bookworm-slim

WORKDIR /opt

ENV ARM_COMPILER_VERSION=15.2.rel1
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        xz-utils \
        wget \
        make \
        patch \
        python3 \
        python3-pip \
        python3-venv \
        sudo \
        xxd && \
        apt-get clean && \
        rm -rf /var/lib/apt/lists/*

RUN ARCH=$(uname -m) && \
    TOOLCHAIN_FILE="arm-gnu-toolchain-${ARM_COMPILER_VERSION}-${ARCH}-arm-none-eabi.tar.xz" && \
    wget -O toolchain.md5.asc "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_COMPILER_VERSION}/binrel/${TOOLCHAIN_FILE}.asc" && \
    EXPECTED_MD5=$(awk '{print $1}' toolchain.md5.asc) && \
    wget -O toolchain.tar.xz "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_COMPILER_VERSION}/binrel/${TOOLCHAIN_FILE}" && \
    ACTUAL_MD5=$(md5sum toolchain.tar.xz | awk '{print $1}') && \
    if [ "$EXPECTED_MD5" != "$ACTUAL_MD5" ]; then \
        echo "MD5 checksum mismatch! Expected: $EXPECTED_MD5, Got: $ACTUAL_MD5" && \
        exit 1; \
    fi && \
    tar xf toolchain.tar.xz && \
    rm -f toolchain.tar.xz toolchain.md5.asc

RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --upgrade pip && \
    /opt/venv/bin/pip install pillow

RUN useradd -m docker && echo "docker:docker" | chpasswd && \
    chown docker:docker /opt && \
    echo "docker ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

RUN echo 'export PATH="/opt/venv/bin:$PATH"' > /etc/profile.d/01-venv.sh && \
    chmod +x /etc/profile.d/01-venv.sh

RUN ARCH=$(uname -m) && \
    echo "export GCC_PATH=/opt/arm-gnu-toolchain-${ARM_COMPILER_VERSION}-${ARCH}-arm-none-eabi/bin" \
    > /etc/profile.d/02-gcc.sh && \
    chmod +x /etc/profile.d/02-gcc.sh

USER docker

ENV PATH="/opt/venv/bin:$PATH"

RUN mkdir /opt/workdir
RUN sudo chown -R docker:docker /opt/workdir

WORKDIR /opt/workdir

CMD ["/bin/bash", "-l"]