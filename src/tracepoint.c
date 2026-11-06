#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "execsnoop.skel.h"
#include "execsnoop.h"
#include "execsnoop_args.h"
#include "tracepoint.h"

static volatile sig_atomic_t stop;

static void on_sig(int sig)
{
  (void)sig;
  stop = 1;
}

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

static int check_btf(const char *path)
{
  if (path) {
    if (access(path, R_OK) != 0) {
      fprintf(stderr, "cannot read BTF file '%s': %s\n", path, strerror(errno));
      return -1;
    }
    return 0;
  }

  if (access(SYS_BTF, R_OK) != 0) {
    fprintf(stderr,
            "no kernel BTF at %s (kernel built without CONFIG_DEBUG_INFO_BTF?)\n"
            "pass a BTF file explicitly with --btf <vmlinux.btf>\n"
            "create one on a BTF-enabled host with ./export_btf.sh\n",
            SYS_BTF);
    return -1;
  }
  return 0;
}

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

int tracepoint_sys_enter_execve(struct uthash_map *args)
{
  const char *btf = uthash_map_get(args, EXECSNOOP_ARG_BTF);
  struct bpf_object_open_opts opts = {
    .sz = sizeof(opts),
    .btf_custom_path = btf,
  };
  struct execsnoop_bpf *skel;
  struct perf_buffer *pb = NULL;
  int err = 0;

  if (check_btf(btf) != 0)
    return 1;

  warn_affected_kernel();
  skel = execsnoop_bpf__open_opts(&opts);
  if (!skel) {
    fprintf(stderr, "failed to open BPF skeleton\n");
    return 1;
  }

  err = execsnoop_bpf__load(skel);
  if (err) {
    fprintf(stderr, "failed to load BPF skeleton: %d\n", err);
    goto out;
  }

  err = execsnoop_bpf__attach(skel);
  if (err) {
    fprintf(stderr, "failed to attach: %d\n", err);
    goto out;
  }

  pb = perf_buffer__new(bpf_map__fd(skel->maps.events), 64,
                        handle_event, handle_lost, NULL, NULL);
  if (!pb) {
    err = -1;
    fprintf(stderr, "failed to create perf buffer\n");
    goto out;
  }

  stop = 0;
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
  execsnoop_bpf__destroy(skel);
  return err != 0;
}
