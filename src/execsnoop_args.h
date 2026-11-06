#ifndef EXECSNOOP_ARGS_H
#define EXECSNOOP_ARGS_H

/* Command-line option names; kept as strings for string-keyed lookups. */
#define EXECSNOOP_ARG_KTYP   "ktype"
#define EXECSNOOP_ARG_UTYP   "utype"
#define EXECSNOOP_ARG_BTF    "btf"
#define EXECSNOOP_KTYP_BPF   "bpf"
#define EXECSNOOP_KTYP_ORIGIN "origin"
#define EXECSNOOP_UTYP_TRACEPOINT_SYS_ENTER_EXECVE "tracepoint_sys_enter_execve"
#define SYS_BTF "/sys/kernel/btf/vmlinux"

#endif
