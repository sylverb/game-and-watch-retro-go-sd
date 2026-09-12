#!/bin/sh
# Download the Arm GNU toolchain this project is pinned to, for the host you are
# on, and verify it against Arm's published SHA-256.
#
# The pinned version is read from Dockerfile (ARM_COMPILER_VERSION) rather than
# duplicated here, so there is exactly one place to bump it and this script
# cannot drift from what CI builds with.
#
# Usage:
#   ./scripts/get_toolchain.sh              # install into ~/opt
#   ./scripts/get_toolchain.sh /some/dir    # install elsewhere
#   ARM_COMPILER_VERSION=14.2.rel1 ./scripts/get_toolchain.sh   # override
#
# On success it prints the PATH line to use (or the GCC_PATH= argument, if you
# would rather not touch PATH).

set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dest=${1:-"$HOME/opt"}

# --- the pin -----------------------------------------------------------------
pin=$(sed -n 's/^ARG ARM_COMPILER_VERSION=\(.*\)$/\1/p' "$repo/Dockerfile" | head -1)
[ -n "$pin" ] || { echo "ERROR: could not read ARM_COMPILER_VERSION from $repo/Dockerfile." >&2; exit 1; }
ver=${ARM_COMPILER_VERSION:-$pin}

# --- host detection ----------------------------------------------------------
os=$(uname -s)
machine=$(uname -m)

case "$os" in
Linux)
	case "$machine" in
	x86_64)          arch=x86_64 ;;
	aarch64 | arm64) arch=aarch64 ;;
	*) echo "ERROR: unsupported Linux architecture '$machine'. Arm ships x86_64 and aarch64 only." >&2; exit 1 ;;
	esac
	;;
Darwin)
	case "$machine" in
	arm64)
		arch=darwin-arm64
		;;
	x86_64)
		# Arm's last Intel-macOS build is 14.2.rel1; 14.3 and 15.x are Apple
		# Silicon only. Fall back rather than 404, but say so loudly -- this is
		# NOT the compiler CI uses, and codegen differs.
		arch=darwin-x86_64
		if [ "$ver" != "14.2.rel1" ] && [ -z "${ARM_COMPILER_VERSION:-}" ]; then
			echo "WARNING: Arm ships no $ver build for Intel macOS (14.2.rel1 is the last one)." >&2
			echo "         Falling back to 14.2.rel1. This is NOT the pinned $ver that CI uses," >&2
			echo "         so your codegen will differ. Use 'make docker' when that matters." >&2
			ver=14.2.rel1
		fi
		;;
	*) echo "ERROR: unsupported macOS architecture '$machine'." >&2; exit 1 ;;
	esac
	;;
*)
	echo "ERROR: unsupported OS '$os'. This script handles Linux and macOS; on Windows use Docker." >&2
	exit 1
	;;
esac

name="arm-gnu-toolchain-$ver-$arch-arm-none-eabi"
url="https://developer.arm.com/-/media/Files/downloads/gnu/$ver/binrel/$name.tar.xz"

if [ -x "$dest/$name/bin/arm-none-eabi-gcc" ]; then
	echo "Already installed: $dest/$name"
else
	command -v curl >/dev/null 2>&1 || { echo "ERROR: curl not found." >&2; exit 1; }

	# Fail early and clearly rather than downloading an HTML error page.
	code=$(curl -sIL -o /dev/null -w '%{http_code}' "$url")
	if [ "$code" != "200" ]; then
		echo "ERROR: Arm has no $ver build for $arch (HTTP $code)." >&2
		echo "       $url" >&2
		echo "       Check the version, or build in Docker with 'make docker'." >&2
		exit 1
	fi

	mkdir -p "$dest"
	echo "Downloading $name.tar.xz ..."
	curl -fSL --progress-bar -o "$dest/$name.tar.xz" "$url"

	# Verify the download -- an interrupted or proxy-mangled archive otherwise
	# surfaces as a baffling compiler error much later.
	#
	# Arm's checksum files are misnamed, verified against 15.2.rel1:
	#   .sha256asc  -> the real SHA-256   (64 hex chars)
	#   .sha256     -> an MD5             (32 hex chars)
	#   .asc        -> the same MD5
	# So prefer .sha256asc, and pick the algorithm by digest length rather than
	# by file extension, which cannot be trusted.
	sum=""
	for ext in .sha256asc .sha256; do
		if curl -fsSL --max-time 60 -o "$dest/$name.sum" "$url$ext" 2>/dev/null; then
			sum=$(awk '{print $1; exit}' "$dest/$name.sum")
			rm -f "$dest/$name.sum"
			[ -n "$sum" ] && break
		fi
	done

	if [ -n "$sum" ]; then
		case ${#sum} in
		64) algo=sha256; actual=$(command -v sha256sum >/dev/null 2>&1 &&
				sha256sum "$dest/$name.tar.xz" | awk '{print $1}' ||
				shasum -a 256 "$dest/$name.tar.xz" | awk '{print $1}') ;;
		32) algo=md5;    actual=$(command -v md5sum >/dev/null 2>&1 &&
				md5sum "$dest/$name.tar.xz" | awk '{print $1}' ||
				md5 -q "$dest/$name.tar.xz") ;;
		*)  algo=""; actual="" ;;
		esac

		if [ -z "$algo" ]; then
			echo "WARNING: unrecognised digest length ${#sum}; skipping verification." >&2
		elif [ "$sum" != "$actual" ]; then
			echo "ERROR: $algo mismatch for $name.tar.xz" >&2
			echo "       expected $sum" >&2
			echo "       actual   $actual" >&2
			rm -f "$dest/$name.tar.xz"
			exit 1
		else
			echo "$algo OK"
		fi
	else
		echo "WARNING: no published checksum for this archive; skipping verification." >&2
	fi

	echo "Extracting into $dest ..."
	tar xf "$dest/$name.tar.xz" -C "$dest"
	rm -f "$dest/$name.tar.xz"
fi

got=$("$dest/$name/bin/arm-none-eabi-gcc" -dumpversion 2>/dev/null || echo '?')
echo
echo "arm-none-eabi-gcc $got installed (toolchain $ver, pinned $pin)."
[ "$ver" = "$pin" ] || echo "NOTE: this is NOT the pinned $pin -- see the warning above."
echo
echo "Add it to PATH:"
echo "    export PATH=\"$dest/$name/bin:\$PATH\""
echo "or pass it per build:"
echo "    make GCC_PATH=$dest/$name/bin ..."
