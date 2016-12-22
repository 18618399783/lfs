#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include "list.h"
#include "times.h"
#include "sched_timer.h"
#include "logger.h"

#define TIMER_MAX_ARG  8

struct timer {
    struct list_head list;
    long long deadline;
    timer_callback_t *callback;
    int argc;
    void *argv[TIMER_MAX_ARG];
};

static struct list_head timer_list;
static pthread_mutex_t timer_lock;
static pthread_t *task_vec = NULL;
static int task_nr = 0;
static int is_exit = 0;

static struct timer* timer_alloc()
{
    struct timer *timer;

    timer = malloc(sizeof(struct timer));
    assert(timer != NULL);
    memset(timer, 0, sizeof(struct timer));
    return timer;
}

static void timer_free(struct timer *timer)
{
    free(timer);
}

static void timer_add(struct timer *timer)
{
    struct list_head *pos;
    struct timer *tmp, *find = NULL;

    INIT_LIST_HEAD(&timer->list);
    pthread_mutex_lock(&timer_lock);
    list_for_each_prev(pos, &timer_list) {
        tmp = container_of(pos, struct timer, list);
        if (timer->deadline >= tmp->deadline) {
            find = tmp;
            break;
        }
    }
    if (find == NULL) {
        list_add(&timer->list, &timer_list);
    } else {
        list_add(&timer->list, &find->list);
    }    
    pthread_mutex_unlock(&timer_lock);   
}

static void __timer_remove(struct timer *timer)
{
    list_del(&timer->list);
}

static struct timer* __timer_peek()
{
    if (list_empty(&timer_list)) {
        return NULL;
    } else {
        return container_of(timer_list.next, struct timer, list);
    }
}

static void* do_timer_fn(void *arg)
{
    while (!is_exit) {
        sched_yielded();
    }
    return NULL;
}

void sched_yielded()
{
    struct timer *timer;
    int n = 0;

    while (1) {
        pthread_mutex_lock(&timer_lock);
        timer = __timer_peek();
        if (timer == NULL || timer->deadline > nowus()) {
            pthread_mutex_unlock(&timer_lock);
            break;
        } else {
            __timer_remove(timer);
            pthread_mutex_unlock(&timer_lock);
            if (timer->callback) {
                timer->callback(timer->argc, timer->argv);
            }
            timer_free(timer);
            n++;
        } 
    } 
    if (n <= 0) {
        usleep(10 * 1000);
    }
}

int sched_timer(long long usec, timer_callback_t *callback, ...)
{
    struct timer *timer;
    va_list ap;
    void *arg;
    int i = 0;

    timer = timer_alloc();
    timer->deadline = nowus() + usec;
    timer->callback = callback;
    va_start(ap, callback);
    while (1) {
        arg = va_arg(ap, void *);
        if (arg == NULL) {
            break;
        }
        if (i >= TIMER_MAX_ARG) {
            assert(0 && "timer args over maxnum.\n");
        }
        timer->argv[i] = arg;
        i++;
    }
    va_end(ap); 
    timer->argc = i;
    timer_add(timer);
    return 0;
}

void sched_sleep(long long usec)
{
    usleep(usec);   
}

int sched_timer_init()
{
    int i;

    is_exit = 0;
    INIT_LIST_HEAD(&timer_list);
    if (pthread_mutex_init(&timer_lock, NULL) != 0) {
        assert(0 && "pthread_mutex_init error.\n");
    }
    task_nr = get_nprocs();
    if (task_nr < 4) {
        task_nr = 4;
    };

    if (task_nr > 0) {    
        task_vec = malloc(sizeof(pthread_t) * task_nr);
        for (i = 0; i < task_nr; i++) {
            if (pthread_create(&task_vec[i], NULL, do_timer_fn, NULL) != 0) {
                assert(0 && "pthread_create error.\n");
            }
        }
    }
    return 0;
}

void sched_timer_exit()
{
    int i;

    is_exit = 1;
    for (i = 0; i < task_nr; i++) {
        pthread_join(task_vec[i], NULL);
    }
    pthread_mutex_destroy(&timer_lock);
    free(task_vec);
    task_vec = NULL;
}
