#!/bin/bash
# SPDX-FileCopyrightText: 2011-2026 Joe T. Sylve, Ph.D. <joe.sylve@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only
# prepare-kernel.sh - Download and prepare a kernel source tree for
# out-of-tree module compilation testing.
#
# Usage: ./test/prepare-kernel.sh <series> [config]
#   series  - Kernel version series, e.g. "6.6" or "5.15"
#   config  - "defconfig" (default), "no-zlib", "preempt-rt", or "qemu"
#
# Output: A prepared kernel tree at /tmp/linux-build/
#
# The tree is stripped of source files after preparation to minimize cache
# size (~100 MB vs ~1 GB).  Only headers, scripts, and build infrastructure
# are kept — everything needed for out-of-tree module compilation.

set -euo pipefail

SERIES="${1:?Usage: $0 <kernel-series> [config]}"
CONFIG="${2:-defconfig}"
DEST="${LIME_KDIR:-/tmp/linux-build}"
MAJOR="${SERIES%%.*}"

##
## Resolve kernel version.
## Pinned versions live in kernel-versions.conf (separate file so that
## edits to this script don't invalidate every cache entry).
## Falls back to dynamic resolution for unknown series (canary job).
##
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

resolve_version() {
    # Check the pin file first
    local pin
    pin=$(grep "^${SERIES}=" "$SCRIPT_DIR/kernel-versions.conf" 2>/dev/null | cut -d= -f2)
    if [ -n "$pin" ]; then
        echo "$pin"
        return
    fi
    # Dynamic resolution for canary / unknown series
    local ver
    ver=$(git ls-remote --tags \
        "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git" |
        grep -oP "refs/tags/v\K${SERIES//./\\.}\.[0-9]+$" |
        sort -V | tail -1) || true
    echo "${ver:-$SERIES}"
}

FULL_VER=$(resolve_version)
echo "==> linux-${FULL_VER} (series ${SERIES}, config ${CONFIG})"

##
## Download
##
URL="https://cdn.kernel.org/pub/linux/kernel/v${MAJOR}.x/linux-${FULL_VER}.tar.xz"
TARBALL="/tmp/linux-${FULL_VER}.tar.xz"

echo "==> Downloading ${URL}..."
curl -fL --retry 3 --progress-bar -o "$TARBALL" "$URL"

echo "==> Extracting..."
tar xf "$TARBALL" -C /tmp
rm -f "$TARBALL"

SRCDIR="/tmp/linux-${FULL_VER}"
cd "$SRCDIR"

##
## Configure
##
echo "==> Configuring..."
make -s defconfig

case "$CONFIG" in
    defconfig)
        ;;
    no-zlib)
        scripts/config --disable CONFIG_ZLIB_DEFLATE
        scripts/config --disable CONFIG_ZLIB_INFLATE
        ;;
    preempt-rt)
        scripts/config --enable CONFIG_PREEMPT_RT
        ;;
    qemu)
        # Console — enable for all supported archs (non-existent options
        # are silently ignored by scripts/config)
        scripts/config --enable CONFIG_SERIAL_8250
        scripts/config --enable CONFIG_SERIAL_8250_CONSOLE
        scripts/config --enable CONFIG_SERIAL_AMBA_PL011
        scripts/config --enable CONFIG_SERIAL_AMBA_PL011_CONSOLE
        # Common
        scripts/config --enable CONFIG_VIRTIO
        scripts/config --enable CONFIG_DEVTMPFS
        scripts/config --enable CONFIG_DEVTMPFS_MOUNT
        scripts/config --enable CONFIG_TMPFS
        scripts/config --enable CONFIG_BLK_DEV_INITRD
        scripts/config --enable CONFIG_RD_GZIP
        scripts/config --enable CONFIG_ZLIB_DEFLATE
        scripts/config --enable CONFIG_ZLIB_INFLATE
        ;;
    *)
        echo "ERROR: Unknown config '${CONFIG}'" >&2
        exit 1
        ;;
esac

# Options LiME needs
scripts/config --enable CONFIG_MODULES
scripts/config --enable CONFIG_CRYPTO
scripts/config --enable CONFIG_CRYPTO_HASH
scripts/config --enable CONFIG_CRYPTO_SHA256
scripts/config --enable CONFIG_INET
scripts/config --enable CONFIG_NET

# Avoid host-tool build failures on modern toolchains
scripts/config --disable CONFIG_STACK_VALIDATION
scripts/config --disable CONFIG_GCC_PLUGINS
# Module signing builds scripts/sign-file against the host OpenSSL.
# Old kernels use OpenSSL 1.x APIs removed in OpenSSL 3.x (Ubuntu 24.04).
scripts/config --disable CONFIG_MODULE_SIG
scripts/config --disable CONFIG_SYSTEM_TRUSTED_KEYRING
make -s olddefconfig

##
## Build
##
echo "==> modules_prepare ($(nproc) jobs)..."
# Host tool compilation flags for modern GCC (14+) building old kernels:
#   -Wno-error: suppress warnings promoted to errors
#   -fcommon: allow duplicate globals in old dtc (GCC 10+ defaults to -fno-common)
#   GCC 14 made implicit-function-declaration et al. hard errors by default
LIME_HOSTCFLAGS="-Wno-error -fcommon"
LIME_HOSTCFLAGS="$LIME_HOSTCFLAGS -Wno-error=implicit-function-declaration"
LIME_HOSTCFLAGS="$LIME_HOSTCFLAGS -Wno-error=implicit-int"
LIME_HOSTCFLAGS="$LIME_HOSTCFLAGS -Wno-error=incompatible-pointer-types"
LIME_HOSTCFLAGS="$LIME_HOSTCFLAGS -Wno-error=int-conversion"
make -j"$(nproc)" HOSTCFLAGS="$LIME_HOSTCFLAGS" modules_prepare 2>&1 | \
    tee /tmp/modules_prepare.log | tail -5
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "::error::modules_prepare failed — full output:"
    grep -E '(error:|Error )' /tmp/modules_prepare.log || cat /tmp/modules_prepare.log
    exit 1
fi

if [ "$CONFIG" = "qemu" ]; then
    # Each arch has a different kernel image target
    case "$(uname -m)" in
        x86_64)         KERNEL_TARGET=bzImage ;;
        aarch64)        KERNEL_TARGET=Image ;;
        armv7l|armhf)   KERNEL_TARGET=zImage ;;
        riscv64)        KERNEL_TARGET=Image ;;
        *)              KERNEL_TARGET=bzImage ;;
    esac
    echo "==> ${KERNEL_TARGET} ($(nproc) jobs — this takes a while on first run)..."
    make -j"$(nproc)" "$KERNEL_TARGET" 2>&1 | \
        tee /tmp/kernel_build.log | tail -5
    if [ "${PIPESTATUS[0]}" -ne 0 ]; then
        echo "::error::kernel build failed — full output:"
        grep -E '(error:|Error )' /tmp/kernel_build.log || cat /tmp/kernel_build.log
        exit 1
    fi
fi

##
## Strip the source tree to minimize cache size.
## Keep only what out-of-tree module compilation needs:
##   include/, arch/*/include/, scripts/, Makefiles, .config, generated headers.
## For qemu config, also keep the kernel image (bzImage/Image/zImage).
##
echo "==> Stripping tree for caching..."

# Remove large directories that are never needed for module builds
rm -rf Documentation samples firmware sound usr certs virt \
       drivers fs mm net block crypto ipc security lib init kernel

# Clean tools/ but keep objtool (needed by kbuild for module linking)
if [ -d tools/objtool ]; then
    find tools -mindepth 1 -maxdepth 1 -not -name objtool \
        -exec rm -rf {} + 2>/dev/null || true
else
    rm -rf tools
fi

# Remove source files (keep headers, makefiles, build scripts, linker scripts)
find . -type f \( -name "*.c" -o -name "*.S" \) \
    -not -path "./scripts/*" -delete 2>/dev/null || true

# Remove object files from the kernel build (keep scripts/*.o — host tools)
find . -path ./scripts -prune -o -name "*.o" -type f -print | xargs rm -f 2>/dev/null || true

TREE_SIZE=$(du -sh . | cut -f1)
echo "==> Trimmed tree: ${TREE_SIZE}"

##
## Move to destination
##
rm -rf "$DEST"
mv "$SRCDIR" "$DEST"

KREL=$(make -s -C "$DEST" kernelrelease 2>/dev/null || echo "$FULL_VER")
echo "==> Ready: ${DEST} (${KREL}, ${CONFIG}, ${TREE_SIZE})"
