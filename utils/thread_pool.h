#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>

struct thread_pool;
typedef void (*thread_pool_task_fn)(void *arg);

/*
 * Create a pool with thread_count workers. Returns NULL and sets errno on
 * failure; thread_count must be greater than zero.
 */
struct thread_pool *thread_pool_create(size_t thread_count);

/*
 * Queue a task for execution. Returns 0 on success, or -1 with errno set.
 * Once destruction starts, submissions fail with errno set to ESHUTDOWN.
 */
int thread_pool_submit(struct thread_pool *pool, thread_pool_task_fn fn,
                       void *arg);

/*
 * Stop accepting tasks, finish queued tasks, wait for all workers, and free
 * the pool. Passing NULL is allowed. Do not call from one of the pool's
 * workers, and synchronize concurrent submit/destroy calls externally.
 */
void thread_pool_destroy(struct thread_pool *pool);

#endif
