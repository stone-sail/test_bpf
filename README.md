# execsnoop (test_perf)

## Overview

`execsnoop` prints every `execve` on the system: PID, parent PID, command name, and
the path being executed. A BPF program attached to the `sys_enter_execve` tracepoint
writes events into a ring buffer; the user-space loader reads the buffer and prints.

## Layout

```
src/
  execsnoop.bpf.c      BPF program (tracepoint, ring buffer, CO-RE read of parent tgid)
  execsnoop.c          user-space loader (open/load/attach, ring buffer polling)
  execsnoop.h          event structure shared by both
  vmlinux_6.18.h       bundled kernel type definitions (used as vmlinux.h)
include/bpf/           libbpf headers, vendored (copied from upstream libbpf)
lib/                   libbpf shared and static libraries (prebuilt, not used by the build yet)
export_btf.sh          copies /sys/kernel/btf/vmlinux to vmlinux.btf for shipping
CMakeLists.txt         build and install rules
.clangd                editor configuration (reads build/compile_commands.json)
```

Build outputs go to `build/` and are not committed:

```
build/bin/execsnoop    the program
build/lib/             libbpf.so.1 (built from third_party/libbpf)
build/bpftool/         bpftool (built from third_party/bpftool)
build/obj/             vmlinux.h, execsnoop.skel.h, object files
build/compile_commands.json
```

## Build

After cloning, fetch the libbpf and bpftool submodules:

```
git submodule update --init --recursive
```

Then:

```
cmake3 -S . -B build      # or: cmake -S . -B build (CMake 3.10+)
cmake3 --build build
```

The build needs:

- Linux. Other systems stop at configure time.
- clang 10 or newer. The first candidate that reports version 10+ is used:
  `/opt/llvm-10/bin/clang`, then `clang-10` … `clang-18`, then `clang`.
  Pass `-DCMAKE_C_COMPILER=<clang>` to choose one explicitly.
- `make`, to build libbpf and bpftool.
- libelf and zlib development files. If they are missing, configure stops and prints
  the package command for your distribution (see below).

## Install

```
cmake3 --install build --prefix /usr/local     # or any other prefix
```

| Path | Content |
|------|---------|
| `<prefix>/bin/execsnoop` | the program |
| `<prefix>/lib64/libbpf.so.1` (RHEL family) or `<prefix>/lib/libbpf.so.1` (Debian family) | libbpf |
| `<prefix>/share/execsnoop/vmlinux.btf` | only if `vmlinux.btf` exists in this directory at install time |

The binary finds libbpf through its RPATH (`$ORIGIN/../lib64` or `$ORIGIN/../lib`),
so `LD_LIBRARY_PATH` is not needed.

## Supported systems

| System | Install library dir | Missing-dependency hint | Status |
|--------|---------------------|-------------------------|--------|
| CentOS 7 (and other RHEL family, e.g. Rocky, Fedora) | `lib64` | `yum install elfutils-libelf-devel zlib-devel` | built and run-checked on CentOS 7.2 |
| Ubuntu / Debian | `lib` | `apt-get install libelf-dev zlib1g-dev` | detection and hints only; not built yet |
| Other Linux | `lib` | generic message | not tested |
| Non-Linux | — | configure fails | not supported (BPF) |

The system is read from `/etc/os-release` (`ID` and `ID_LIKE`).

## Runtime requirements

- Root, or the BPF capabilities of your kernel.
- Kernel 5.8 or newer: the program uses ring buffers.
- BTF for CO-RE relocations. The program chooses it in this order:
  1. the file passed on the command line: `./execsnoop /path/to/vmlinux.btf`;
  2. `/sys/kernel/btf/vmlinux`, when the kernel exposes it (`CONFIG_DEBUG_INFO_BTF=y`);
  3. otherwise it stops and prints what to do.

  Create a BTF file on a BTF-enabled host with `./export_btf.sh`.
- Not in the affected kernel range: upstream 5.19 through 6.3 (including 6.1 LTS).
  execsnoop warns at startup on those kernels. See
  [docs/kernel-compatibility.md](docs/kernel-compatibility.md) for the full matrix.

On a kernel without BPF support (for example the CentOS 7 3.10 kernel) the loader
reports `failed to load BPF skeleton: -38` and exits.

## Lifecycle

1. `main` opens the BPF skeleton, with the optional BTF path.
2. `execsnoop_bpf__load` loads the program and maps.
3. `execsnoop_bpf__attach` attaches the tracepoint.
4. `ring_buffer__poll` loops every 100 ms until SIGINT or SIGTERM.
5. `ring_buffer__free` and `execsnoop_bpf__destroy` release resources.

## Data flow

1. The kernel runs `handle_execve` on every `execve` call.
2. The program reserves a record in the `events` ring buffer and fills in `pid`, `ppid`,
   `comm` and `filename`.
3. The loader prints each record as `PID PPID COMM FILENAME`.

## Known limits

- The program only reports `execve`. It does not report the exit status or the arguments.
- `ppid` is read with `BPF_CORE_READ`, so the BPF object must be built with `-g`.
- The vendored `vmlinux.h` is for kernel 6.18 types. Compiling against it on other kernels
  is fine, because CO-RE fixes field offsets at load time, but the BTF on the target
  machine must be the one that matches the kernel you run on.

## Related docs

- [docs/kernel-compatibility.md](docs/kernel-compatibility.md) — kernel versions to avoid and BTF selection
- `third_party/libbpf` — libbpf submodule (https://github.com/libbpf/libbpf), used to build `build/lib`
- `third_party/bpftool` — bpftool submodule (https://github.com/libbpf/bpftool), used to build `build/bpftool`
