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
        stop = 1;
}

static int handle_event(void *ctx, void *data, size_t len)
{
        const struct event *e = data;

        printf("%-8d %-8d %-16s %s\n", e->pid, e->ppid, e->comm, e->filename);
        return 0;
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
        struct bpf_object_open_opts opts = {
                .sz = sizeof(opts),
                .btf_custom_path = btf_arg,
        };
        struct execsnoop_bpf *skel;
        struct ring_buffer *rb = NULL;
        int err;

        if (argc > 2) {
                fprintf(stderr, "usage: %s [vmlinux.btf]\n", argv[0]);
                return 1;
        }
        if (check_btf(btf_arg, argv[0]) != 0)
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

        rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
        if (!rb) {
                err = -1;
                fprintf(stderr, "failed to create ring buffer\n");
                goto out;
        }

        signal(SIGINT, on_sig);
        signal(SIGTERM, on_sig);

        printf("%-8s %-8s %-16s %s\n", "PID", "PPID", "COMM", "FILENAME");
        while (!stop) {
                err = ring_buffer__poll(rb, 100);
                if (err < 0 && err != -EINTR) {
                        fprintf(stderr, "poll error: %d\n", err);
                        break;
                }
                err = 0;
        }

out:
        ring_buffer__free(rb);
        execsnoop_bpf__destroy(skel);
        return err != 0;
}
