# BPF map 类型速查

## 目录

- [头文件约定](#头文件约定)
- Map 类型（表格）
  - [通用键值 map](#通用键值-map)
  - [嵌套 map（map 中存 map）](#嵌套-map中存-map)
  - [程序与调用栈](#程序与调用栈)
  - [事件输出](#事件输出)
  - [网络](#网络)
  - [每对象存储](#每对象存储)
  - [队列、栈与其他](#队列栈与其他)
- 事件与挂载点
  - [概览（事件与挂载点）](#事件与程序挂载点sec-前缀)
  - 详细列表：[tracepoint.md](tracepoint.md)、[probe.md](probe.md)、[perf.md](perf.md)、[other.md](other.md)
- [时间相关辅助函数](#时间相关辅助函数)
- [execsnoop 的程序接口](#execsnoop-的程序接口)
- [版本注意](#版本注意)

本文以表格列出内核 `enum bpf_map_type` 的 map 类型。枚举来源：`third_party/libbpf/include/uapi/linux/bpf.h`（libbpf 子模块，与内核 UAPI 同步）。

## 头文件约定

| 位置 | 头文件 | 说明 |
|---|---|---|
| 内核 UAPI 枚举 | `<linux/bpf.h>` | 枚举与结构体定义，也可用 `third_party/libbpf/include/uapi/linux/bpf.h` |
| BPF 程序端（`.bpf.c`） | `<linux/bpf.h>` 或 `vmlinux.h`，`<bpf/bpf_helpers.h>` | `SEC`、`__uint`、`__type`、辅助函数 |
| 用户态（loader） | `<bpf/libbpf.h>` | skeleton、`ring_buffer__new`、`perf_buffer__new` 等 |
| 用户态（直接系统调用封装） | `<bpf/bpf.h>` | `bpf_map_create`、`bpf_map_lookup_elem` 等 |

BPF 端定义 map 的通用写法：

```c
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} counts SEC(".maps");
```

用户态通过 skeleton 访问，例如 `skel->maps.counts`，或用 `bpf_map__fd()` 取得 fd。

## 通用键值 map

| 类型名 | 用途及注意事项 | 使用及例子 | 头文件 |
|---|---|---|---|
| `BPF_MAP_TYPE_HASH` | 哈希表，键值可为任意大小，最常用。超过 `max_entries` 时插入失败。 | BPF 端：`bpf_map_update_elem(&counts, &k, &v, BPF_ANY);`<br>用户态：`bpf_map_lookup_elem(fd, &k, &v)` | `<linux/bpf.h>`；用户态 `<bpf/bpf.h>` |
| `BPF_MAP_TYPE_ARRAY` | 定长数组，键为下标 `__u32`，元素预分配，查找 O(1)。键必须小于 `max_entries`，元素不能删除。 | `__type(value, struct stat_s);` 后用 `bpf_map_lookup_elem(&arr, &idx)` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_PERCPU_HASH` | 每 CPU 一份哈希表，无需加锁，适合计数器。用户态读到的是 `ncpus` 份值。 | 用户态：按 `libbpf_num_possible_cpus()` 分配缓冲区，一次读出所有 CPU 的值 | `<linux/bpf.h>`；用户态 `<bpf/bpf.h>` |
| `BPF_MAP_TYPE_PERCPU_ARRAY` | 每 CPU 一份数组，常用于每 CPU 统计。键为下标。 | 同 PERCPU_HASH，键为下标 | `<linux/bpf.h>`；用户态 `<bpf/bpf.h>` |
| `BPF_MAP_TYPE_LRU_HASH` | 容量满时自动淘汰最久未使用的元素，插入不会失败。适合缓存。 | `__uint(type, BPF_MAP_TYPE_LRU_HASH);` 其余同 HASH | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_LRU_PERCPU_HASH` | LRU 淘汰加每 CPU 存储。 | 同 LRU_HASH，读取方式同 PERCPU_HASH | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_LPM_TRIE` | 最长前缀匹配，典型用于 IP 路由（CIDR）。键必须以 `struct bpf_lpm_trie_key` 开头（含前缀长度），创建时需 `BPF_F_NO_PREALLOC`。 | `struct { __u32 prefixlen; __u32 ip; } key = {32, addr};` | `<linux/bpf.h>` |

## 嵌套 map（map 中存 map）

| 类型名 | 用途及注意事项 | 使用及例子 | 头文件 |
|---|---|---|---|
| `BPF_MAP_TYPE_ARRAY_OF_MAPS` | 数组，每个元素是一个 map 引用，内层 map 类型固定。用于热替换内层表而不重载程序。 | 用 `__array(values, struct { __uint(type, BPF_MAP_TYPE_HASH); ... })` 声明内层 map，用户态替换下标处的 fd | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_HASH_OF_MAPS` | 哈希表，值是 map 引用，键任意。 | 同 ARRAY_OF_MAPS，键换成任意类型 | `<linux/bpf.h>` |

## 程序与调用栈

| 类型名 | 用途及注意事项 | 使用及例子 | 头文件 |
|---|---|---|---|
| `BPF_MAP_TYPE_PROG_ARRAY` | 存放 BPF 程序引用，用于尾调用。 | BPF 端：`bpf_tail_call(ctx, &progs, idx);`<br>用户态：把程序 fd 写入下标 | `<linux/bpf.h>`；用户态 `<bpf/bpf.h>` |
| `BPF_MAP_TYPE_STACK_TRACE` | 保存调用栈，`bpf_get_stackid()` 返回的 id 用于查表。注意栈深度有上限，超出则返回错误。 | BPF 端：`int id = bpf_get_stackid(ctx, &stacks, 0);`<br>用户态用 id 读出地址列表 | `<linux/bpf.h>` |

## 事件输出

| 类型名 | 用途及注意事项 | 使用及例子 | 头文件 |
|---|---|---|---|
| `BPF_MAP_TYPE_PERF_EVENT_ARRAY` | 每 CPU 一个 perf 环形缓冲，把事件推给用户态。旧方案，新代码优先用 RINGBUF。 | BPF 端：`bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &data, sizeof(data));`<br>用户态：`perf_buffer__new(bpf_map__fd(...), ...)` 轮询 | BPF 端 `<linux/bpf.h>`、`<bpf/bpf_helpers.h>`；用户态 `<bpf/libbpf.h>` |
| `BPF_MAP_TYPE_RINGBUF` | 单个共享环形缓冲，按 CPU 共享，比 PERF_EVENT_ARRAY 省内存且保序。需内核 5.8+。 | BPF 端：`e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);` 填充后 `bpf_ringbuf_submit(e, 0);`<br>用户态：`ring_buffer__new(fd, cb, NULL, NULL)` + `ring_buffer__poll(rb, 100)` | BPF 端 `<linux/bpf.h>`、`<bpf/bpf_helpers.h>`；用户态 `<bpf/libbpf.h>` |
| `BPF_MAP_TYPE_USER_RINGBUF` | 反向环形缓冲：用户态写，BPF 端读。需内核 6.1+。 | 用户态：`user_ring_buffer__reserve()` + `user_ring_buffer__submit()`<br>BPF 端：`bpf_user_ringbuf_drain(&urb, cb, NULL, 0);` | 用户态 `<bpf/libbpf.h>`；BPF 端 `<linux/bpf.h>` |

## 网络

| 类型名 | 用途及注意事项 | 使用及例子 | 头文件 |
|---|---|---|---|
| `BPF_MAP_TYPE_DEVMAP` | 网卡索引映射，XDP 重定向到其他网卡。下标越界会失败。 | BPF 端：`return bpf_redirect_map(&devmap, idx, 0);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_DEVMAP_HASH` | 同 DEVMAP，键为任意值（哈希查找）。 | BPF 端：`bpf_redirect_map(&devmap_hash, key, 0);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_CPUMAP` | 把 XDP 包重定向到指定 CPU 处理。 | BPF 端：`bpf_redirect_map(&cpumap, cpu, 0);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_XSKMAP` | AF_XDP socket 映射，包重定向到用户态 socket。 | BPF 端：`bpf_redirect_map(&xsks, queue_id, 0);`<br>用户态把 xsk fd 写入 | `<linux/bpf.h>`；用户态 `<bpf/xsk.h>` |
| `BPF_MAP_TYPE_SOCKMAP` | 保存 socket 引用，用于 sk_msg / sk_skb 程序转发。 | BPF 端：`bpf_sk_redirect_map(ctx, &sockmap, idx, 0);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_SOCKHASH` | 同 SOCKMAP，键为哈希值（如四元组）。 | BPF 端：`bpf_sk_redirect_hash(ctx, &sockhash, &key, 0);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_REUSEPORT_SOCKARRAY` | 保存 SO_REUSEPORT 的 socket 数组，由 BPF 选择接收 socket。 | BPF 端：`bpf_sk_select_reuseport(ctx, &socks, &key, 0);` | `<linux/bpf.h>` |

## 每对象存储

| 类型名 | 用途及注意事项 | 使用及例子 | 头文件 |
|---|---|---|---|
| `BPF_MAP_TYPE_SK_STORAGE` | 每个 socket 附加一份数据，socket 释放时自动回收。创建时需 `BPF_F_NO_PREALLOC`。 | BPF 端：`bpf_sk_storage_get(&stor, sk, 0, BPF_SK_STORAGE_GET_F_CREATE);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_INODE_STORAGE` | 每个 inode 附加数据。 | BPF 端：`bpf_inode_storage_get(&stor, inode, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_TASK_STORAGE` | 每个 task（线程）附加数据，适合按线程记录状态。 | BPF 端：`bpf_task_storage_get(&stor, task, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_CGRP_STORAGE` | 每个 cgroup 附加数据，替代已弃用的 `CGROUP_STORAGE`。需内核 6.2+。 | BPF 端：`bpf_cgrp_storage_get(&stor, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_CGROUP_STORAGE`（已弃用） | 每 cgroup 附加数据。与 `CGROUP_STORAGE_DEPRECATED` 同值。新代码改用 `CGRP_STORAGE`。 | 仅用于维护旧代码 | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE`（已弃用） | 每 CPU 每 cgroup 附加数据。新代码改用 `CGRP_STORAGE` 配合 per-CPU kptr。 | 仅用于维护旧代码 | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_CGROUP_ARRAY` | 保存 cgroup fd 的数组，用于 `bpf_skb_under_cgroup()` 判断。 | 用户态把 cgroup fd 写入下标 | `<linux/bpf.h>` |

## 队列、栈与其他

| 类型名 | 用途及注意事项 | 使用及例子 | 头文件 |
|---|---|---|---|
| `BPF_MAP_TYPE_QUEUE` | FIFO 队列，没有键，只能 push / pop / peek。 | BPF 端：`bpf_map_push_elem(&q, &v, 0);` `bpf_map_pop_elem(&q, &v);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_STACK` | LIFO 栈，没有键，用法同 QUEUE，但后进先出。 | BPF 端：`bpf_map_push_elem(&s, &v, 0);` `bpf_map_pop_elem(&s, &v);` | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_BLOOM_FILTER` | 布隆过滤器，判断元素"可能存在"，有误判、无漏判。 | BPF 端：`bpf_map_push_elem(&bf, &v, 0);` 添加；`bpf_map_peek_elem(&bf, &v);` 查询 | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_STRUCT_OPS` | 为内核结构体（如 TCP 拥塞控制）提供 BPF 实现的操作表。 | 用户态：`bpf_map__attach_struct_ops(map);` | `<linux/bpf.h>`；用户态 `<bpf/libbpf.h>` |
| `BPF_MAP_TYPE_ARENA` | 大块共享内存区域，BPF 与用户态用指针直接访问。需较新内核（6.9 起）。 | BPF 端：`__arena` 修饰的变量；用户态 `mmap()` 访问 | `<linux/bpf.h>`；用户态 `<bpf/libbpf.h>` |
| `BPF_MAP_TYPE_INSN_ARRAY` | 保存指令偏移的数组，用于指令级跳转表（新增类型）。 | 细节以内核文档为准，一般不直接使用 | `<linux/bpf.h>` |
| `BPF_MAP_TYPE_UNSPEC` | 未指定类型，仅为占位，不能用于创建 map。 | 无 | `<linux/bpf.h>` |

## 事件与程序挂载点（SEC 前缀）

| 挂载类型（SEC 前缀） | 类别（子系统） | 事件名 | 描述 | 例子 | 头文件 | 详细文件 |
|---|---|---|---|---|---|---|
| `tracepoint/ 或 tp/` | 内核静态 tracepoint 的类别，如 `syscalls`、`sched`、`block`（见 [tracepoint.md](tracepoint.md)） | 类别下的事件，如 `sys_enter_execve` | 静态埋点，名称稳定；上下文为 `struct trace_event_raw_<事件名>`，字段随事件变化 | `SEC("tracepoint/syscalls/sys_enter_execve")` | `vmlinux.h`，`<bpf/bpf_helpers.h>` | [tracepoint.md](tracepoint.md) |
| `raw_tracepoint/ 或 raw_tp/` | 无（只写事件名） | tracepoint 事件名，如 `sys_enter` | 原始 tracepoint，参数未展开，用 `ctx->args[i]` 读取 | `SEC("raw_tp/sys_enter")` | `vmlinux.h`，`<bpf/bpf_helpers.h>` | [tracepoint.md](tracepoint.md) |
| `tp_btf/` | 无 | tracepoint 事件名，如 `sched_switch` | 通过 BTF 读取参数，类型安全，需要内核 BTF | `SEC("tp_btf/sched_switch")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` | [other.md](other.md) |
| `kprobe/` | 无（内核函数名） | 内核函数名，如 `do_sys_open` | 函数入口断点，依赖符号名，内核版本间可能变化 | `SEC("kprobe/do_sys_open")` | `<bpf/bpf_tracing.h>`，参数用 `PT_REGS_PARM1(ctx)` | [probe.md](probe.md) |
| `kretprobe/` | 无（内核函数名） | 内核函数名 | 函数返回时触发，可读取返回值 `PT_REGS_RC(ctx)` | `SEC("kretprobe/do_sys_open")` | `<bpf/bpf_tracing.h>` | [probe.md](probe.md) |
| `kprobe.multi/ 与 kretprobe.multi/` | 无（支持通配符） | 内核函数名或通配符 | 一次挂载多个函数，依赖较新的内核和 libbpf | `SEC("kprobe.multi/do_sys_*")` | `<bpf/bpf_tracing.h>` | [probe.md](probe.md) |
| `uprobe/` | 无（用户态二进制路径） | `<路径>:<符号>`，路径为绝对路径 | 用户态函数入口，路径需要绝对路径，符号来自二进制的符号表 | `SEC("uprobe//usr/lib64/libc.so.6:malloc")` | `<bpf/bpf_tracing.h>` | [probe.md](probe.md) |
| `uretprobe/` | 无（用户态二进制路径） | `<路径>:<符号>` | 用户态函数返回时触发 | `SEC("uretprobe//usr/lib64/libc.so.6:malloc")` | `<bpf/bpf_tracing.h>` | [probe.md](probe.md) |
| `usdt/` | 无（提供者名） | `<提供者>:<探针>` | 用户态 USDT 静态探针，二进制需要编译进探针 | `SEC("usdt/libc:setjmp")` | `<bpf/usdt.bpf.h>` | [probe.md](probe.md) |
| `fentry/` | 无（内核函数名，依赖 BTF） | 内核函数名，如 `tcp_v4_connect` | 函数入口，参数直接可读，开销低于 kprobe | `SEC("fentry/tcp_v4_connect")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` | [other.md](other.md) |
| `fexit/` | 无（内核函数名，依赖 BTF） | 内核函数名 | 函数出口，可同时读取参数和返回值 | `SEC("fexit/tcp_v4_connect")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` | [other.md](other.md) |
| `fmod_ret/` | 无（内核函数名，依赖 BTF） | 内核函数名 | 可修改函数返回值（需要内核允许） | `SEC("fmod_ret/bpf_modify_return_test")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` | [other.md](other.md) |
| `lsm/` | 无（LSM 钩子名） | LSM 钩子，如 `file_open` | 安全模块钩子，可返回拒绝，需要启用 BPF LSM | `SEC("lsm/file_open")` | `vmlinux.h`，`<bpf/bpf_tracing.h>` | [other.md](other.md) |
| `ksyscall/` | 无（系统调用名） | 系统调用名，如 `execve` | 系统调用入口，libbpf 自动解析参数 | `SEC("ksyscall/execve")` | `<bpf/bpf_tracing.h>` | [probe.md](probe.md) |
| `kretsyscall/` | 无（系统调用名） | 系统调用名 | 系统调用返回 | `SEC("kretsyscall/execve")` | `<bpf/bpf_tracing.h>` | [probe.md](probe.md) |
| `perf_event` | 无 | 无（由用户态 `perf_event_open()` 决定） | 采样或计数事件，事件类型见第 4 节 | `SEC("perf_event")` | `vmlinux.h`，`<bpf/bpf_helpers.h>` | [perf.md](perf.md) |
| `xdp/ 与 xdp.frags/` | 网络（无类别） | 无 | 网卡驱动层的包处理，最早的包路径 | `SEC("xdp")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `tc/ingress, tc/egress, tcx/ingress, tcx/egress` | 网络（流量控制） | 无 | tc 分类器或动作，挂在网卡入口或出口 | `SEC("tc/ingress")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `cgroup_skb/ingress, cgroup_skb/egress` | 网络（cgroup） | 无 | cgroup 内的包收发过滤 | `SEC("cgroup_skb/egress")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `cgroup/<钩子>` | cgroup（socket、sysctl、设备等） | 钩子名，如 `connect4`、`sysctl`、`dev` | cgroup 层的系统调用钩子 | `SEC("cgroup/connect4")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `sockops` | 网络（socket） | 无 | TCP 状态与选项回调 | `SEC("sockops")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `sk_msg, sk_skb/*` | 网络（socket 重定向） | 无 | socket 消息与流的转发 | `SEC("sk_msg")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `sk_lookup` | 网络（socket 查找） | 无 | 自定义 socket 查找 | `SEC("sk_lookup")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `sk_reuseport` | 网络（SO_REUSEPORT） | 无 | 选择 reuseport 的目标 socket | `SEC("sk_reuseport")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `struct_ops/<结构体>` | 内核结构体的 BPF 实现 | 结构体与成员，如 `tcp_congestion_ops` | 为内核结构体提供回调，如 TCP 拥塞控制 | `SEC("struct_ops/my_cc_init")` | `<bpf/bpf_tracing.h>` | [other.md](other.md) |
| `iter/<类型>` | 内核对象迭代器 | 对象类型，如 `task`、`tcp` | 遍历内核对象并输出到 seq_file | `SEC("iter/task")` | `vmlinux.h`，`<bpf/bpf_helpers.h>` | [other.md](other.md) |
| `syscall` | 无 | 无 | 由用户态通过 `BPF_PROG_TEST_RUN` 调用，不挂到事件上 | `SEC("syscall")` | `<bpf/bpf_helpers.h>` | [other.md](other.md) |

各挂载类型的详细列表放在以下文件中：

| 文件 | 包含的挂载类型 | 内容 |
|---|---|---|
| [tracepoint.md](tracepoint.md) | `tracepoint` / `tp`、`raw_tracepoint` / `raw_tp` | 节点上全部 tracepoint（类别、事件名、ID、字段）、代码写法（内置事件与模块事件）、raw_tracepoint 按类别的事件 |
| [probe.md](probe.md) | `kprobe`、`kretprobe`、`ksyscall`、`kretsyscall`、`kprobe.multi`、`uprobe`、`uretprobe`、`usdt` | 系统调用入口函数全表、按来源（内核本体/各模块）统计的可挂载函数、uprobe/usdt 写法 |
| [perf.md](perf.md) | `perf_event` | 通用硬件事件、软件事件、节点上的 PMU 事件 |
| [other.md](other.md) | `fentry`、`fexit`、`fmod_ret`、`fsession`、`freplace`、`lsm`、`lsm_cgroup`、`tp_btf`、`iter`、`struct_ops`、`xdp`、`tc`/`tcx`、`cgroup_skb`、`cgroup/*`、`sockops`、`sk_msg`、`sk_skb`、`sk_lookup`、`sk_reuseport`、`syscall` 等 | 依赖 BTF 的类型（本节点不可用）、网络与 cgroup 等固定挂载点、libbpf `section_defs` 全表（110 条） |

Map 类型、时间相关辅助函数和版本注意都在本文件中。

## 时间相关辅助函数

| 函数 | 时间含义 | 使用及例子 | 头文件 / 版本 |
|---|---|---|---|
| `bpf_ktime_get_ns()` | 单调时钟，自开机起计算，**不含休眠时间** | `__u64 t = bpf_ktime_get_ns();` | `<bpf/bpf_helpers.h>`，早期内核即可用 |
| `bpf_ktime_get_boot_ns()` | 开机起的时间，**包含休眠时间** | `__u64 t = bpf_ktime_get_boot_ns();` | 5.7+ |
| `bpf_ktime_get_coarse_ns()` | 粗粒度时钟，开销小、精度低（约 jiffies 级），适合不需要精确时间的场景 | `__u64 t = bpf_ktime_get_coarse_ns();` | 5.11+ |
| `bpf_ktime_get_tai_ns()` | TAI 时间，不受 NTP 跳秒影响 | `__u64 t = bpf_ktime_get_tai_ns();` | 6.1+ |

时间的注意事项：

- 计算耗时用 `bpf_ktime_get_ns()`，两次取值相减即可。
- 需要跨重启或与日志时间对齐时，在用户态换算为墙上时间，BPF 端只记录单调时间。
- 具体版本以 `include/bpf/bpf_helper_defs.h` 中每个辅助函数的注释为准。

## execsnoop 的程序接口：`SEC("tracepoint/syscalls/sys_enter_execve")`

`src/execsnoop.bpf.c` 中的定义：

```c
SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(struct trace_event_raw_sys_enter *ctx)
```

| 项目 | 说明 |
|---|---|
| 挂载点 | `syscalls` 类别下 `sys_enter_execve` tracepoint，即进入 execve 系统调用时触发 |
| 上下文 | `struct trace_event_raw_sys_enter`（在 `vmlinux.h` 中），字段包括 `id`（系统调用号）和 `args[6]`（参数） |
| 参数映射 | `args[0]` 是 `filename`（用户态指针），`args[1]` 是 `argv`，`args[2]` 是 `envp` |
| 读取用户态内存 | 必须用 `bpf_probe_read_user_str()`，直接解引用会失败 |
| 返回值 | 一般返回 0，tracepoint 程序的返回值会被忽略 |
| 加载与挂载 | 用户态调用 `execsnoop_bpf__attach(skel)`，由 libbpf 按 SEC 名自动挂载 |

本项目用到的辅助函数：

| 函数 | 作用 |
|---|---|
| `bpf_get_current_task()` | 当前进程的 `task_struct` 指针，用于读取父进程 |
| `bpf_get_current_pid_tgid()` | 返回 `tgid << 32 \| pid`，高 32 位为进程 PID |
| `bpf_get_current_comm()` | 当前进程名 |
| `bpf_probe_read_user_str()` | 从用户态复制字符串到 BPF 空间，返回实际长度 |
| `bpf_map_lookup_elem()` | 取每 CPU 暂存区，避免大结构放在 BPF 栈上（栈上限 512 字节） |
| `bpf_perf_event_output()` | 把事件写入 `events` 的 PERF_EVENT_ARRAY |
| `BPF_CORE_READ()` | 读取父进程的 `real_parent->tgid`，由 CO-RE 处理字段偏移 |

用户态事件结构 `struct event` 定义在 `src/execsnoop.h`，包含 `pid`、`ppid`、`comm`、`filename`。

## 版本注意

- `RINGBUF` 需要内核 5.8+，`USER_RINGBUF` 需要 6.1+，`CGRP_STORAGE` 需要 6.2+，本项目要求 5.8+（见 `docs/kernel-compatibility.md`）。
- 其余类型的最低内核版本未逐一核实，需要时以内核源码或 `Documentation/bpf/` 为准。
- 不确定某类型能否在目标内核创建时，可用 `bpf_map_create()` 试探，返回 `-EINVAL` 或 `-EPERM` 时检查内核版本和权限。
