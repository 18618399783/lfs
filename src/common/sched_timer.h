#ifndef _SCHED_TIMER_H_
#define _SCHED_TIMER_H_

#ifdef __cplusplus
extern "C"{
#endif
typedef void (timer_callback_t)(int argc, void *argv[]);

int sched_timer(long long usec, timer_callback_t *callback, ...);
void sched_sleep(long long usec);
void sched_yielded();
int sched_timer_init();
void sched_timer_exit();

#ifdef __cplusplus
}
#endif
#endif

