#ifndef _TIMER_H
#define _TIMER_H

#include <time.h>

extern struct timespec tstart;
extern struct timespec tend;
extern double deltaT;

#define START(tstart) (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tstart))
#define STOP(tend) (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tend))
#define DELTA(t1, t0) (((double)t1.tv_sec + 1.0e-9*t1.tv_nsec) - \
    ((double)t0.tv_sec + 1.0e-9*t0.tv_nsec))
#define DISPLAY_DELTA(str, deltaT) (printf("%s: dt = %lf\n", str, deltaT))

#endif