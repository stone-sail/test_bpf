#!/usr/bin/env bash
# Copy this machine's kernel BTF so it can be shipped next to execsnoop and
# passed as the argument:  ./execsnoop vmlinux.btf
# Run on a kernel built with CONFIG_DEBUG_INFO_BTF=y (5.8+ typically).
set -euo pipefail

SRC=/sys/kernel/btf/vmlinux
DST="$(dirname "$0")/vmlinux.btf"

if [ ! -r "$SRC" ]; then
    echo "error: $SRC not found; this kernel does not expose BTF" >&2
    exit 1
fi

cp "$SRC" "$DST"
echo "wrote $DST ($(stat -c %s "$DST") bytes)"
