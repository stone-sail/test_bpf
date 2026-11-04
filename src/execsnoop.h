#ifndef EXECSNOOP_H
#define EXECSNOOP_H

#define TASK_COMM_LEN 16
#define FILENAME_LEN 128

struct event {
  int pid;
  int ppid;
  char comm[TASK_COMM_LEN];
  char filename[FILENAME_LEN];
};

#endif
