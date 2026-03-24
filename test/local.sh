#!/bin/bash
# local.sh — Run LiME CI tests locally in Docker.
#
# This script self-dispatches: on the host it launches Docker, inside
# the container it runs the actual tests.  One file, two modes.
#
# Usage:
#   ./test/local.sh build [kernel] [config] [builds]
#   ./test/local.sh analyze
#   ./test/local.sh smoke [kernel]
#   ./test/local.sh all
#   ./test/local.sh clean
#   ./test/local.sh shell
#
# Kernel trees are cached in a Docker volume and reused across runs.
# First run per kernel: ~5 min.  Subsequent runs: ~10 sec.

set -euo pipefail

# =========================================================================
#  CONTAINER MODE — runs inside Docker
# =========================================================================

if [ -n "${LIME_IN_DOCKER:-}" ]; then

    # Prepare a kernel tree, skipping if already cached.
    # Sets KDIR as a side effect.
    prepare_cached() {
        local kernel="$1" config="$2"
        local hash
        hash=$(cat /src/test/prepare-kernel.sh /src/test/kernel-versions.conf | sha256sum | cut -c1-12)
        KDIR="/cache/kernel-${kernel}-${config}-${hash}"

        if [ -d "$KDIR" ]; then
            echo "==> Cached: $KDIR"
        else
            echo "==> Preparing linux-${kernel} [${config}]..."
            LIME_KDIR="$KDIR" bash /src/test/prepare-kernel.sh "$kernel" "$config"
        fi
    }

    # Copy source to a writable directory (source is mounted read-only).
    setup_build() {
        rm -rf /tmp/build
        cp -r /src/src /tmp/build
    }

    # modules_prepare does not generate Module.symvers (requires full
    # kernel build).  KBUILD_MODPOST_WARN=1 lets modpost warn instead
    # of error on unresolved symbols — the QEMU smoke tests verify
    # actual module loading.
    export KBUILD_MODPOST_WARN=1

    cmd="${1:?}"; shift
    case "$cmd" in
        build)
            kernel="${1:-6.6}"; config="${2:-defconfig}"; builds="${3:-default}"
            prepare_cached "$kernel" "$config"
            setup_build
            cd /tmp/build
            for b in $builds; do
                echo "==> Build: $b"
                make clean 2>/dev/null || true
                case "$b" in
                    default) make KDIR="$KDIR" ;;
                    debug)   make debug KDIR="$KDIR" ;;
                    symbols) make symbols KDIR="$KDIR" ;;
                    werror)  KCFLAGS="-Werror" make KDIR="$KDIR" ;;
                esac
                file lime-*.ko | grep -q ELF
                echo "  $b: OK ($(ls -lh lime-*.ko | awk '{print $5}'))"
            done
            echo "==> All builds passed"
            ;;

        analyze)
            prepare_cached 6.6 defconfig
            setup_build
            echo "==> Sparse"
            make -C "$KDIR" M=/tmp/build C=2 modules 2>&1 | tee /tmp/sparse.log
            if grep -E '(error|warning):' /tmp/sparse.log | grep -q 'build/'; then
                echo "Sparse issues:"
                grep -E '(error|warning):' /tmp/sparse.log | grep 'build/'
                exit 1
            fi
            echo "==> Source checks"
            bash /src/test/check-source.sh
            ;;

        smoke)
            kernel="${1:-6.6}"
            prepare_cached "$kernel" qemu
            setup_build
            make -C /tmp/build KDIR="$KDIR"
            bash /src/test/qemu-smoke-test.sh "$KDIR" /tmp/build/lime-*.ko
            ;;

        *)  echo "Unknown container command: $cmd"; exit 1 ;;
    esac
    exit 0
fi

# =========================================================================
#  HOST MODE — launches Docker
# =========================================================================

IMAGE="lime-test"
VOLUME="lime-kernel-cache"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

ensure_image() {
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "==> Building Docker image (one-time)..."
        docker build -q -t "$IMAGE" "$ROOT/test"
    fi
}

ensure_volume() {
    docker volume inspect "$VOLUME" >/dev/null 2>&1 ||
        docker volume create "$VOLUME" >/dev/null
}

# Launch this same script inside Docker.
drun() {
    local kvm_flag=""
    [ -e /dev/kvm ] && kvm_flag="--device /dev/kvm"

    docker run --rm \
        -v "$ROOT:/src:ro" \
        -v "$VOLUME:/cache" \
        -e LIME_IN_DOCKER=1 \
        $kvm_flag \
        "$IMAGE" bash /src/test/local.sh "$@"
}

case "${1:-help}" in
    build|analyze|smoke)
        ensure_image; ensure_volume
        drun "$@"
        ;;

    all)
        ensure_image; ensure_volume
        echo "=== Tier 1: Compile tests ==="
        for k in 4.15 5.4 5.10 6.1; do
            drun build "$k"
        done
        drun build 5.15 defconfig "default debug symbols werror"
        drun build 6.6  defconfig "default debug symbols werror"
        drun build 6.12 defconfig "default werror"
        drun build 6.19  defconfig "default werror"
        drun build 5.15 no-zlib
        drun build 6.6  no-zlib
        drun build 6.12 preempt-rt

        echo ""
        echo "=== Tier 2: Static analysis ==="
        drun analyze

        echo ""
        echo "=== Tier 3: Smoke tests ==="
        drun smoke 5.15
        drun smoke 6.6

        echo ""
        echo "=== All tests passed ==="
        ;;

    clean)
        echo "Removing cache volume and image..."
        docker volume rm "$VOLUME" 2>/dev/null || true
        docker rmi "$IMAGE" 2>/dev/null || true
        echo "Done."
        ;;

    shell)
        ensure_image; ensure_volume
        local kvm_flag=""
        [ -e /dev/kvm ] && kvm_flag="--device /dev/kvm"
        docker run --rm -it \
            -v "$ROOT:/src:ro" \
            -v "$VOLUME:/cache" \
            -e LIME_IN_DOCKER=1 \
            $kvm_flag \
            "$IMAGE" bash
        ;;

    *)
        cat <<'EOF'
Usage: ./test/local.sh <command> [args...]

Commands:
  build [kernel] [config] [builds]   Compile test
  analyze                            Sparse + source checks
  smoke [kernel]                     QEMU runtime test
  all                                Full CI matrix
  clean                              Remove Docker image and cache
  shell                              Interactive shell in container

Kernels: 4.15  5.4  5.10  5.15  6.1  6.6  6.12  6.19
Configs: defconfig  no-zlib  preempt-rt
Builds:  default  debug  symbols  werror  (space-separated in quotes)

Examples:
  ./test/local.sh build                  Quick build against 6.6
  ./test/local.sh build 5.15 defconfig "default debug symbols werror"
  ./test/local.sh smoke 6.6             QEMU runtime test
  ./test/local.sh all                   Full CI (~30 min first run, ~5 min cached)
  ./test/local.sh clean                 Free disk space
EOF
        exit 1
        ;;
esac
