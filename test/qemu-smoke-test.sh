#!/bin/bash
# qemu-smoke-test.sh — Boot a kernel in QEMU and run LiME smoke tests.
# Supports x86_64, aarch64, arm, and riscv64.
#
# Usage: ./test/qemu-smoke-test.sh <kernel-dir> <lime.ko>

set -euo pipefail

KDIR="${1:?Usage: $0 <kernel-dir> <lime.ko>}"
LIME_KO="${2:?Usage: $0 <kernel-dir> <lime.ko>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Architecture-specific QEMU configuration
case "$(uname -m)" in
    x86_64)
        QEMU_BIN=qemu-system-x86_64
        QEMU_MACHINE="-cpu max"
        KERNEL_IMAGE="$KDIR/arch/x86/boot/bzImage"
        CONSOLE=ttyS0
        ;;
    aarch64)
        QEMU_BIN=qemu-system-aarch64
        QEMU_MACHINE="-M virt -cpu max"
        KERNEL_IMAGE="$KDIR/arch/arm64/boot/Image"
        CONSOLE=ttyAMA0
        ;;
    armv7l|armhf)
        QEMU_BIN=qemu-system-arm
        QEMU_MACHINE="-M virt"
        KERNEL_IMAGE="$KDIR/arch/arm/boot/zImage"
        CONSOLE=ttyAMA0
        ;;
    riscv64)
        QEMU_BIN=qemu-system-riscv64
        QEMU_MACHINE="-M virt -bios none"
        KERNEL_IMAGE="$KDIR/arch/riscv/boot/Image"
        CONSOLE=ttyS0
        ;;
    *)
        echo "ERROR: Unsupported architecture $(uname -m)" >&2
        exit 1
        ;;
esac

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "ERROR: $KERNEL_IMAGE not found" >&2
    exit 1
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "ERROR: $QEMU_BIN not found (install the appropriate qemu-system package)" >&2
    exit 1
fi

INITRAMFS=$(mktemp --suffix=.cpio.gz)
LOG=$(mktemp)
trap "rm -f $INITRAMFS $LOG" EXIT

bash "$SCRIPT_DIR/build-initramfs.sh" "$LIME_KO" "$INITRAMFS"

echo "==> Booting QEMU ($(uname -m))..."
echo "    Binary:    $QEMU_BIN"
echo "    Kernel:    $KERNEL_IMAGE"
echo "    Console:   $CONSOLE"
echo ""

timeout 180 $QEMU_BIN \
    $QEMU_MACHINE \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$INITRAMFS" \
    -append "console=$CONSOLE panic=-1 quiet loglevel=4" \
    -nographic \
    -no-reboot \
    -m 256M \
    -smp 2 \
    > "$LOG" 2>&1 || true

# Show the test output (skip kernel boot messages)
sed -n '/=== LiME Smoke Test ===/,/=== TEST_COMPLETE ===/p' "$LOG"

if grep -q "SMOKE_TEST_RESULT=PASS" "$LOG"; then
    echo ""
    echo "==> Smoke test PASSED"
    exit 0
elif grep -q "TEST_COMPLETE" "$LOG"; then
    echo ""
    echo "==> Smoke test FAILED"
    exit 1
else
    echo ""
    echo "==> VM did not complete — full log:"
    tail -40 "$LOG"
    exit 2
fi
