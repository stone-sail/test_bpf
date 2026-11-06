#include <errno.h>
#include <stdio.h>
#include "thread_pool.h"

#define TASK_COUNT 100

struct task_context {
  int value;
  int *results;
  int *result_count;
};

static void record_task(void *arg)
{
  struct task_context *ctx = arg;

  ctx->results[(*ctx->result_count)++] = ctx->value;
}

static int test_invalid_arguments(void)
{
  struct thread_pool *pool;

  errno = 0;
  pool = thread_pool_create(0);
  if (pool != NULL || errno != EINVAL) {
    fprintf(stderr, "thread_pool_create(0) should fail with EINVAL\n");
    return 1;
  }

  errno = 0;
  if (thread_pool_submit(NULL, record_task, NULL) != -1 || errno != EINVAL) {
    fprintf(stderr, "thread_pool_submit(NULL, ...) should fail with EINVAL\n");
    return 1;
  }

  pool = thread_pool_create(1);
  if (!pool) {
    perror("thread_pool_create");
    return 1;
  }
  errno = 0;
  if (thread_pool_submit(pool, NULL, NULL) != -1 || errno != EINVAL) {
    fprintf(stderr, "thread_pool_submit(..., NULL, ...) should fail with EINVAL\n");
    thread_pool_destroy(pool);
    return 1;
  }
  thread_pool_destroy(pool);
  return 0;
}

static int test_fifo_and_drain_on_destroy(void)
{
  struct thread_pool *pool;
  struct task_context tasks[TASK_COUNT];
  int results[TASK_COUNT];
  int result_count = 0;
  int i;

  pool = thread_pool_create(1);
  if (!pool) {
    perror("thread_pool_create");
    return 1;
  }

  for (i = 0; i < TASK_COUNT; i++) {
    tasks[i].value = i;
    tasks[i].results = results;
    tasks[i].result_count = &result_count;
    if (thread_pool_submit(pool, record_task, &tasks[i]) != 0) {
      perror("thread_pool_submit");
      thread_pool_destroy(pool);
      return 1;
    }
  }

  /* Destruction must wait for all accepted tasks to finish. */
  thread_pool_destroy(pool);

  if (result_count != TASK_COUNT) {
    fprintf(stderr, "expected %d completed tasks, got %d\n",
            TASK_COUNT, result_count);
    return 1;
  }
  for (i = 0; i < TASK_COUNT; i++) {
    if (results[i] != i) {
      fprintf(stderr, "task order mismatch at %d: got %d\n", i, results[i]);
      return 1;
    }
  }

  return 0;
}

int main(void)
{
  if (test_invalid_arguments() != 0)
    return 1;
  if (test_fifo_and_drain_on_destroy() != 0)
    return 1;

  puts("thread_pool tests passed");
  return 0;
}
