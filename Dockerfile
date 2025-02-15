FROM ubuntu

WORKDIR /opt

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get upgrade -y && \
    apt-get install -y \
        bzip2 \
        curl \
        git \
        libncurses5 \
        lz4 \
        make \
        python3 \
        python3-pip \
        sudo \
        unzip \
        vim \
        wget \
        && \
    wget -O toolchain.tar.bz2 'https://developer.arm.com/-/media/Files/downloads/gnu-rm/10-2020q4/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux.tar.bz2?revision=ca0cbf9c-9de2-491c-ac48-898b5bbc0443&la=en&hash=68760A8AE66026BCF99F05AC017A6A50C6FD832A' && \
    tar xf toolchain.tar.bz2 && \
    rm -f toolchain.tar.bz2 && \
    useradd -m docker && echo "docker:docker" | chpasswd && \
    chown docker:docker /opt && \
    echo "docker ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

ENV GCC_PATH=/opt/gcc-arm-none-eabi-10-2020-q4-major/bin

USER docker

# Install openocd nightly
# TODO: remove once gnwmanager fully supports raspberry pi in pyocd;
# this currently leaves the option if we want to add openocd backend to gnwmanager.
RUN wget https://nightly.link/kbeckmann/ubuntu-openocd-git-builder/workflows/docker/master/openocd-git.deb.zip && \
    unzip openocd-git.deb.zip && \
    sudo apt-get -y install ./openocd-git_*_amd64.deb
ENV OPENOCD="/opt/openocd-git/bin/openocd"

COPY requirements.txt requirements.txt
RUN python3 -m pip install -r requirements.txt

RUN mkdir /opt/workdir
RUN sudo chown -R docker:docker /opt/workdir

WORKDIR /opt/workdir

CMD /bin/bash
