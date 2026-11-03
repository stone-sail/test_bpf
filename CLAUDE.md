# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`execsnoop` is a small eBPF tool: a tracepoint program on `sys_enter_execve` writes events into a
BPF ring buffer, and a user-space loader polls the buffer and prints `PID PPID COMM FILENAME`.
The whole project is three source files in `src/` plus a CMake build that compiles the BPF object,
generates a skeleton header, and builds libbpf and bpftool from submodules.

## Commands

Submodules must be present first (`third_party/libbpf`, `third_party/bpftool`):

```
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_C_COMPILER=clang-18    # or omit the flag to auto-pick clang >= 10
cmake --build build -j
cmake --install build --prefix /usr/local          # optional; installs bin/, libbpf.so.1, vmlinux.btf if present
```

- On CentOS 7 the README uses `cmake3`. The existing `build/` is configured with CMake 3.17.5.
- Configure only re-runs when CMakeLists.txt or the cache changes. Use `cmake --build build` for
  incremental builds. Delete `build/` for a clean rebuild; it is generated and gitignored.
- Run: `sudo ./build/bin/execsnoop [vmlinux.btf]`. It needs root and BTF, either a file argument or
  `/sys/kernel/btf/vmlinux`. Create a BTF file on a BTF-enabled host with `./export_btf.sh`.
- There is no test suite and no linter configured. Verify changes by building, and by running the
  loader on a kernel that supports BPF ring buffers (5.8+, not 5.19–6.3, see
  `docs/kernel-compatibility.md`).
- The CI workflow (`.github/workflows/build.yml`) only builds with clang-18 on Ubuntu 24.04 and
  publishes a GitHub release for `v*` tags. It does not run anything.

## Architecture (the parts that span files)

**Shared event layout.** `src/execsnoop.h` defines `struct event`, which both the BPF program and
the loader include. Any change to it affects both sides at once.

**Kernel side (`src/execsnoop.bpf.c`).** `handle_execve` reserves a record in the `events` ring buffer
(`BPF_MAP_TYPE_RINGBUF`), fills in `pid`, `comm`, and `filename` (`bpf_probe_read_user_str` of
`args[0]`), and reads `ppid` via `BPF_CORE_READ(task, real_parent, tgid)`. That CO-RE read is why
BTF is needed at runtime and why the object is compiled with `-g`.

**Types.** The BPF build includes `vmlinux.h`, which is a copy of the tracked `include/vmlinux_6.18.h`,
copied into `build/obj/` by CMake. The libbpf headers in `include/bpf/` are vendored copies of
upstream. CO-RE adjusts field offsets at load time, so the header version does not have to match
the running kernel.

**Build pipeline (`CMakeLists.txt`).**
1. Choose a clang (>= 10), with `-DCMAKE_C_COMPILER` taking precedence.
2. Build libbpf from `third_party/libbpf` with its own Makefile, and copy `libbpf.so.1` to `build/lib/`.
3. Build bpftool from `third_party/bpftool`. The `LLVM_VERSION` suffix follows the chosen clang-NN.
   The `NFPROTO_NETDEV` define is added only when the kernel headers lack it (CentOS 7).
4. Compile `execsnoop.bpf.c` as an OBJECT library (`-target bpf -g -O2`). Run
   `bpftool gen skeleton` on it to produce `build/obj/execsnoop.skel.h`.
5. Compile `execsnoop.c` against the skeleton and libbpf. The RPATH is `$ORIGIN/../lib`
   in the build tree and `$ORIGIN/../lib64` or `../lib` after install.

The loader's `build/` outputs are not tracked. `lib/libbpf.*` at the repo root is a prebuilt copy
that the build does not use.

**Loader lifecycle (`src/execsnoop.c`).** `main` checks BTF and warns about affected kernels, then
opens the skeleton with the BTF path, loads, attaches, and creates a `ring_buffer` on the `events`
map fd. It polls every 100 ms until SIGINT/SIGTERM. Cleanup is `ring_buffer__free` followed by
`execsnoop_bpf__destroy`. Note the early-exit paths: BTF failure returns before the skeleton is opened.

**Kernel-version gate.** The warning in `warn_affected_kernel()` and the matrix in
`docs/kernel-compatibility.md` both describe the same range (upstream 5.19–6.3). Keep them in sync.

## Limits to keep in mind

- Only `execve` is reported. Exit status and arguments are not captured.
- The development host is CentOS 7 (kernel 3.10). It can build the project, but the loader cannot run
  there because the kernel has no BPF support (`failed to load BPF skeleton: -38`). Test runs need a
  modern kernel.
