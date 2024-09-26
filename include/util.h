#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

/* Will only print messages (to stdout) when DEBUG is defined */
#ifdef DEBUG
#    define dbg_printf(M, ...) printf("%s: " M, __func__, ##__VA_ARGS__)
#else
#    define dbg_printf(...)
#endif

#define TIME_NOW(_t) (clock_gettime(CLOCK_MONOTONIC, (_t)))

#define TIME_DIFFERENCE(_start, _end)                                                              \
    ((_end.tv_sec + _end.tv_nsec / 1.0e9) - (_start.tv_sec + _start.tv_nsec / 1.0e9))

#define TIME_DIFFERENCE_MILLISEC(_start, _end)                                                     \
    ((_end.tv_nsec / 1.0e6 < _start.tv_nsec / 1.0e6)) ?                                            \
        ((_end.tv_sec - 1.0 - _start.tv_sec) * 1.0e3 + (_end.tv_nsec / 1.0e6) + 1.0e3 -            \
         (_start.tv_nsec / 1.0e6)) :                                                               \
        ((_end.tv_sec - _start.tv_sec) * 1.0e3 + (_end.tv_nsec / 1.0e6) -                          \
         (_start.tv_nsec / 1.0e6))

#define TIME_DIFFERENCE_NSEC(_start, _end)                                                         \
    ((_end.tv_nsec < _start.tv_nsec)) ?                                                            \
        ((_end.tv_sec - 1 - (_start.tv_sec)) * 1e9 + _end.tv_nsec + 1e9 - _start.tv_nsec) :        \
        ((_end.tv_sec - (_start.tv_sec)) * 1e9 + _end.tv_nsec - _start.tv_nsec)

#define TIME_DIFFERENCE_GETTIMEOFDAY(_start, _end)                                                 \
    ((_end.tv_sec + _end.tv_usec / 1.0e6) - (_start.tv_sec + _start.tv_usec / 1.0e6))

#endif // UTIL_H
