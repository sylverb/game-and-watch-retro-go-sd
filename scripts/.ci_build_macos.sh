#!/bin/bash
set -xe

./scripts/.ci_prepare_roms.sh

# Install toolchain
# Prefer ./scripts/get_toolchain.sh (reads ARM_COMPILER_VERSION from Dockerfile).
# Legacy mirror kept below for this old CI path:
# curl -L -o toolchain.tar.bz2 https://allg.one/7vMO

# Faster mirror:
curl -L --http1.1 -o toolchain.tar.bz2 https://allg.one/7vMO

tar xf toolchain.tar.bz2

export GCC_PATH=$(pwd)/gcc-arm-none-eabi-10-2020-q4-major/bin

make -j8 download_sdk

make -j$(sysctl -n hw.logicalcpu)
