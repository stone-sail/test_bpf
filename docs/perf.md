# perf_event

每个表按"挂载类型、类别（子系统）、事件名、描述、例子、头文件"整理。总览见 [bpf_known.md](bpf_known.md)。数据来自测试节点（内核 4.18.0-193.el8.jd_001.x86_64）和仓库内 libbpf，只代表该内核。

## 两种使用 perf 事件的方式

perf 事件有两种用法：一种是直接读取内核 perf 写出的样本（下称"纯 perf"），另一种是把 BPF 程序挂到 perf 事件上（下称"BPF + perf"）。两者都用 `perf_event_open()` 创建事件，**区别在于事件触发之后由谁采集数据**。

### 数据流

纯 perf：

```
事件触发（计数溢出 / tracepoint 命中 / kprobe 命中）
  → 内核 perf 子系统按 attr.sample_type 采集，写成固定格式的 PERF_RECORD_SAMPLE
  → 写入该 perf fd 的 mmap ring buffer
  → 用户态按 data_head / data_tail 读取并解析，再做过滤
```

BPF + perf：

```
事件触发
  → 内核运行挂在该 perf fd 上的 BPF 程序（SEC("perf_event") / SEC("tracepoint") / SEC("kprobe")）
  → 程序在内核里过滤，并自行采集数据（bpf_get_stack、复制用户栈、读内核状态等）
  → 写入 BPF ringbuf 或 PERF_EVENT_ARRAY，按程序自定义的结构
  → 用户态通过 libbpf（ring_buffer__poll / perf_buffer__poll）读取
```

### 对比

| | 纯 perf | BPF + perf |
|---|---|---|
| perf 事件的作用 | 触发，并由内核按固定格式采样 | 只触发，样本由 BPF 程序采集 |
| 谁采数据 | 内核 perf 子系统 | 自己写的 BPF 程序（在内核里运行） |
| 是否加载内核代码 | 否 | 是，需要加载 BPF 程序并通过 verifier |
| `attr.sample_type` | 必须设置，决定样本里有哪些字段 | 不需要；样本由 BPF 程序决定 |
| fd 的 mmap | 需要，用来读样本 | 不需要；用 `bpf_program__attach_perf_event()` 等挂载 |
| 能采的内容 | 只能选 `PERF_SAMPLE_*` 提供的字段 | 可自定义，能读内核内部状态（`bpf_probe_read_kernel` 等） |
| 数据格式 | 内核规定，按固定顺序排列 | 程序自定义的结构体 |
| 过滤 pid / comm | 用户态过滤，内核仍会采集、复制所有样本 | 内核里过滤，不匹配的事件几乎没有开销 |
| 缩小范围 | 按 cgroup 采样（`PERF_FLAG_PID_CGROUP`） | BPF 程序内按任意条件判断 |
| pid 的 namespace | 打开事件的进程所在的 namespace，容器内按 pid 过滤正确 | `bpf_get_current_pid_tgid()` 返回宿主机（init）namespace 的 pid |
| 用户态读取 | 自己解析 ring buffer，自己处理绕回和 `data_tail` 写回 | libbpf 处理 |
| 内核要求 | 只需要 perf_events（`CONFIG_PERF_EVENTS`），老内核也能用 | 需要对应的 BPF 程序类型、辅助函数和 map 类型支持 |

### 纯 perf 的 `sample_type`

`sample_type` 是 `struct perf_event_attr` 中的位掩码，决定每个 `PERF_RECORD_SAMPLE` 包含哪些字段。样本里没有字段名，字段按内核规定的固定顺序排列，只包含设置了的字段，解析时必须按同样的顺序读取（完整顺序见 `man 2 perf_event_open`）。

| 标志 | 样本中的字段 | 用途 |
|---|---|---|
| `PERF_SAMPLE_TID` | `u32 pid, tid` | 按进程过滤、取进程名 |
| `PERF_SAMPLE_TIME` | `u64 time` | 时间戳 |
| `PERF_SAMPLE_CPU` | `u32 cpu, res` | 所在 CPU |
| `PERF_SAMPLE_PERIOD` | `u64 period` | 按周期采样时作为样本权重 |
| `PERF_SAMPLE_CALLCHAIN` | `u64 nr, ips[nr]` | 内核调用链和帧指针用户调用链，中间以标记值分隔 |
| `PERF_SAMPLE_REGS_USER` | `u64 abi, regs[...]` | 用户态寄存器，作为 DWARF 展开起点；配合 `attr.sample_regs_user` |
| `PERF_SAMPLE_STACK_USER` | `u64 size, data[size], u64 dyn_size` | 用户栈原始内存；配合 `attr.sample_stack_user`（上限略小于 64 KB） |
| `PERF_SAMPLE_RAW` | `u32 size, data[size]` | tracepoint 的原始字段 |

只要内核栈时，可以只用 TID、TIME、CPU、CALLCHAIN，并设置 `exclude_callchain_user = 1`、`sample_regs_user = 0`、`sample_stack_user = 0`，样本会从几十 KB 降到几百字节。

### tracepoint 和 kprobe 也是同样的模式

| 触发点 | 纯 perf | BPF + perf |
|---|---|---|
| 定时采样 | `PERF_TYPE_SOFTWARE` / `PERF_COUNT_SW_CPU_CLOCK`，按频率或周期 | 同一个事件，`bpf_program__attach_perf_event()` 挂 `SEC("perf_event")` 程序 |
| tracepoint | `PERF_TYPE_TRACEPOINT`，`config` 为 `/sys/kernel/tracing/events/<类别>/<事件>/id`，`sample_period = 1` | `bpf_program__attach_tracepoint()` 挂 `SEC("tracepoint")` 程序；libbpf 底层同样打开 tracepoint perf 事件再绑定程序 |
| kprobe | kprobe PMU（4.17 起）：`type` 读自 `/sys/bus/event_source/devices/kprobe/type`，函数名放在 `config1` | `bpf_program__attach_kprobe()` 挂 `SEC("kprobe")` 程序 |

tracepoint 和 kprobe 每次命中都会触发，频率远高于定时采样，两种方式的差距也更大：
- 纯 perf 下，不需要的事件内核仍会采集并复制栈，再由用户态丢弃，开销大，应当用 cgroup 缩小范围。
- BPF + perf 下，在程序开头判断条件后直接返回，几乎没有开销。

### 怎么选

- 内核支持 BPF 时优先用 BPF + perf：能在源头过滤，能采集自定义数据，开销更低。
- 内核太老、BPF 被禁用或没有加载 BPF 的权限时用纯 perf：只依赖 perf_events，兼容性最好。
- 两者都受 `perf_event_paranoid` 限制，全机采样通常需要 root 或 `CAP_PERFMON`（5.8 起）。

## perf_event 事件列表

- 写法：BPF 端固定为 `SEC("perf_event")`。具体事件由用户态 `perf_event_open()` 的 `attr.type` 和 `attr.config` 决定，再把 BPF 程序挂到该 perf 事件上（libbpf 的 `bpf_program__attach_perf_event()`）。
- 上下文：`struct bpf_perf_event_data *ctx`。
- 下表"类别"为 PMU（`attr.type`），"事件名"为 `attr.config`。

### 通用硬件与软件事件（来自 `linux/perf_event.h`）

| 挂载类型 | 类别（子系统） | 事件名 | 描述 | 例子 | 头文件 |
|---|---|---|---|---|---|
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_CPU_CYCLES`（0） | CPU 周期数 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_CPU_CYCLES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_INSTRUCTIONS`（1） | 执行完成的指令数 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_INSTRUCTIONS;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_CACHE_REFERENCES`（2） | 缓存访问次数 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_CACHE_REFERENCES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_CACHE_MISSES`（3） | 缓存未命中次数 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_CACHE_MISSES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_BRANCH_INSTRUCTIONS`（4） | 分支指令数 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_BRANCH_MISSES`（5） | 分支预测失败次数 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_BRANCH_MISSES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_BUS_CYCLES`（6） | 总线周期数 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_BUS_CYCLES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_STALLED_CYCLES_FRONTEND`（7） | 前端停顿周期 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_STALLED_CYCLES_FRONTEND;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_STALLED_CYCLES_BACKEND`（8） | 后端停顿周期 | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_STALLED_CYCLES_BACKEND;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_HARDWARE`（0） | `PERF_COUNT_HW_REF_CPU_CYCLES`（9） | 参考周期数（不受调频影响） | `attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_REF_CPU_CYCLES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_CPU_CLOCK`（0） | CPU 时钟（高精度定时器），常用于定时采样 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_CPU_CLOCK;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_TASK_CLOCK`（1） | 任务运行时钟 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_TASK_CLOCK;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_PAGE_FAULTS`（2） | 缺页次数 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_PAGE_FAULTS;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_CONTEXT_SWITCHES`（3） | 上下文切换次数 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_CONTEXT_SWITCHES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_CPU_MIGRATIONS`（4） | 进程在 CPU 间迁移次数 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_CPU_MIGRATIONS;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_PAGE_FAULTS_MIN`（5） | 次缺页（不需磁盘 I/O） | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_PAGE_FAULTS_MIN;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_PAGE_FAULTS_MAJ`（6） | 主缺页（需要磁盘 I/O） | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_PAGE_FAULTS_MAJ;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_ALIGNMENT_FAULTS`（7） | 对齐异常次数 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_ALIGNMENT_FAULTS;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_EMULATION_FAULTS`（8） | 指令模拟异常次数 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_EMULATION_FAULTS;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_DUMMY`（9） | 占位事件，不计数 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_DUMMY;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_BPF_OUTPUT`（10） | BPF 输出事件，配合 bpf_perf_event_output 使用 | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_BPF_OUTPUT;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | `PERF_TYPE_SOFTWARE`（1） | `PERF_COUNT_SW_CGROUP_SWITCHES`（11） | cgroup 切换次数（较新内核才有，4.18 可能不支持） | `attr.type = PERF_TYPE_SOFTWARE; attr.config = PERF_COUNT_SW_CGROUP_SWITCHES;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |

### 测试节点上的 PMU 事件（`/sys/bus/event_source/devices/*/events`）

- PMU 的 `type` 值因机器而异，需要从 `/sys/bus/event_source/devices/<PMU>/type` 读取；`config` 需要按 `/sys/bus/event_source/devices/<PMU>/format/` 中的位定义把编码（如 `event=0xc4`）换算成数值。
- 节点上的 PMU：`breakpoint`(type=5)、`cpu`(type=4)、`cstate_core`(type=73)、`cstate_pkg`(type=74)、`intel_bts`(type=8)、`intel_pt`(type=9)、`kprobe`(type=6)、`msr`(type=10)、`power`(type=11)、`software`(type=1)、`tracepoint`(type=2)、`uncore_cha_0`(type=30)、`uncore_cha_1`(type=31)、`uncore_cha_10`(type=40)、`uncore_cha_11`(type=41)、`uncore_cha_12`(type=42)、`uncore_cha_13`(type=43)、`uncore_cha_14`(type=44)、`uncore_cha_15`(type=45)、`uncore_cha_16`(type=46)、`uncore_cha_17`(type=47)、`uncore_cha_18`(type=48)、`uncore_cha_19`(type=49)、`uncore_cha_2`(type=32)、`uncore_cha_20`(type=50)、`uncore_cha_21`(type=51)、`uncore_cha_22`(type=52)、`uncore_cha_23`(type=53)、`uncore_cha_3`(type=33)、`uncore_cha_4`(type=34)、`uncore_cha_5`(type=35)、`uncore_cha_6`(type=36)、`uncore_cha_7`(type=37)、`uncore_cha_8`(type=38)、`uncore_cha_9`(type=39)、`uncore_iio_0`(type=54)、`uncore_iio_1`(type=55)、`uncore_iio_2`(type=56)、`uncore_iio_3`(type=57)、`uncore_iio_4`(type=58)、`uncore_iio_5`(type=59)、`uncore_iio_free_running_0`(type=60)、`uncore_iio_free_running_1`(type=61)、`uncore_iio_free_running_2`(type=62)、`uncore_iio_free_running_3`(type=63)、`uncore_iio_free_running_4`(type=64)、`uncore_iio_free_running_5`(type=65)、`uncore_imc_0`(type=14)、`uncore_imc_1`(type=15)、`uncore_imc_2`(type=16)、`uncore_imc_3`(type=17)、`uncore_imc_4`(type=18)、`uncore_imc_5`(type=19)、`uncore_irp_0`(type=66)、`uncore_irp_1`(type=67)、`uncore_irp_2`(type=68)、`uncore_irp_3`(type=69)、`uncore_irp_4`(type=70)、`uncore_irp_5`(type=71)、`uncore_m2m_0`(type=12)、`uncore_m2m_1`(type=13)、`uncore_m2pcie_0`(type=26)、`uncore_m2pcie_1`(type=27)、`uncore_m2pcie_2`(type=28)、`uncore_m3upi_0`(type=23)、`uncore_m3upi_1`(type=24)、`uncore_m3upi_2`(type=25)、`uncore_pcu`(type=72)、`uncore_ubox`(type=29)、`uncore_upi_0`(type=20)、`uncore_upi_1`(type=21)、`uncore_upi_2`(type=22)、`uprobe`(type=7)

| 挂载类型 | 类别（子系统） | 事件名 | 描述 | 例子 | 头文件 |
|---|---|---|---|---|---|
| `perf_event` | PMU `cpu`（type=4） | `branch-instructions` | 编码 `event=0xc4` | `attr.type = 4; attr.config = <按 format 换算 event=0xc4>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `branch-misses` | 编码 `event=0xc5` | `attr.type = 4; attr.config = <按 format 换算 event=0xc5>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `bus-cycles` | 编码 `event=0x3c,umask=0x01` | `attr.type = 4; attr.config = <按 format 换算 event=0x3c,umask=0x01>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `cache-misses` | 编码 `event=0x2e,umask=0x41` | `attr.type = 4; attr.config = <按 format 换算 event=0x2e,umask=0x41>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `cache-references` | 编码 `event=0x2e,umask=0x4f` | `attr.type = 4; attr.config = <按 format 换算 event=0x2e,umask=0x4f>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `cpu-cycles` | 编码 `event=0x3c` | `attr.type = 4; attr.config = <按 format 换算 event=0x3c>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `cycles-ct` | 编码 `event=0x3c,in_tx=1,in_tx_cp=1` | `attr.type = 4; attr.config = <按 format 换算 event=0x3c,in_tx=1,in_tx_cp=1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `cycles-t` | 编码 `event=0x3c,in_tx=1` | `attr.type = 4; attr.config = <按 format 换算 event=0x3c,in_tx=1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `el-abort` | 编码 `event=0xc8,umask=0x4` | `attr.type = 4; attr.config = <按 format 换算 event=0xc8,umask=0x4>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `el-capacity` | 编码 `event=0x54,umask=0x2` | `attr.type = 4; attr.config = <按 format 换算 event=0x54,umask=0x2>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `el-commit` | 编码 `event=0xc8,umask=0x2` | `attr.type = 4; attr.config = <按 format 换算 event=0xc8,umask=0x2>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `el-conflict` | 编码 `event=0x54,umask=0x1` | `attr.type = 4; attr.config = <按 format 换算 event=0x54,umask=0x1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `el-start` | 编码 `event=0xc8,umask=0x1` | `attr.type = 4; attr.config = <按 format 换算 event=0xc8,umask=0x1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `instructions` | 编码 `event=0xc0` | `attr.type = 4; attr.config = <按 format 换算 event=0xc0>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `mem-loads` | 编码 `event=0xcd,umask=0x1,ldlat=3` | `attr.type = 4; attr.config = <按 format 换算 event=0xcd,umask=0x1,ldlat=3>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `mem-stores` | 编码 `event=0xd0,umask=0x82` | `attr.type = 4; attr.config = <按 format 换算 event=0xd0,umask=0x82>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `ref-cycles` | 编码 `event=0x00,umask=0x03` | `attr.type = 4; attr.config = <按 format 换算 event=0x00,umask=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `topdown-fetch-bubbles` | 编码 `event=0x9c,umask=0x1` | `attr.type = 4; attr.config = <按 format 换算 event=0x9c,umask=0x1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `topdown-recovery-bubbles` | 编码 `event=0xd,umask=0x1,cmask=1,any=1` | `attr.type = 4; attr.config = <按 format 换算 event=0xd,umask=0x1,cmask=1,any=1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `topdown-slots-issued` | 编码 `event=0xe,umask=0x1` | `attr.type = 4; attr.config = <按 format 换算 event=0xe,umask=0x1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `topdown-slots-retired` | 编码 `event=0xc2,umask=0x2` | `attr.type = 4; attr.config = <按 format 换算 event=0xc2,umask=0x2>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `topdown-total-slots` | 编码 `event=0x3c,umask=0x0,any=1` | `attr.type = 4; attr.config = <按 format 换算 event=0x3c,umask=0x0,any=1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `tx-abort` | 编码 `event=0xc9,umask=0x4` | `attr.type = 4; attr.config = <按 format 换算 event=0xc9,umask=0x4>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `tx-capacity` | 编码 `event=0x54,umask=0x2` | `attr.type = 4; attr.config = <按 format 换算 event=0x54,umask=0x2>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `tx-commit` | 编码 `event=0xc9,umask=0x2` | `attr.type = 4; attr.config = <按 format 换算 event=0xc9,umask=0x2>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `tx-conflict` | 编码 `event=0x54,umask=0x1` | `attr.type = 4; attr.config = <按 format 换算 event=0x54,umask=0x1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cpu`（type=4） | `tx-start` | 编码 `event=0xc9,umask=0x1` | `attr.type = 4; attr.config = <按 format 换算 event=0xc9,umask=0x1>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cstate_core`（type=73） | `c3-residency` | 编码 `event=0x01` | `attr.type = 73; attr.config = <按 format 换算 event=0x01>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cstate_core`（type=73） | `c6-residency` | 编码 `event=0x02` | `attr.type = 73; attr.config = <按 format 换算 event=0x02>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cstate_core`（type=73） | `c7-residency` | 编码 `event=0x03` | `attr.type = 73; attr.config = <按 format 换算 event=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cstate_pkg`（type=74） | `c2-residency` | 编码 `event=0x00` | `attr.type = 74; attr.config = <按 format 换算 event=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cstate_pkg`（type=74） | `c3-residency` | 编码 `event=0x01` | `attr.type = 74; attr.config = <按 format 换算 event=0x01>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cstate_pkg`（type=74） | `c6-residency` | 编码 `event=0x02` | `attr.type = 74; attr.config = <按 format 换算 event=0x02>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `cstate_pkg`（type=74） | `c7-residency` | 编码 `event=0x03` | `attr.type = 74; attr.config = <按 format 换算 event=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `msr`（type=10） | `aperf` | 编码 `event=0x01` | `attr.type = 10; attr.config = <按 format 换算 event=0x01>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `msr`（type=10） | `cpu_thermal_margin` | 编码 `event=0x07` | `attr.type = 10; attr.config = <按 format 换算 event=0x07>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `msr`（type=10） | `mperf` | 编码 `event=0x02` | `attr.type = 10; attr.config = <按 format 换算 event=0x02>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `msr`（type=10） | `pperf` | 编码 `event=0x03` | `attr.type = 10; attr.config = <按 format 换算 event=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `msr`（type=10） | `smi` | 编码 `event=0x04` | `attr.type = 10; attr.config = <按 format 换算 event=0x04>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `msr`（type=10） | `tsc` | 编码 `event=0x00` | `attr.type = 10; attr.config = <按 format 换算 event=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `power`（type=11） | `energy-pkg` | 编码 `event=0x02` | `attr.type = 11; attr.config = <按 format 换算 event=0x02>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `power`（type=11） | `energy-ram` | 编码 `event=0x03` | `attr.type = 11; attr.config = <按 format 换算 event=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_in_port0` | 编码 `event=0xff,umask=0x20` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x20>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_in_port1` | 编码 `event=0xff,umask=0x21` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x21>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_in_port2` | 编码 `event=0xff,umask=0x22` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x22>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_in_port3` | 编码 `event=0xff,umask=0x23` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x23>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_out_port0` | 编码 `event=0xff,umask=0x24` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x24>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_out_port1` | 编码 `event=0xff,umask=0x25` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x25>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_out_port2` | 编码 `event=0xff,umask=0x26` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x26>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `bw_out_port3` | 编码 `event=0xff,umask=0x27` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x27>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `ioclk` | 编码 `event=0xff,umask=0x10` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x10>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_in_port0` | 编码 `event=0xff,umask=0x30` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x30>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_in_port1` | 编码 `event=0xff,umask=0x32` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x32>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_in_port2` | 编码 `event=0xff,umask=0x34` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x34>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_in_port3` | 编码 `event=0xff,umask=0x36` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x36>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_out_port0` | 编码 `event=0xff,umask=0x31` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x31>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_out_port1` | 编码 `event=0xff,umask=0x33` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x33>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_out_port2` | 编码 `event=0xff,umask=0x35` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x35>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_0`（type=60） | `util_out_port3` | 编码 `event=0xff,umask=0x37` | `attr.type = 60; attr.config = <按 format 换算 event=0xff,umask=0x37>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_in_port0` | 编码 `event=0xff,umask=0x20` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x20>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_in_port1` | 编码 `event=0xff,umask=0x21` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x21>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_in_port2` | 编码 `event=0xff,umask=0x22` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x22>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_in_port3` | 编码 `event=0xff,umask=0x23` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x23>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_out_port0` | 编码 `event=0xff,umask=0x24` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x24>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_out_port1` | 编码 `event=0xff,umask=0x25` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x25>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_out_port2` | 编码 `event=0xff,umask=0x26` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x26>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `bw_out_port3` | 编码 `event=0xff,umask=0x27` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x27>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `ioclk` | 编码 `event=0xff,umask=0x10` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x10>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_in_port0` | 编码 `event=0xff,umask=0x30` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x30>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_in_port1` | 编码 `event=0xff,umask=0x32` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x32>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_in_port2` | 编码 `event=0xff,umask=0x34` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x34>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_in_port3` | 编码 `event=0xff,umask=0x36` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x36>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_out_port0` | 编码 `event=0xff,umask=0x31` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x31>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_out_port1` | 编码 `event=0xff,umask=0x33` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x33>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_out_port2` | 编码 `event=0xff,umask=0x35` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x35>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_1`（type=61） | `util_out_port3` | 编码 `event=0xff,umask=0x37` | `attr.type = 61; attr.config = <按 format 换算 event=0xff,umask=0x37>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_in_port0` | 编码 `event=0xff,umask=0x20` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x20>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_in_port1` | 编码 `event=0xff,umask=0x21` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x21>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_in_port2` | 编码 `event=0xff,umask=0x22` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x22>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_in_port3` | 编码 `event=0xff,umask=0x23` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x23>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_out_port0` | 编码 `event=0xff,umask=0x24` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x24>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_out_port1` | 编码 `event=0xff,umask=0x25` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x25>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_out_port2` | 编码 `event=0xff,umask=0x26` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x26>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `bw_out_port3` | 编码 `event=0xff,umask=0x27` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x27>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `ioclk` | 编码 `event=0xff,umask=0x10` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x10>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_in_port0` | 编码 `event=0xff,umask=0x30` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x30>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_in_port1` | 编码 `event=0xff,umask=0x32` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x32>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_in_port2` | 编码 `event=0xff,umask=0x34` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x34>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_in_port3` | 编码 `event=0xff,umask=0x36` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x36>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_out_port0` | 编码 `event=0xff,umask=0x31` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x31>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_out_port1` | 编码 `event=0xff,umask=0x33` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x33>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_out_port2` | 编码 `event=0xff,umask=0x35` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x35>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_2`（type=62） | `util_out_port3` | 编码 `event=0xff,umask=0x37` | `attr.type = 62; attr.config = <按 format 换算 event=0xff,umask=0x37>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_in_port0` | 编码 `event=0xff,umask=0x20` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x20>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_in_port1` | 编码 `event=0xff,umask=0x21` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x21>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_in_port2` | 编码 `event=0xff,umask=0x22` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x22>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_in_port3` | 编码 `event=0xff,umask=0x23` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x23>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_out_port0` | 编码 `event=0xff,umask=0x24` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x24>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_out_port1` | 编码 `event=0xff,umask=0x25` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x25>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_out_port2` | 编码 `event=0xff,umask=0x26` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x26>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `bw_out_port3` | 编码 `event=0xff,umask=0x27` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x27>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `ioclk` | 编码 `event=0xff,umask=0x10` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x10>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_in_port0` | 编码 `event=0xff,umask=0x30` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x30>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_in_port1` | 编码 `event=0xff,umask=0x32` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x32>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_in_port2` | 编码 `event=0xff,umask=0x34` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x34>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_in_port3` | 编码 `event=0xff,umask=0x36` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x36>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_out_port0` | 编码 `event=0xff,umask=0x31` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x31>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_out_port1` | 编码 `event=0xff,umask=0x33` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x33>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_out_port2` | 编码 `event=0xff,umask=0x35` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x35>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_3`（type=63） | `util_out_port3` | 编码 `event=0xff,umask=0x37` | `attr.type = 63; attr.config = <按 format 换算 event=0xff,umask=0x37>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_in_port0` | 编码 `event=0xff,umask=0x20` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x20>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_in_port1` | 编码 `event=0xff,umask=0x21` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x21>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_in_port2` | 编码 `event=0xff,umask=0x22` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x22>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_in_port3` | 编码 `event=0xff,umask=0x23` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x23>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_out_port0` | 编码 `event=0xff,umask=0x24` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x24>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_out_port1` | 编码 `event=0xff,umask=0x25` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x25>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_out_port2` | 编码 `event=0xff,umask=0x26` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x26>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `bw_out_port3` | 编码 `event=0xff,umask=0x27` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x27>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `ioclk` | 编码 `event=0xff,umask=0x10` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x10>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_in_port0` | 编码 `event=0xff,umask=0x30` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x30>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_in_port1` | 编码 `event=0xff,umask=0x32` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x32>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_in_port2` | 编码 `event=0xff,umask=0x34` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x34>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_in_port3` | 编码 `event=0xff,umask=0x36` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x36>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_out_port0` | 编码 `event=0xff,umask=0x31` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x31>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_out_port1` | 编码 `event=0xff,umask=0x33` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x33>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_out_port2` | 编码 `event=0xff,umask=0x35` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x35>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_4`（type=64） | `util_out_port3` | 编码 `event=0xff,umask=0x37` | `attr.type = 64; attr.config = <按 format 换算 event=0xff,umask=0x37>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_in_port0` | 编码 `event=0xff,umask=0x20` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x20>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_in_port1` | 编码 `event=0xff,umask=0x21` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x21>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_in_port2` | 编码 `event=0xff,umask=0x22` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x22>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_in_port3` | 编码 `event=0xff,umask=0x23` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x23>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_out_port0` | 编码 `event=0xff,umask=0x24` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x24>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_out_port1` | 编码 `event=0xff,umask=0x25` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x25>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_out_port2` | 编码 `event=0xff,umask=0x26` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x26>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `bw_out_port3` | 编码 `event=0xff,umask=0x27` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x27>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `ioclk` | 编码 `event=0xff,umask=0x10` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x10>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_in_port0` | 编码 `event=0xff,umask=0x30` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x30>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_in_port1` | 编码 `event=0xff,umask=0x32` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x32>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_in_port2` | 编码 `event=0xff,umask=0x34` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x34>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_in_port3` | 编码 `event=0xff,umask=0x36` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x36>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_out_port0` | 编码 `event=0xff,umask=0x31` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x31>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_out_port1` | 编码 `event=0xff,umask=0x33` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x33>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_out_port2` | 编码 `event=0xff,umask=0x35` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x35>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_iio_free_running_5`（type=65） | `util_out_port3` | 编码 `event=0xff,umask=0x37` | `attr.type = 65; attr.config = <按 format 换算 event=0xff,umask=0x37>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_0`（type=14） | `cas_count_read` | 编码 `event=0x04,umask=0x03` | `attr.type = 14; attr.config = <按 format 换算 event=0x04,umask=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_0`（type=14） | `cas_count_write` | 编码 `event=0x04,umask=0x0c` | `attr.type = 14; attr.config = <按 format 换算 event=0x04,umask=0x0c>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_0`（type=14） | `clockticks` | 编码 `event=0x00,umask=0x00` | `attr.type = 14; attr.config = <按 format 换算 event=0x00,umask=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_1`（type=15） | `cas_count_read` | 编码 `event=0x04,umask=0x03` | `attr.type = 15; attr.config = <按 format 换算 event=0x04,umask=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_1`（type=15） | `cas_count_write` | 编码 `event=0x04,umask=0x0c` | `attr.type = 15; attr.config = <按 format 换算 event=0x04,umask=0x0c>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_1`（type=15） | `clockticks` | 编码 `event=0x00,umask=0x00` | `attr.type = 15; attr.config = <按 format 换算 event=0x00,umask=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_2`（type=16） | `cas_count_read` | 编码 `event=0x04,umask=0x03` | `attr.type = 16; attr.config = <按 format 换算 event=0x04,umask=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_2`（type=16） | `cas_count_write` | 编码 `event=0x04,umask=0x0c` | `attr.type = 16; attr.config = <按 format 换算 event=0x04,umask=0x0c>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_2`（type=16） | `clockticks` | 编码 `event=0x00,umask=0x00` | `attr.type = 16; attr.config = <按 format 换算 event=0x00,umask=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_3`（type=17） | `cas_count_read` | 编码 `event=0x04,umask=0x03` | `attr.type = 17; attr.config = <按 format 换算 event=0x04,umask=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_3`（type=17） | `cas_count_write` | 编码 `event=0x04,umask=0x0c` | `attr.type = 17; attr.config = <按 format 换算 event=0x04,umask=0x0c>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_3`（type=17） | `clockticks` | 编码 `event=0x00,umask=0x00` | `attr.type = 17; attr.config = <按 format 换算 event=0x00,umask=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_4`（type=18） | `cas_count_read` | 编码 `event=0x04,umask=0x03` | `attr.type = 18; attr.config = <按 format 换算 event=0x04,umask=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_4`（type=18） | `cas_count_write` | 编码 `event=0x04,umask=0x0c` | `attr.type = 18; attr.config = <按 format 换算 event=0x04,umask=0x0c>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_4`（type=18） | `clockticks` | 编码 `event=0x00,umask=0x00` | `attr.type = 18; attr.config = <按 format 换算 event=0x00,umask=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_5`（type=19） | `cas_count_read` | 编码 `event=0x04,umask=0x03` | `attr.type = 19; attr.config = <按 format 换算 event=0x04,umask=0x03>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_5`（type=19） | `cas_count_write` | 编码 `event=0x04,umask=0x0c` | `attr.type = 19; attr.config = <按 format 换算 event=0x04,umask=0x0c>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
| `perf_event` | PMU `uncore_imc_5`（type=19） | `clockticks` | 编码 `event=0x00,umask=0x00` | `attr.type = 19; attr.config = <按 format 换算 event=0x00,umask=0x00>;` | `<linux/perf_event.h>`，`<linux/bpf_perf_event.h>` |
