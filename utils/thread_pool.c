#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include "thread_pool.h"

struct thread_pool_task {
  thread_pool_task_fn fn;
  void *arg;
  struct thread_pool_task *next;
};

struct thread_pool {
  pthread_t *threads;
  size_t thread_count;
  struct thread_pool_task *head;
  struct thread_pool_task *tail;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  bool stopping;
};

static void *thread_pool_worker(void *arg)
{
  struct thread_pool *pool = arg;

  for (;;) {
    struct thread_pool_task *task;

    pthread_mutex_lock(&pool->mutex);
    while (!pool->head && !pool->stopping)
      pthread_cond_wait(&pool->condition, &pool->mutex);

    if (!pool->head && pool->stopping) {
      pthread_mutex_unlock(&pool->mutex);
      return NULL;
    }

    task = pool->head;
    pool->head = task->next;
    if (!pool->head)
      pool->tail = NULL;
    pthread_mutex_unlock(&pool->mutex);

    task->fn(task->arg);
    free(task);
  }
}

struct thread_pool *thread_pool_create(size_t thread_count)
{
  struct thread_pool *pool;
  size_t i;
  int err;

  if (thread_count == 0) {
    errno = EINVAL;
    return NULL;
  }

  pool = calloc(1, sizeof(*pool));
  if (!pool)
    return NULL;

  pool->threads = calloc(thread_count, sizeof(*pool->threads));
  if (!pool->threads)
    goto fail_pool;

  err = pthread_mutex_init(&pool->mutex, NULL);
  if (err != 0) {
    errno = err;
    goto fail_threads;
  }
  err = pthread_cond_init(&pool->condition, NULL);
  if (err != 0) {
    errno = err;
    goto fail_mutex;
  }

  for (i = 0; i < thread_count; i++) {
    err = pthread_create(&pool->threads[i], NULL, thread_pool_worker, pool);
    if (err != 0) {
      errno = err;
      pthread_mutex_lock(&pool->mutex);
      pool->stopping = true;
      pthread_cond_broadcast(&pool->condition);
      pthread_mutex_unlock(&pool->mutex);
      while (i > 0)
        pthread_join(pool->threads[--i], NULL);
      pthread_cond_destroy(&pool->condition);
      pthread_mutex_destroy(&pool->mutex);
      goto fail_threads;
    }
  }

  pool->thread_count = thread_count;
  return pool;

fail_mutex:
  pthread_mutex_destroy(&pool->mutex);
fail_threads:
  free(pool->threads);
fail_pool:
  free(pool);
  return NULL;
}

int thread_pool_submit(struct thread_pool *pool, thread_pool_task_fn fn,
                       void *arg)
{
  struct thread_pool_task *task;

  if (!pool || !fn) {
    errno = EINVAL;
    return -1;
  }

  task = malloc(sizeof(*task));
  if (!task)
    return -1;
  task->fn = fn;
  task->arg = arg;
  task->next = NULL;

  pthread_mutex_lock(&pool->mutex);
  if (pool->stopping) {
    pthread_mutex_unlock(&pool->mutex);
    free(task);
    errno = ESHUTDOWN;
    return -1;
  }

  if (pool->tail)
    pool->tail->next = task;
  else
    pool->head = task;
  pool->tail = task;
  pthread_cond_signal(&pool->condition);
  pthread_mutex_unlock(&pool->mutex);
  return 0;
}

void thread_pool_destroy(struct thread_pool *pool)
{
  size_t i;
  struct thread_pool_task *task, *next;

  if (!pool)
    return;

  pthread_mutex_lock(&pool->mutex);
  pool->stopping = true;
  pthread_cond_broadcast(&pool->condition);
  pthread_mutex_unlock(&pool->mutex);

  for (i = 0; i < pool->thread_count; i++)
    pthread_join(pool->threads[i], NULL);

  /* Workers drain the queue before exiting; this is only defensive cleanup. */
  for (task = pool->head; task; task = next) {
    next = task->next;
    free(task);
  }

  pthread_cond_destroy(&pool->condition);
  pthread_mutex_destroy(&pool->mutex);
  free(pool->threads);
  free(pool);
}
