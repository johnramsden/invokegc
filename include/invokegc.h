#ifndef ZNSBENCH_H
#define ZNSBENCH_H



#define metric_printf(FD, M, ...)                                                                  \
    do {                                                                                           \
        if (FD != NULL)                                                                            \
            fprintf(FD, M, ##__VA_ARGS__);                                                         \
    } while (0)

#endif // ZNSBENCH_H
