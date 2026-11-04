#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "execsnoop.h"

char LICENSE[] SEC("license") = "GPL";

/* 事件通过 perf 缓冲区送到用户态，每个 CPU 一个环形缓冲区。
 * 不设置 max_entries，由 libbpf 按可用 CPU 数分配。 */
struct {
  __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
  __uint(key_size, sizeof(__u32));
  __uint(value_size, sizeof(__u32));
} events SEC(".maps");

/* 每 CPU 的暂存区，避免把 152 字节的事件结构放在 BPF 栈上。 */
struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, 1);
  __type(key, __u32);
  __type(value, struct event);
} scratch SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(struct trace_event_raw_sys_enter *ctx)
{
  struct task_struct *task = (struct task_struct *)bpf_get_current_task();
  struct event *e;
  __u32 zero = 0;

  e = bpf_map_lookup_elem(&scratch, &zero);
  if (!e)
    return 0;

  e->pid = bpf_get_current_pid_tgid() >> 32;
  e->ppid = BPF_CORE_READ(task, real_parent, tgid);
  bpf_get_current_comm(&e->comm, sizeof(e->comm));
  bpf_probe_read_user_str(&e->filename, sizeof(e->filename),
                          (const char *)ctx->args[0]);

  bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                        e, sizeof(*e));
  return 0;
}
