#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "execsnoop.skel.h"
#include "execsnoop.h"

static volatile sig_atomic_t stop;

static void on_sig(int sig)
{
  (void)sig;
  stop = 1;
}

/* perf 缓冲区的采样回调返回 void，丢包回调单独处理。 */
static void handle_event(void *ctx, int cpu, void *data, __u32 len)
{
  const struct event *e = data;

  (void)ctx;
  (void)cpu;
  if (len < sizeof(*e))
    return;
  printf("%-8d %-8d %-16s %s\n", e->pid, e->ppid, e->comm, e->filename);
}

static void handle_lost(void *ctx, int cpu, __u64 cnt)
{
  (void)ctx;
  fprintf(stderr, "lost %llu events on CPU %d\n",
          (unsigned long long)cnt, cpu);
}

#define SYS_BTF "/sys/kernel/btf/vmlinux"

/*
 * Kernel BTF is chosen in this order (same as cpa):
 *   1. the file given on the command line (explicit, always wins);
 *   2. /sys/kernel/btf/vmlinux, when the running kernel exposes it;
 *   3. otherwise stop with instructions, rather than failing inside libbpf.
 *
 * Usage: execsnoop [vmlinux.btf]
 */
static int check_btf(const char *arg, const char *prog)
{
  if (arg) {
    if (access(arg, R_OK) != 0) {
      fprintf(stderr, "cannot read BTF file '%s': %s\n", arg, strerror(errno));
      return -1;
    }
    return 0;
  }
  if (access(SYS_BTF, R_OK) != 0) {
    fprintf(stderr,
            "no kernel BTF at %s (kernel built without CONFIG_DEBUG_INFO_BTF?)\n"
            "pass a BTF file explicitly:  %s <vmlinux.btf>\n"
            "create one on a BTF-enabled host with ./export_btf.sh\n",
            SYS_BTF, prog);
    return -1;
  }
  return 0;
}

/*
 * Warn about kernels in the upstream range affected by the
 * copy_from_user_nofault() bug (5.19 through 6.3, including the 6.1 LTS line).
 * See docs/kernel-compatibility.md. This is a warning, not a refusal: vendor
 * kernels may carry the fix as a backport.
 */
static void warn_affected_kernel(void)
{
  struct utsname u;
  int major = 0, minor = 0;

  if (uname(&u) != 0 || sscanf(u.release, "%d.%d", &major, &minor) != 2)
    return;
  if ((major == 5 && minor >= 19) || (major == 6 && minor <= 3))
    fprintf(stderr,
            "warning: kernel %s is in the upstream range affected by the "
            "copy_from_user_nofault() bug (5.19-6.3); see docs/kernel-compatibility.md\n",
            u.release);
}

int main(int argc, char **argv)
{
  const char *btf_arg = argc > 1 ? argv[1] : NULL;
  /* 取命令行的第一个参数作为 BTF 文件路径。没有参数时为 NULL，
   * 表示用默认的 /sys/kernel/btf/vmlinux。 */
  struct bpf_object_open_opts opts = {
    .sz = sizeof(opts),
    .btf_custom_path = btf_arg,
  };
  struct execsnoop_bpf *skel;
  struct perf_buffer *pb = NULL;
  int err;

  if (argc > 2) {
    fprintf(stderr, "usage: %s [vmlinux.btf]\n", argv[0]);
    return 1;
  }
  if (check_btf(btf_arg, argv[0]) != 0)
    return 1;
  warn_affected_kernel();
  /* 1. 打开 */
  skel = execsnoop_bpf__open_opts(&opts);
  if (!skel) {
    fprintf(stderr, "failed to open BPF skeleton\n");
    return 1;
  }
  /* 2. load */
  err = execsnoop_bpf__load(skel);
  if (err) {
    fprintf(stderr, "failed to load BPF skeleton: %d\n", err);
    goto out;
  }
  /* 3. attach */
  err = execsnoop_bpf__attach(skel);
  if (err) {
    fprintf(stderr, "failed to attach: %d\n", err);
    goto out;
  }

  /* 每个 CPU 64 页（256 KiB）的缓冲区。 */
  pb = perf_buffer__new(bpf_map__fd(skel->maps.events), 64,
                        handle_event, handle_lost, NULL, NULL);
  if (!pb) {
    err = -1;
    fprintf(stderr, "failed to create perf buffer\n");
    goto out;
  }

  signal(SIGINT, on_sig);
  signal(SIGTERM, on_sig);

  printf("%-8s %-8s %-16s %s\n", "PID", "PPID", "COMM", "FILENAME");
  while (!stop) {
    err = perf_buffer__poll(pb, 100);
    if (err < 0 && err != -EINTR) {
      fprintf(stderr, "poll error: %d\n", err);
      break;
    }
    err = 0;
  }

out:
  perf_buffer__free(pb);
  /* 4. destroy */
  execsnoop_bpf__destroy(skel);
  return err != 0;
}
