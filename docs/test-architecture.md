# Test Architecture

LiME uses a three-tier testing strategy: compile testing across multiple kernel
versions, static analysis, and runtime smoke tests in QEMU virtual machines.
All tiers run in GitHub Actions CI and can be reproduced locally via Docker.

## Directory Layout

```text
test/
  kernel-versions.conf   # Pinned kernel versions (one per series)
  prepare-kernel.sh      # Downloads, configures, and strips kernel trees
  check-source.sh        # Grep-based static source checks
  qemu-smoke-test.sh     # QEMU boot harness (multi-arch)
  build-initramfs.sh     # Packs busybox + lime.ko into a cpio.gz
  smoke-init             # Init script (PID 1) inside the QEMU VM
  Dockerfile             # Ubuntu 24.04 image for local testing
  local.sh               # Self-dispatching host/container test runner
.github/workflows/
  build-test.yml         # CI workflow definition
```

## Tier 1 -- Compile Testing

Builds the module against eight pinned kernel series spanning the range of
supported kernel APIs:

| Series | API breakpoints covered                                    |
|--------|------------------------------------------------------------|
| 4.15   | crypto_ahash (4.6), kernel_write (4.14)                    |
| 5.4    |                                                            |
| 5.10   | sock_set_reuseaddr (5.8), set_fs() removed (5.10)          |
| 5.15   | kmap_local_page (5.11)                                     |
| 6.1    |                                                            |
| 6.6    |                                                            |
| 6.12   | -Wmissing-prototypes (6.8), PREEMPT_RT merged (6.12)       |
| 6.19   | sockaddr_unsized (6.19)                                    |

Each kernel series is tested with one or more build variants:

- **default** -- standard `make` (all series)
- **debug** -- `make debug`, enables DBG() printk output (5.15, 6.6)
- **symbols** -- `make symbols`, preserves debug info (5.15, 6.6)
- **werror** -- `KCFLAGS="-Werror"` (5.15, 6.6, 6.12, 6.19)

Additional kernel configs:

- **no-zlib** -- `CONFIG_ZLIB_DEFLATE` disabled (5.15, 6.6): verifies
  deflate.c is correctly excluded when compression is unavailable.
- **preempt-rt** -- `CONFIG_PREEMPT_RT` enabled (6.12): verifies
  compatibility with the RT patchset.

A **canary job** builds against the latest stable kernel (dynamically resolved
from kernel.org). It is allowed to fail and serves as an early warning for
upstream API changes.

### Kernel Tree Preparation

`prepare-kernel.sh` handles the full lifecycle:

1. Resolves the exact version from `kernel-versions.conf` (pinned) or
   git ls-remote (canary fallback).
2. Downloads the tarball from cdn.kernel.org.
3. Runs `defconfig`, then applies config-specific overrides via
   `scripts/config` (e.g., disabling zlib, enabling PREEMPT_RT, or
   adding QEMU console/virtio/initrd support).
4. Enables LiME requirements: `CONFIG_MODULES`, `CONFIG_CRYPTO`,
   `CONFIG_CRYPTO_HASH`, `CONFIG_CRYPTO_SHA256`, `CONFIG_INET`,
   `CONFIG_NET`.
5. Runs `modules_prepare` (lightweight, no full kernel build).
6. For `qemu` config only, builds the full kernel image (bzImage/Image/zImage).
7. Strips the tree to ~100-180 MB by removing source files, object files,
   and large directories (drivers, fs, mm, net, etc.), keeping only headers,
   scripts, Makefiles, and generated build artifacts.

`KBUILD_MODPOST_WARN=1` is set globally because `modules_prepare` does not
generate `Module.symvers`. Unresolved symbol warnings are acceptable at
compile time; actual module loading is verified in Tier 3.

## Tier 2 -- Static Analysis

Runs after Tier 1 completes. Reuses the cached 6.6-defconfig kernel tree
(same cache key = free cache hit).

### Sparse

Runs the kernel's sparse static analyzer in `C=2` mode (check all files,
not just recompiled ones):

```bash
make -C <kdir> M=<lime-src> C=2 modules
```

Fails if sparse reports any errors or warnings in LiME source files.

### Source Checks

`check-source.sh` runs grep-based validation (portable across macOS and
Linux, no dependencies beyond coreutils):

1. **Extern declarations** -- Non-static functions in tcp.c, disk.c, hash.c,
   and deflate.c must have matching `extern` declarations in lime.h.
2. **Format string safety** -- `resource_size_t` values must use
   `(unsigned long long)` cast with `%llx`, not bare `%lx` (truncates on
   32-bit PAE).
3. **SPDX headers** -- All .c and .h files must contain an
   `SPDX-License-Identifier` line.
4. **Pointer arithmetic** -- Warns on `void*` arithmetic without `(u8 *)`
   cast (GNU extension, not portable).
5. **Version guard consistency** -- `LINUX_VERSION_CODE >` without `=` is
   flagged (should usually be `>=` for "introduced in" checks).
6. **ERR_PTR cleanup** -- disk.c must have at least two `f = NULL` sites
   (success path and error path in dio_write_test) to prevent closing an
   error pointer.

## Tier 3 -- Runtime Smoke Tests

Boots real kernels in QEMU with a minimal initramfs containing busybox and
the compiled lime.ko. Runs against two kernel series in CI (5.15, 6.6).
Depends on Tier 1 to avoid wasting the expensive full kernel build when the
code has a compilation error.

### QEMU Configuration

`qemu-smoke-test.sh` selects architecture-specific settings based on `uname -m`:

| Architecture    | QEMU binary            | Kernel image          | Console |
|-----------------|------------------------|-----------------------|---------|
| x86_64          | qemu-system-x86_64     | arch/x86/boot/bzImage | ttyS0   |
| aarch64         | qemu-system-aarch64    | arch/arm64/boot/Image | ttyAMA0 |
| armv7l / armhf  | qemu-system-arm        | arch/arm/boot/zImage  | ttyAMA0 |
| riscv64         | qemu-system-riscv64    | arch/riscv/boot/Image | ttyS0   |

CI runs x86_64 only (GitHub Actions runners). Local testing via Docker
supports all four architectures.

The VM is configured with 256 MB RAM, 2 vCPUs, no reboot on panic, and a
180-second timeout. All I/O goes through the serial console (`-nographic`).

### Initramfs

`build-initramfs.sh` creates a minimal cpio.gz archive containing:

- `/bin/busybox` with symlinks for sh, mount, insmod, rmmod, od, wc, etc.
- `/lib/modules/lime.ko` (the compiled module)
- `/init` (copy of `smoke-init`)

### Test Cases

`smoke-init` runs as PID 1 inside the VM. Each test loads the module with
specific parameters, verifies the output, then unloads and cleans up (the VM
has only 256 MB of RAM, so tmpfs space is tight).

| Test | Parameters      | Verification                              |
|------|-----------------|-------------------------------------------|
| t1   | `format=lime`   | File exists, magic = `0x4C694D45`         |
| t2   | `format=raw`    | File exists, size saved as baseline       |
| t3   | `format=padded` | Output size >= RAW size (zero-fill)       |
| t4   | SHA-256 digest  | `.sha256` sidecar has 64 hex chars        |
| t5   | `compress=1`    | Compressed output < RAW baseline          |

Tests are independent; a failure in one does not block others. t4 is skipped
if the kernel's crypto subsystem lacks SHA-256. t5 is skipped if compression
is unavailable or if t2 failed (no baseline).

Results are reported as PASS/FAIL/SKIP counters. The final line
`SMOKE_TEST_RESULT=PASS` or `SMOKE_TEST_RESULT=FAIL` is parsed by the
harness to determine the exit code.

### What Is Not Tested

- TCP transport (network streaming)
- Direct I/O (`dio=1`)
- `localhostonly` parameter
- Memory edge cases (sparse RAM layouts, large gaps)
- Performance or stress testing
- Android devices

## CI Workflow

Defined in `.github/workflows/build-test.yml`.

**Triggers:** push to master, pull requests (filtered to src/test/workflow
paths), weekly schedule (Monday 06:00 UTC to keep caches alive), and manual
dispatch.

**Job dependency graph:**

```text
build (11 matrix entries)
  |
  +---> analyze (sparse + source checks)
  |
  +---> smoke-test (5.15, 6.6)

build-latest (canary, independent, allowed to fail)
```

`analyze` and `smoke-test` use `if: !cancelled()` so an unrelated build
failure (e.g., 4.15) does not block testing on 6.6.

**Timeouts:** 20 minutes for build/analyze jobs, 45 minutes for smoke tests.

### Caching Strategy

Each kernel tree is cached independently. Cache keys incorporate the kernel
series, config variant, pinned version, and a hash of `prepare-kernel.sh`:

```text
kernel-{series}-{config}-{pinned_version}-{script_hash}
```

Bumping one version in `kernel-versions.conf` invalidates only that series'
cache entries. Smoke tests use separate `kernel-qemu-*` keys since they
include the full kernel image.

**Budget:** ~2.5 GB total (1.6 GB compile trees + 700 MB QEMU trees +
180 MB canary). GitHub Actions allows 10 GB per repo.

## Local Testing

`test/local.sh` is a self-dispatching script: on the host it manages Docker,
inside the container it runs the tests.

### Setup

A Docker image (`lime-test`) is built from `test/Dockerfile` (Ubuntu 24.04
with build-essential, kernel toolchain, sparse, busybox-static, and QEMU
for x86, ARM, and RISC-V). A persistent Docker volume (`lime-kernel-cache`)
stores prepared kernel trees across runs.

### Commands

```bash
./test/local.sh build [kernel] [config] [builds]   # Compile test (default: 6.6)
./test/local.sh analyze                             # Sparse + source checks
./test/local.sh smoke [kernel]                      # QEMU runtime test
./test/local.sh all                                 # Full CI matrix
./test/local.sh clean                               # Remove Docker image and cache
./test/local.sh shell                               # Interactive shell in container
```

The source directory is mounted read-only (`-v $ROOT:/src:ro`); builds happen
in `/tmp/build` (a writable copy). KVM passthrough (`--device /dev/kvm`) is
enabled automatically when `/dev/kvm` exists on the host.

**Performance:** ~5 minutes per kernel on first run (full download + build),
~10 seconds per kernel on subsequent runs (cache hit). A full `all` run takes
~30 minutes cold, ~5 minutes warm.
