#include "bc_os.h"
#include "bc_log.h"
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint32_t count;

} bc_os_semaphore_primitives_t;

void bc_os_task_init(bc_os_task_t *task, void *(*task_function)(void *), void *parameter)
{
    task->_task = malloc(sizeof(pthread_t));

    if (task->_task == NULL)
    {
        bc_log_fatal("bc_os_task_init: call failed: malloc");
    }

    if (pthread_create((pthread_t *) task->_task, NULL, task_function, parameter) != 0)
    {
        bc_log_fatal("bc_os_task_init: call failed: pthread_create");
    }
}

void bc_os_task_destroy(bc_os_task_t *task)
{
    if (pthread_join(*((pthread_t *) task->_task), NULL) != 0)
    {
        bc_log_fatal("bc_os_task_destroy: call failed: pthread_join");
    }

    free(task->_task);

    task->_task = NULL;
}

bool bc_os_task_is_alive(bc_os_task_t *task)
{
    return pthread_kill(*(pthread_t *) task->_task, 0) == 0 ? true : false;
}


void bc_os_task_sleep(bc_tick_t timeout)
{
    if (timeout < 0)
    {
        return;
    }

    if (usleep(timeout * 1000UL) != 0)
    {
        bc_log_fatal("bc_os_sleep: call failed: usleep");
    }
}

void bc_os_mutex_init(bc_os_mutex_t *mutex)
{
    mutex->_mutex = malloc(sizeof(pthread_mutex_t));

    if (mutex->_mutex == NULL)
    {
        bc_log_fatal("bc_os_mutex_init: call failed: malloc");
    }

    if (pthread_mutex_init((pthread_mutex_t *) mutex->_mutex, NULL) != 0)
    {
        bc_log_fatal("bc_os_mutex_init: call failed: pthread_mutex_init");
    }
}

void bc_os_mutex_destroy(bc_os_mutex_t *mutex)
{
    if (pthread_mutex_destroy((pthread_mutex_t *) mutex->_mutex) != 0)
    {
        bc_log_fatal("bc_os_mutex_destroy: call failed: pthread_mutex_destroy");
    }

    free(mutex->_mutex);
}

void bc_os_mutex_lock(bc_os_mutex_t *mutex)
{
    if (pthread_mutex_lock((pthread_mutex_t *) mutex->_mutex) != 0)
    {
        bc_log_fatal("bc_os_mutex_lock: call failed: pthread_mutex_lock");
    }
}

void bc_os_mutex_unlock(bc_os_mutex_t *mutex)
{
    if (pthread_mutex_unlock((pthread_mutex_t *) mutex->_mutex) != 0)
    {
        bc_log_fatal("bc_os_mutex_unlock: call failed: pthread_mutex_unlock");
    }
}

void bc_os_semaphore_init(bc_os_semaphore_t *semaphore, uint32_t value)
{
    bc_os_semaphore_primitives_t *primitives;

    pthread_condattr_t cond_attributes;

    semaphore->_semaphore = malloc(sizeof(bc_os_semaphore_primitives_t));

    if (semaphore->_semaphore == NULL)
    {
        bc_log_fatal("bc_os_semaphore_init: call failed: malloc");
    }

    primitives = (bc_os_semaphore_primitives_t *) semaphore->_semaphore;

    primitives->count = value;

    if (pthread_mutex_init(&primitives->mutex, NULL) != 0)
    {
        bc_log_fatal("bc_os_semaphore_init: call failed: pthread_mutex_init");
    }

    if (pthread_condattr_init(&cond_attributes) != 0)
    {
        bc_log_fatal("bc_os_semaphore_init: call failed: pthread_condattr_init");
    }

    if (pthread_condattr_setclock(&cond_attributes, CLOCK_MONOTONIC) != 0)
    {
        bc_log_fatal("bc_os_semaphore_init: call failed: pthread_condattr_setclock");
    }

    if (pthread_cond_init(&primitives->cond, &cond_attributes) != 0)
    {
        bc_log_fatal("bc_os_semaphore_init: call failed: pthread_cond_init");
    }
}

void bc_os_semaphore_destroy(bc_os_semaphore_t *semaphore)
{
    bc_os_semaphore_primitives_t *primitives;

    primitives = (bc_os_semaphore_primitives_t *) semaphore->_semaphore;

    if (pthread_cond_destroy(&primitives->cond) != 0)
    {
        bc_log_fatal("bc_os_semaphore_destroy: call failed: pthread_cond_destroy");
    }

    if (pthread_mutex_destroy(&primitives->mutex) != 0)
    {
        bc_log_fatal("bc_os_semaphore_destroy: call failed: pthread_mutex_destroy");
    }

    free(semaphore->_semaphore);
}

void bc_os_semaphore_put(bc_os_semaphore_t *semaphore)
{
    bc_os_semaphore_primitives_t *primitives;

    primitives = (bc_os_semaphore_primitives_t *) semaphore->_semaphore;

    if (pthread_mutex_lock(&primitives->mutex) != 0)
    {
        bc_log_fatal("bc_os_semaphore_put: call failed: pthread_mutex_lock");
    }

    if (primitives->count == 0)
    {
        if (pthread_cond_signal(&primitives->cond) != 0)
        {
            bc_log_fatal("bc_os_semaphore_put: call failed: pthread_cond_signal");
        }
    }

    primitives->count++;

    if (pthread_mutex_unlock(&primitives->mutex) != 0)
    {
        bc_log_fatal("bc_os_semaphore_put: call failed: pthread_mutex_unlock");
    }
}

void bc_os_semaphore_get(bc_os_semaphore_t *semaphore)
{
    bc_os_semaphore_primitives_t *primitives;

    primitives = (bc_os_semaphore_primitives_t *) semaphore->_semaphore;

    if (pthread_mutex_lock(&primitives->mutex) != 0)
    {
        bc_log_fatal("bc_os_semaphore_get: call failed: pthread_mutex_lock");
    }

    while (primitives->count == 0)
    {
        if (pthread_cond_wait(&primitives->cond, &primitives->mutex) != 0)
        {
            bc_log_fatal("bc_os_semaphore_get: call failed: pthread_cond_wait");
        }
    }

    primitives->count--;

    if (pthread_mutex_unlock(&primitives->mutex) != 0)
    {
        bc_log_fatal("bc_os_semaphore_get: call failed: pthread_mutex_unlock");
    }
}

bool bc_os_semaphore_timed_get(bc_os_semaphore_t *semaphore, bc_tick_t timeout)
{
    bc_os_semaphore_primitives_t *primitives;

    primitives = (bc_os_semaphore_primitives_t *) semaphore->_semaphore;

    if (pthread_mutex_lock(&primitives->mutex) != 0)
    {
        bc_log_fatal("bc_os_semaphore_timed_get: call failed: pthread_mutex_lock");
    }

    if (timeout == 0)
    {
        if (primitives->count == 0)
        {
            if (pthread_mutex_unlock(&primitives->mutex) != 0)
            {
                bc_log_fatal("bc_os_semaphore_timed_get: call failed: pthread_mutex_unlock");
            }

            return false;
        }
    }
    else
    {
        struct timespec ts;

        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        {
            bc_log_fatal("bc_os_semaphore_timed_get: call failed: clock_gettime");
        }

        ts.tv_sec += timeout / 1000UL;
        ts.tv_nsec += (timeout % 1000UL) * 1000000UL;
        ts.tv_sec += ts.tv_nsec / 1000000000UL;
        ts.tv_nsec %= 1000000000UL;

        while (primitives->count == 0)
        {
            int res;

            res = pthread_cond_timedwait(&primitives->cond, &primitives->mutex, &ts);

            if (res == ETIMEDOUT)
            {
                if (pthread_mutex_unlock(&primitives->mutex) != 0)
                {
                    bc_log_fatal("bc_os_semaphore_timed_get: call failed: pthread_mutex_unlock");
                }

                return false;
            }

            if (res != 0)
            {
                bc_log_fatal("bc_os_semaphore_timed_get: call failed: pthread_cond_timedwait");
            }
        }
    }

    primitives->count--;

    if (pthread_mutex_unlock(&primitives->mutex) != 0)
    {
        bc_log_fatal("bc_os_semaphore_timed_get: call failed: pthread_mutex_unlock");
    }

    return true;
}
