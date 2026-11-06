#include <stdio.h>
#include <string.h>
#include "argparse.h"
#include "uthash_map.h"
#include "execsnoop_args.h"
#include "tracepoint.h"

int main(int argc, char **argv)
{
  const char *btf = NULL;
  const char *ktype = EXECSNOOP_KTYP_ORIGIN;
  const char *utype = EXECSNOOP_UTYP_TRACEPOINT_SYS_ENTER_EXECVE;
  struct uthash_map *args = NULL;
  const char *const usages[] = {
    "execsnoop [options]",
    NULL,
  };
  struct argparse_option options[] = {
    OPT_HELP(),
    OPT_STRING(0, EXECSNOOP_ARG_KTYP, &ktype,
               "kernel event type: " EXECSNOOP_KTYP_BPF " or " EXECSNOOP_KTYP_ORIGIN
               " (default: " EXECSNOOP_KTYP_ORIGIN ")", NULL, 0, 0),
    OPT_STRING(0, EXECSNOOP_ARG_UTYP, &utype,
               "user event type (default: " EXECSNOOP_UTYP_TRACEPOINT_SYS_ENTER_EXECVE ")",
               NULL, 0, 0),
    OPT_STRING('b', EXECSNOOP_ARG_BTF, &btf,
               "BTF file (default: /sys/kernel/btf/vmlinux)", NULL, 0, 0),
    OPT_END(),
  };
  struct argparse parser;
  int positional_count;
  int ret;

  argparse_init(&parser, options, usages, 0);
  argparse_describe(&parser,
                    "Trace execve() and print PID, PPID, COMM and filename.", NULL);
  positional_count = argparse_parse(&parser, argc, (const char **)argv);
  if (positional_count != 0) {
    fprintf(stderr, "error: positional arguments are not supported\n");
    argparse_usage(&parser);
    return 1;
  }

  /* Keep parsed options in a name-keyed map for backend dispatch. */
  if (uthash_map_set(&args, EXECSNOOP_ARG_KTYP, ktype, false) != 0 ||
      uthash_map_set(&args, EXECSNOOP_ARG_UTYP, utype, false) != 0 ||
      uthash_map_set(&args, EXECSNOOP_ARG_BTF, btf, false) != 0) {
    fprintf(stderr, "error: failed to store command-line arguments\n");
    uthash_map_free(args);
    return 1;
  }

  ktype = uthash_map_get(args, EXECSNOOP_ARG_KTYP);
  utype = uthash_map_get(args, EXECSNOOP_ARG_UTYP);
  if (!ktype || !utype) {
    fprintf(stderr, "error: missing required dispatch arguments\n");
    uthash_map_free(args);
    return 1;
  }

  if (strcmp(ktype, EXECSNOOP_KTYP_BPF) == 0 &&
      strcmp(utype, EXECSNOOP_UTYP_TRACEPOINT_SYS_ENTER_EXECVE) == 0) {
    ret = tracepoint_sys_enter_execve(args);
  } else {
    fprintf(stderr, "error: unsupported event combination --ktype %s --utype %s\n",
            ktype, utype);
    argparse_usage(&parser);
    ret = 1;
  }

  uthash_map_free(args);
  return ret;
}
