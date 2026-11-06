#ifndef TRACEPOINT_H
#define TRACEPOINT_H

#include "uthash_map.h"

int tracepoint_sys_enter_execve(struct uthash_map *args);

#endif
