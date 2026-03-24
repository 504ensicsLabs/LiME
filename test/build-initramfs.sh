#!/bin/bash
# SPDX-FileCopyrightText: 2011-2026 Joe T. Sylve, Ph.D. <joe.sylve@gmail.com>
# SPDX-License-Identifier: GPL-2.0-only
# build-initramfs.sh — Create a minimal initramfs for QEMU smoke testing.
# Usage: ./test/build-initramfs.sh <lime.ko> <output.cpio.gz>

set -euo pipefail

LIME_KO="${1:?Usage: $0 <lime.ko> <output.cpio.gz>}"
OUTPUT="${2:?Usage: $0 <lime.ko> <output.cpio.gz>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

BUSYBOX=$(command -v busybox 2>/dev/null || echo /usr/bin/busybox)
if [ ! -x "$BUSYBOX" ]; then
    echo "ERROR: busybox not found (install busybox-static)" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap "rm -rf $WORK" EXIT

mkdir -p "$WORK"/{bin,dev,proc,sys,tmp,lib/modules}

cp "$BUSYBOX" "$WORK/bin/busybox"
for cmd in sh mount umount ls cat wc od awk tr grep insmod rmmod poweroff; do
    ln -s busybox "$WORK/bin/$cmd"
done

cp "$LIME_KO" "$WORK/lib/modules/lime.ko"
cp "$SCRIPT_DIR/smoke-init" "$WORK/init"
chmod +x "$WORK/init"

(cd "$WORK" && find . | cpio -o -H newc --quiet) | gzip > "$OUTPUT"

echo "Initramfs: $OUTPUT ($(wc -c < "$OUTPUT") bytes)"
