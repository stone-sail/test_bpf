# 其他挂载类型：依赖 BTF 的类型、网络、cgroup 等

每个表按"挂载类型、类别（子系统）、事件名、描述、例子、头文件"整理。总览见 [bpf_known.md](bpf_known.md)。数据来自测试节点（内核 4.18.0-193.el8.jd_001.x86_64）和仓库内 libbpf，只代表该内核。

## 依赖 BTF 函数信息的类型（本节点不可用）

- 测试节点的 `/sys/kernel/btf/vmlinux` 中 FUNC 记录数为 0（STRUCT 7762、FUNC_PROTO 3782），没有函数级 BTF，因此下列类型在该节点上无法使用，也无法列出可挂载的函数。
- 在支持的内核上，可用 `bpftool btf dump file /sys/kernel/btf/vmlinux format raw | grep "] FUNC "` 列出函数。

| 挂载类型 | 类别（子系统） | 事件名 | 描述 | 例子 | 头文件 |
|---|---|---|---|---|---|
| `fentry` | 内核函数（依赖 BTF FUNC） | 函数名 | 函数入口，参数直接可读（本节点不可用） | `SEC("fentry/tcp_v4_connect")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `fexit` | 内核函数（依赖 BTF FUNC） | 函数名 | 函数出口，可同时读参数和返回值（本节点不可用） | `SEC("fexit/tcp_v4_connect")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `fmod_ret` | 内核函数（依赖 BTF FUNC） | 函数名 | 修改函数返回值（本节点不可用） | `SEC("fmod_ret/<函数名>")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `fsession` | 内核函数（依赖 BTF FUNC） | 函数名 | 同一程序处理入口和出口（本节点不可用） | `SEC("fsession/<函数名>")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `freplace` | BPF 程序中的函数 | 函数名 | 替换另一个 BPF 程序中的函数（本节点不可用） | `SEC("freplace/<函数名>")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `lsm` | LSM 钩子（BTF 中 bpf_lsm_*） | 钩子名 | 安全钩子，需要启用 BPF LSM（本节点不可用） | `SEC("lsm/file_open")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `lsm_cgroup` | LSM 钩子（按 cgroup） | 钩子名 | 按 cgroup 生效的 LSM 钩子（本节点不可用） | `SEC("lsm_cgroup/socket_bind")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `tp_btf` | tracepoint（BTF 类型） | 事件名 | 通过 BTF 读取 tracepoint 参数（本节点不可用） | `SEC("tp_btf/sched_switch")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `iter` | 内核对象迭代器 | 对象类型 | 遍历内核对象（本节点不可用） | `SEC("iter/task")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |
| `struct_ops` | 内核结构体操作表 | 结构体成员 | 用 BPF 实现内核回调（本节点不可用） | `SEC("struct_ops/<名称>")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` |

`kprobe.multi`、`kretprobe.multi`、`kprobe.session`、`uprobe.multi` 等批量挂载类型依赖较新的内核（上游 5.18 起），未在本节点验证。

## 网络、cgroup 与其他固定挂载点（libbpf `section_defs` 全表）

以下来自仓库内 `third_party/libbpf/src/libbpf.c` 的 `section_defs`，共 110 条。前缀以 `+` 结尾表示后面要接名称（如 `kprobe/<函数名>`），否则 SEC 就写这个前缀本身。"描述"列为该前缀的程序类型和挂载标志。

| 挂载类型 | 类别（子系统） | 事件名 | 描述 | 例子 | 头文件 |
|---|---|---|---|---|---|
| `socket` | `BPF_PROG_TYPE_SOCKET_FILTER` | `0` | 挂载标志 `SEC_NONE` | `SEC("socket")` | `<bpf/bpf_helpers.h>` |
| `sk_reuseport/migrate` | `BPF_PROG_TYPE_SK_REUSEPORT` | `BPF_SK_REUSEPORT_SELECT_OR_MIGRATE` | 挂载标志 `SEC_ATTACHABLE` | `SEC("sk_reuseport/migrate")` | `<bpf/bpf_helpers.h>` |
| `sk_reuseport` | `BPF_PROG_TYPE_SK_REUSEPORT` | `BPF_SK_REUSEPORT_SELECT` | 挂载标志 `SEC_ATTACHABLE` | `SEC("sk_reuseport")` | `<bpf/bpf_helpers.h>` |
| `kprobe+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_NONE` | `SEC("kprobe/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uprobe+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_NONE` | `SEC("uprobe/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uprobe.s+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("uprobe.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `kretprobe+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_NONE` | `SEC("kretprobe/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uretprobe+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_NONE` | `SEC("uretprobe/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uretprobe.s+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("uretprobe.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `kprobe.multi+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_KPROBE_MULTI` | 挂载标志 `SEC_NONE` | `SEC("kprobe.multi/<名称>")` | `<bpf/bpf_tracing.h>` |
| `kretprobe.multi+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_KPROBE_MULTI` | 挂载标志 `SEC_NONE` | `SEC("kretprobe.multi/<名称>")` | `<bpf/bpf_tracing.h>` |
| `kprobe.session+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_KPROBE_SESSION` | 挂载标志 `SEC_NONE` | `SEC("kprobe.session/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uprobe.multi+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_UPROBE_MULTI` | 挂载标志 `SEC_NONE` | `SEC("uprobe.multi/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uretprobe.multi+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_UPROBE_MULTI` | 挂载标志 `SEC_NONE` | `SEC("uretprobe.multi/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uprobe.session+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_UPROBE_SESSION` | 挂载标志 `SEC_NONE` | `SEC("uprobe.session/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uprobe.multi.s+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_UPROBE_MULTI` | 挂载标志 `SEC_SLEEPABLE` | `SEC("uprobe.multi.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uretprobe.multi.s+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_UPROBE_MULTI` | 挂载标志 `SEC_SLEEPABLE` | `SEC("uretprobe.multi.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `uprobe.session.s+` | `BPF_PROG_TYPE_KPROBE` | `BPF_TRACE_UPROBE_SESSION` | 挂载标志 `SEC_SLEEPABLE` | `SEC("uprobe.session.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `ksyscall+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_NONE` | `SEC("ksyscall/<名称>")` | `<bpf/bpf_tracing.h>` |
| `kretsyscall+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_NONE` | `SEC("kretsyscall/<名称>")` | `<bpf/bpf_tracing.h>` |
| `usdt+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_USDT` | `SEC("usdt/<名称>")` | `<bpf/bpf_tracing.h>` |
| `usdt.s+` | `BPF_PROG_TYPE_KPROBE` | `0` | 挂载标志 `SEC_USDT / SEC_SLEEPABLE` | `SEC("usdt.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `tc/ingress` | `BPF_PROG_TYPE_SCHED_CLS` | `BPF_TCX_INGRESS` | 挂载标志 `SEC_NONE` | `SEC("tc/ingress")` | `<bpf/bpf_helpers.h>` |
| `tc/egress` | `BPF_PROG_TYPE_SCHED_CLS` | `BPF_TCX_EGRESS` | 挂载标志 `SEC_NONE` | `SEC("tc/egress")` | `<bpf/bpf_helpers.h>` |
| `tcx/ingress` | `BPF_PROG_TYPE_SCHED_CLS` | `BPF_TCX_INGRESS` | 挂载标志 `SEC_NONE` | `SEC("tcx/ingress")` | `<bpf/bpf_helpers.h>` |
| `tcx/egress` | `BPF_PROG_TYPE_SCHED_CLS` | `BPF_TCX_EGRESS` | 挂载标志 `SEC_NONE` | `SEC("tcx/egress")` | `<bpf/bpf_helpers.h>` |
| `tc` | `BPF_PROG_TYPE_SCHED_CLS` | `0` | 挂载标志 `SEC_NONE` | `SEC("tc")` | `<bpf/bpf_helpers.h>` |
| `classifier` | `BPF_PROG_TYPE_SCHED_CLS` | `0` | 挂载标志 `SEC_NONE` | `SEC("classifier")` | `<bpf/bpf_helpers.h>` |
| `action` | `BPF_PROG_TYPE_SCHED_ACT` | `0` | 挂载标志 `SEC_NONE` | `SEC("action")` | `<bpf/bpf_helpers.h>` |
| `netkit/primary` | `BPF_PROG_TYPE_SCHED_CLS` | `BPF_NETKIT_PRIMARY` | 挂载标志 `SEC_NONE` | `SEC("netkit/primary")` | `<bpf/bpf_helpers.h>` |
| `netkit/peer` | `BPF_PROG_TYPE_SCHED_CLS` | `BPF_NETKIT_PEER` | 挂载标志 `SEC_NONE` | `SEC("netkit/peer")` | `<bpf/bpf_helpers.h>` |
| `tracepoint+` | `BPF_PROG_TYPE_TRACEPOINT` | `0` | 挂载标志 `SEC_NONE` | `SEC("tracepoint/<名称>")` | `<bpf/bpf_tracing.h>` |
| `tp+` | `BPF_PROG_TYPE_TRACEPOINT` | `0` | 挂载标志 `SEC_NONE` | `SEC("tp/<名称>")` | `<bpf/bpf_tracing.h>` |
| `tracepoint.s+` | `BPF_PROG_TYPE_TRACEPOINT` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("tracepoint.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `tp.s+` | `BPF_PROG_TYPE_TRACEPOINT` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("tp.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `raw_tracepoint+` | `BPF_PROG_TYPE_RAW_TRACEPOINT` | `0` | 挂载标志 `SEC_NONE` | `SEC("raw_tracepoint/<名称>")` | `<bpf/bpf_tracing.h>` |
| `raw_tp+` | `BPF_PROG_TYPE_RAW_TRACEPOINT` | `0` | 挂载标志 `SEC_NONE` | `SEC("raw_tp/<名称>")` | `<bpf/bpf_tracing.h>` |
| `raw_tracepoint.s+` | `BPF_PROG_TYPE_RAW_TRACEPOINT` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("raw_tracepoint.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `raw_tp.s+` | `BPF_PROG_TYPE_RAW_TRACEPOINT` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("raw_tp.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `raw_tracepoint.w+` | `BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE` | `0` | 挂载标志 `SEC_NONE` | `SEC("raw_tracepoint.w/<名称>")` | `<bpf/bpf_helpers.h>` |
| `raw_tp.w+` | `BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE` | `0` | 挂载标志 `SEC_NONE` | `SEC("raw_tp.w/<名称>")` | `<bpf/bpf_helpers.h>` |
| `tp_btf+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_RAW_TP` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("tp_btf/<名称>")` | `<bpf/bpf_tracing.h>` |
| `tp_btf.s+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_RAW_TP` | 挂载标志 `SEC_ATTACH_BTF / SEC_SLEEPABLE` | `SEC("tp_btf.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fentry+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_FENTRY` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("fentry/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fmod_ret+` | `BPF_PROG_TYPE_TRACING` | `BPF_MODIFY_RETURN` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("fmod_ret/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fexit+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_FEXIT` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("fexit/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fentry.s+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_FENTRY` | 挂载标志 `SEC_ATTACH_BTF / SEC_SLEEPABLE` | `SEC("fentry.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fmod_ret.s+` | `BPF_PROG_TYPE_TRACING` | `BPF_MODIFY_RETURN` | 挂载标志 `SEC_ATTACH_BTF / SEC_SLEEPABLE` | `SEC("fmod_ret.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fexit.s+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_FEXIT` | 挂载标志 `SEC_ATTACH_BTF / SEC_SLEEPABLE` | `SEC("fexit.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fsession+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_FSESSION` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("fsession/<名称>")` | `<bpf/bpf_tracing.h>` |
| `fsession.s+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_FSESSION` | 挂载标志 `SEC_ATTACH_BTF / SEC_SLEEPABLE` | `SEC("fsession.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `freplace+` | `BPF_PROG_TYPE_EXT` | `0` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("freplace/<名称>")` | `<bpf/bpf_helpers.h>` |
| `lsm+` | `BPF_PROG_TYPE_LSM` | `BPF_LSM_MAC` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("lsm/<名称>")` | `<bpf/bpf_tracing.h>` |
| `lsm.s+` | `BPF_PROG_TYPE_LSM` | `BPF_LSM_MAC` | 挂载标志 `SEC_ATTACH_BTF / SEC_SLEEPABLE` | `SEC("lsm.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `lsm_cgroup+` | `BPF_PROG_TYPE_LSM` | `BPF_LSM_CGROUP` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("lsm_cgroup/<名称>")` | `<bpf/bpf_tracing.h>` |
| `iter+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_ITER` | 挂载标志 `SEC_ATTACH_BTF` | `SEC("iter/<名称>")` | `<bpf/bpf_tracing.h>` |
| `iter.s+` | `BPF_PROG_TYPE_TRACING` | `BPF_TRACE_ITER` | 挂载标志 `SEC_ATTACH_BTF / SEC_SLEEPABLE` | `SEC("iter.s/<名称>")` | `<bpf/bpf_tracing.h>` |
| `syscall` | `BPF_PROG_TYPE_SYSCALL` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("syscall")` | `<bpf/bpf_helpers.h>` |
| `xdp.frags/devmap` | `BPF_PROG_TYPE_XDP` | `BPF_XDP_DEVMAP` | 挂载标志 `SEC_XDP_FRAGS` | `SEC("xdp.frags/devmap")` | `<bpf/bpf_helpers.h>` |
| `xdp/devmap` | `BPF_PROG_TYPE_XDP` | `BPF_XDP_DEVMAP` | 挂载标志 `SEC_ATTACHABLE` | `SEC("xdp/devmap")` | `<bpf/bpf_helpers.h>` |
| `xdp.frags/cpumap` | `BPF_PROG_TYPE_XDP` | `BPF_XDP_CPUMAP` | 挂载标志 `SEC_XDP_FRAGS` | `SEC("xdp.frags/cpumap")` | `<bpf/bpf_helpers.h>` |
| `xdp/cpumap` | `BPF_PROG_TYPE_XDP` | `BPF_XDP_CPUMAP` | 挂载标志 `SEC_ATTACHABLE` | `SEC("xdp/cpumap")` | `<bpf/bpf_helpers.h>` |
| `xdp.frags` | `BPF_PROG_TYPE_XDP` | `BPF_XDP` | 挂载标志 `SEC_XDP_FRAGS` | `SEC("xdp.frags")` | `<bpf/bpf_helpers.h>` |
| `xdp` | `BPF_PROG_TYPE_XDP` | `BPF_XDP` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("xdp")` | `<bpf/bpf_helpers.h>` |
| `perf_event` | `BPF_PROG_TYPE_PERF_EVENT` | `0` | 挂载标志 `SEC_NONE` | `SEC("perf_event")` | `<bpf/bpf_tracing.h>` |
| `lwt_in` | `BPF_PROG_TYPE_LWT_IN` | `0` | 挂载标志 `SEC_NONE` | `SEC("lwt_in")` | `<bpf/bpf_helpers.h>` |
| `lwt_out` | `BPF_PROG_TYPE_LWT_OUT` | `0` | 挂载标志 `SEC_NONE` | `SEC("lwt_out")` | `<bpf/bpf_helpers.h>` |
| `lwt_xmit` | `BPF_PROG_TYPE_LWT_XMIT` | `0` | 挂载标志 `SEC_NONE` | `SEC("lwt_xmit")` | `<bpf/bpf_helpers.h>` |
| `lwt_seg6local` | `BPF_PROG_TYPE_LWT_SEG6LOCAL` | `0` | 挂载标志 `SEC_NONE` | `SEC("lwt_seg6local")` | `<bpf/bpf_helpers.h>` |
| `sockops` | `BPF_PROG_TYPE_SOCK_OPS` | `BPF_CGROUP_SOCK_OPS` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("sockops")` | `<bpf/bpf_helpers.h>` |
| `sk_skb/stream_parser` | `BPF_PROG_TYPE_SK_SKB` | `BPF_SK_SKB_STREAM_PARSER` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("sk_skb/stream_parser")` | `<bpf/bpf_helpers.h>` |
| `sk_skb/stream_verdict` | `BPF_PROG_TYPE_SK_SKB` | `BPF_SK_SKB_STREAM_VERDICT` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("sk_skb/stream_verdict")` | `<bpf/bpf_helpers.h>` |
| `sk_skb/verdict` | `BPF_PROG_TYPE_SK_SKB` | `BPF_SK_SKB_VERDICT` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("sk_skb/verdict")` | `<bpf/bpf_helpers.h>` |
| `sk_skb` | `BPF_PROG_TYPE_SK_SKB` | `0` | 挂载标志 `SEC_NONE` | `SEC("sk_skb")` | `<bpf/bpf_helpers.h>` |
| `sk_msg` | `BPF_PROG_TYPE_SK_MSG` | `BPF_SK_MSG_VERDICT` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("sk_msg")` | `<bpf/bpf_helpers.h>` |
| `lirc_mode2` | `BPF_PROG_TYPE_LIRC_MODE2` | `BPF_LIRC_MODE2` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("lirc_mode2")` | `<bpf/bpf_helpers.h>` |
| `flow_dissector` | `BPF_PROG_TYPE_FLOW_DISSECTOR` | `BPF_FLOW_DISSECTOR` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("flow_dissector")` | `<bpf/bpf_helpers.h>` |
| `cgroup_skb/ingress` | `BPF_PROG_TYPE_CGROUP_SKB` | `BPF_CGROUP_INET_INGRESS` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("cgroup_skb/ingress")` | `<bpf/bpf_helpers.h>` |
| `cgroup_skb/egress` | `BPF_PROG_TYPE_CGROUP_SKB` | `BPF_CGROUP_INET_EGRESS` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("cgroup_skb/egress")` | `<bpf/bpf_helpers.h>` |
| `cgroup/skb` | `BPF_PROG_TYPE_CGROUP_SKB` | `0` | 挂载标志 `SEC_NONE` | `SEC("cgroup/skb")` | `<bpf/bpf_helpers.h>` |
| `cgroup/sock_create` | `BPF_PROG_TYPE_CGROUP_SOCK` | `BPF_CGROUP_INET_SOCK_CREATE` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/sock_create")` | `<bpf/bpf_helpers.h>` |
| `cgroup/sock_release` | `BPF_PROG_TYPE_CGROUP_SOCK` | `BPF_CGROUP_INET_SOCK_RELEASE` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/sock_release")` | `<bpf/bpf_helpers.h>` |
| `cgroup/sock` | `BPF_PROG_TYPE_CGROUP_SOCK` | `BPF_CGROUP_INET_SOCK_CREATE` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("cgroup/sock")` | `<bpf/bpf_helpers.h>` |
| `cgroup/post_bind4` | `BPF_PROG_TYPE_CGROUP_SOCK` | `BPF_CGROUP_INET4_POST_BIND` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/post_bind4")` | `<bpf/bpf_helpers.h>` |
| `cgroup/post_bind6` | `BPF_PROG_TYPE_CGROUP_SOCK` | `BPF_CGROUP_INET6_POST_BIND` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/post_bind6")` | `<bpf/bpf_helpers.h>` |
| `cgroup/bind4` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET4_BIND` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/bind4")` | `<bpf/bpf_helpers.h>` |
| `cgroup/bind6` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET6_BIND` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/bind6")` | `<bpf/bpf_helpers.h>` |
| `cgroup/connect4` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET4_CONNECT` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/connect4")` | `<bpf/bpf_helpers.h>` |
| `cgroup/connect6` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET6_CONNECT` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/connect6")` | `<bpf/bpf_helpers.h>` |
| `cgroup/connect_unix` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UNIX_CONNECT` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/connect_unix")` | `<bpf/bpf_helpers.h>` |
| `cgroup/sendmsg4` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UDP4_SENDMSG` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/sendmsg4")` | `<bpf/bpf_helpers.h>` |
| `cgroup/sendmsg6` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UDP6_SENDMSG` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/sendmsg6")` | `<bpf/bpf_helpers.h>` |
| `cgroup/sendmsg_unix` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UNIX_SENDMSG` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/sendmsg_unix")` | `<bpf/bpf_helpers.h>` |
| `cgroup/recvmsg4` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UDP4_RECVMSG` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/recvmsg4")` | `<bpf/bpf_helpers.h>` |
| `cgroup/recvmsg6` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UDP6_RECVMSG` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/recvmsg6")` | `<bpf/bpf_helpers.h>` |
| `cgroup/recvmsg_unix` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UNIX_RECVMSG` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/recvmsg_unix")` | `<bpf/bpf_helpers.h>` |
| `cgroup/getpeername4` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET4_GETPEERNAME` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/getpeername4")` | `<bpf/bpf_helpers.h>` |
| `cgroup/getpeername6` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET6_GETPEERNAME` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/getpeername6")` | `<bpf/bpf_helpers.h>` |
| `cgroup/getpeername_unix` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UNIX_GETPEERNAME` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/getpeername_unix")` | `<bpf/bpf_helpers.h>` |
| `cgroup/getsockname4` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET4_GETSOCKNAME` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/getsockname4")` | `<bpf/bpf_helpers.h>` |
| `cgroup/getsockname6` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_INET6_GETSOCKNAME` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/getsockname6")` | `<bpf/bpf_helpers.h>` |
| `cgroup/getsockname_unix` | `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `BPF_CGROUP_UNIX_GETSOCKNAME` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/getsockname_unix")` | `<bpf/bpf_helpers.h>` |
| `cgroup/sysctl` | `BPF_PROG_TYPE_CGROUP_SYSCTL` | `BPF_CGROUP_SYSCTL` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/sysctl")` | `<bpf/bpf_helpers.h>` |
| `cgroup/getsockopt` | `BPF_PROG_TYPE_CGROUP_SOCKOPT` | `BPF_CGROUP_GETSOCKOPT` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/getsockopt")` | `<bpf/bpf_helpers.h>` |
| `cgroup/setsockopt` | `BPF_PROG_TYPE_CGROUP_SOCKOPT` | `BPF_CGROUP_SETSOCKOPT` | 挂载标志 `SEC_ATTACHABLE` | `SEC("cgroup/setsockopt")` | `<bpf/bpf_helpers.h>` |
| `cgroup/dev` | `BPF_PROG_TYPE_CGROUP_DEVICE` | `BPF_CGROUP_DEVICE` | 挂载标志 `SEC_ATTACHABLE_OPT` | `SEC("cgroup/dev")` | `<bpf/bpf_helpers.h>` |
| `struct_ops+` | `BPF_PROG_TYPE_STRUCT_OPS` | `0` | 挂载标志 `SEC_NONE` | `SEC("struct_ops/<名称>")` | `<bpf/bpf_helpers.h>` |
| `struct_ops.s+` | `BPF_PROG_TYPE_STRUCT_OPS` | `0` | 挂载标志 `SEC_SLEEPABLE` | `SEC("struct_ops.s/<名称>")` | `<bpf/bpf_helpers.h>` |
| `sk_lookup` | `BPF_PROG_TYPE_SK_LOOKUP` | `BPF_SK_LOOKUP` | 挂载标志 `SEC_ATTACHABLE` | `SEC("sk_lookup")` | `<bpf/bpf_helpers.h>` |
| `netfilter` | `BPF_PROG_TYPE_NETFILTER` | `BPF_NETFILTER` | 挂载标志 `SEC_NONE` | `SEC("netfilter")` | `<bpf/bpf_helpers.h>` |
