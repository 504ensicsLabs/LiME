#!/bin/bash
# check-source.sh — Static source checks for LiME.
# Catches common mistakes that compile testing alone misses.
# Runs on both macOS (dev) and Linux (CI) — avoids grep -P.

set -euo pipefail

SRC="$(cd "$(dirname "$0")/../src" && pwd)"
ERRORS=0
WARNINGS=0

err()  { ERRORS=$((ERRORS+1));   echo "  ERROR: $1"; }
warn() { WARNINGS=$((WARNINGS+1)); echo "  WARN:  $1"; }

echo "=== LiME Source Checks ==="

##
## 1. Non-static functions in the transport/hash/deflate files must
##    have matching extern declarations in lime.h.
##
echo "--- Extern declarations ---"
for f in "$SRC"/tcp.c "$SRC"/disk.c "$SRC"/hash.c "$SRC"/deflate.c; do
    [ -f "$f" ] || continue
    base=$(basename "$f")

    # Find function definitions at column 0 that aren't static or preprocessor
    grep -n '^[a-zA-Z]' "$f" | grep -v '^[0-9]*:static ' | grep -v '^[0-9]*:#' |
        grep '(' | while IFS=: read -r line content; do
            # Extract the word immediately before the first (
            func=$(echo "$content" | sed 's/(.*//' | awk '{print $NF}' | tr -d '* ')
            [ -z "$func" ] && continue
            # Skip obvious non-functions
            case "$func" in if|while|for|switch|return|sizeof) continue ;; esac
            # Check lime.h
            if ! grep -q "extern.*${func} *(" "$SRC/lime.h"; then
                err "$base:$line: '$func' missing extern in lime.h"
            fi
        done
done

##
## 2. resource_size_t in format strings must use %llx with cast
##    (bare %lx truncates on 32-bit PAE; bare %llx is UB on 32-bit non-PAE)
##
echo "--- Format string safety ---"
if grep -n 'DBG.*%l[dux]' "$SRC"/*.c | grep -E '(p|res)->(start|end)'; then
    err "resource_size_t printed with %lx — use (unsigned long long) cast + %llx"
fi
if grep -n 'DBG.*%p.*(void' "$SRC"/*.c | grep -v '%pa'; then
    err "%p with (void*) cast — use %llx for physical addresses"
fi

##
## 3. SPDX license headers
##
echo "--- SPDX headers ---"
for f in "$SRC"/*.c "$SRC"/*.h; do
    if ! grep -q 'SPDX-License-Identifier' "$f"; then
        err "$(basename "$f"): missing SPDX header"
    fi
done

##
## 4. Void pointer arithmetic (GNU extension, -Wpointer-arith)
##
echo "--- Pointer arithmetic ---"
if grep -n 'v +=' "$SRC"/*.c | grep -v '(u8 \*)' | grep -v '//'; then
    warn "void* arithmetic — prefer (u8 *) cast for portability"
fi

##
## 5. Version guard consistency
##    > KERNEL_VERSION is almost always a bug — should be >=
##
echo "--- Version guards ---"
if grep -n 'LINUX_VERSION_CODE *>' "$SRC"/*.c "$SRC"/*.h |
   grep -v '>='; then
    warn "> KERNEL_VERSION without = (should usually be >= for 'introduced in' checks)"
fi

##
## 6. ERR_PTR hygiene in disk.c
##    After a failed filp_open, f must be set to NULL so that cleanup_disk's
##    if(f) check doesn't try to close an error pointer.
##
echo "--- ERR_PTR cleanup ---"
if grep -c 'f = NULL' "$SRC/disk.c" | grep -q '^[01]$'; then
    err "disk.c: expected at least 2 'f = NULL' sites (success + error paths in dio_write_test)"
fi

echo ""
if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo "=== All checks passed ==="
elif [ $ERRORS -eq 0 ]; then
    echo "=== $WARNINGS warning(s), no errors ==="
else
    echo "=== $ERRORS error(s), $WARNINGS warning(s) ==="
fi
exit $ERRORS
