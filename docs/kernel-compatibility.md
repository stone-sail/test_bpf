# Kernel compatibility

execsnoop needs three things from the running kernel:

| Requirement | Why | Check |
|-------------|-----|-------|
| 5.8 or newer | ring buffer (`BPF_MAP_TYPE_RINGBUF`) | `uname -r` |
| `sys_enter_execve` tracepoint, `bpf_probe_read_user_str` | event collection | `/sys/kernel/tracing/events/syscalls/sys_enter_execve` |
| BTF for CO-RE | `BPF_CORE_READ` of `real_parent->tgid` | `/sys/kernel/btf/vmlinux` exists, or pass a BTF file |

## Kernels to avoid

Upstream kernels from **5.19 through 6.3** (this includes the 6.1 LTS line) are in
the range affected by the `copy_from_user_nofault()` bug. The execsnoop program
reads user memory with `bpf_probe_read_user_str`, which is in the same family of
nofault user-access paths, so the same range applies. The failure mode can be a
host lockup.

execsnoop prints a warning at startup on these kernels. The warning does not stop
the program, because vendor kernels can carry the upstream fix as a backport.

Upstream fix: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=d319f344561de23e810515d109c7278919bff7b0
Discussion: https://lore.kernel.org/bpf/20230118213202.3859786-1-hsinweih@uci.edu/T/

## Version guidance

| Kernel line | Guidance | Notes |
|-------------|----------|-------|
| Upstream 5.19.x – 6.3.x | Do not use | Affected by the nofault user-copy bug (includes 6.1 LTS). |
| Upstream 6.4 and newer | Supported baseline | The fix is in the 6.4 development cycle. |
| 5.15 LTS and vendor kernels | Vendor-patched only | Use only when the vendor confirms the fix is backported. |
| 5.10 LTS and older | Not recommended without validation | Check BTF and BPF ring buffer support first. |
| CentOS 7 (3.10) | Not supported | No ring buffer, no BTF. Use only to build; it cannot load the program. |

For distribution kernels, do not rely on `uname -r` alone. Check the vendor
changelog for the upstream fix commit before deploying.

## BTF on the target host

The program chooses kernel BTF in this order:

1. the file given on the command line (`./execsnoop vmlinux.btf`);
2. `/sys/kernel/btf/vmlinux`, if the kernel exposes it;
3. otherwise it stops with instructions.

Create a BTF file on a BTF-enabled host with `./export_btf.sh`. Use a file from
the same kernel that will run execsnoop.
