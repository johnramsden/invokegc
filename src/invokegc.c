#define _GNU_SOURCE
// Needed for O_DIRECT

#include "invokegc.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
// #include <libzbd/zbd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <time.h>

// Emulate dumb workload, good for ZNS

// 1) Fill up the drive -- the load phase
// 2) Run phase: a read thread and a write thread. Possible to vary the number of
//    threads if needed later. The write thread overwrites blocks. In ZNS-ideal
//    case: pick a zone and overwrite it sequentially.

// Hypothesis: that’s not ideal for SSD, so we will see higher latency.

// Measure: latency average and standard deviation for reads, separately for writes
// on SSD and ZNS and also graph how latency avg/stdev changes over time. Plot CDF
// of latencies.

#define FILL_BUFFER_SIZE 1024*1024
#define BUFFER_SIZE 4096  // Size of each buffer
#define GIGABYTE 1024*1024*1024

// Structure to hold thread arguments
typedef struct {
    int thread_id;
    int fd;
    off_t start_offset;
    size_t bytes_to_process;
    int *reader_done_count;
    int total_readers;
    pthread_mutex_t *mutex;
} thread_arg_t;

// Function to fill a buffer with random data
void fill_random_data(char *buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = rand() % 256;  // Fill buffer with random byte values
    }
}

// Function to generate a random offset within a range
off_t random_offset(off_t start, size_t range) {
    return start + (rand() % (range / BUFFER_SIZE)) * BUFFER_SIZE;
}

// Thread function to fill a portion of the device with random data sequentially
void *fill_device_with_random_data_thread(void *arg) {
    thread_arg_t *thread_args = (thread_arg_t *)arg;
    char buffer[FILL_BUFFER_SIZE];
    fill_random_data(buffer, FILL_BUFFER_SIZE);

    off_t current_offset = thread_args->start_offset;
    size_t total_written = 0;
    size_t last_reported_progress = 0;  // To track last reported progress

    while (total_written < thread_args->bytes_to_process) {
        ssize_t bytes_to_write = (thread_args->bytes_to_process - total_written > FILL_BUFFER_SIZE) ? FILL_BUFFER_SIZE : (thread_args->bytes_to_process - total_written);
        ssize_t bytes_written = pwrite(thread_args->fd, buffer, bytes_to_write, current_offset);
        if (bytes_written == -1) {
            perror("pwrite");
            pthread_exit(NULL);
        }

        current_offset += bytes_written;
        total_written += bytes_written;

        // Calculate progress in percentage
        size_t progress = (total_written * 100) / thread_args->bytes_to_process;
        if (progress >= last_reported_progress + 10) {  // Report progress every 10%
            dbg_printf("Fill Thread %d: %zu%% complete\n", thread_args->thread_id, progress);
            last_reported_progress = progress;  // Update last reported progress
        }
    }

    dbg_printf("Fill Thread %d: Finished filling %zu bytes\n", thread_args->thread_id, total_written);
    pthread_exit(NULL);
}

// Multi-threaded function to fill the entire device with random data sequentially
void fill_device_with_random_data(int fd, size_t total_size, int num_threads) {
    pthread_t fill_threads[num_threads];
    thread_arg_t fill_args[num_threads];

    size_t size_per_thread = total_size / num_threads;

    for (int i = 0; i < num_threads; ++i) {
        fill_args[i].thread_id = i;
        fill_args[i].fd = fd;
        fill_args[i].start_offset = i * size_per_thread;
        fill_args[i].bytes_to_process = size_per_thread;
        fill_args[i].reader_done_count = NULL;
        fill_args[i].mutex = NULL;

        if (pthread_create(&fill_threads[i], NULL, fill_device_with_random_data_thread, &fill_args[i]) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    for (int i = 0; i < num_threads; ++i) {
        pthread_join(fill_threads[i], NULL);
    }

    dbg_printf("Finished filling device with %zu bytes of random data using %d threads.\n", total_size, num_threads);
}

// Thread function to write random data to different random locations on a raw device
void *write_random_data(void *arg) {
    thread_arg_t *thread_args = (thread_arg_t *)arg;
    char buffer[BUFFER_SIZE];
    struct timespec start_time, end_time;

    while (true) {
        pthread_mutex_lock(thread_args->mutex);
        if (*thread_args->reader_done_count >= thread_args->total_readers) {
            pthread_mutex_unlock(thread_args->mutex);
            break;  // Stop writing only when all reader threads are done
        }
        pthread_mutex_unlock(thread_args->mutex);

        fill_random_data(buffer, BUFFER_SIZE);

        // Generate random offset within the assigned range
        off_t random_loc = random_offset(thread_args->start_offset, thread_args->bytes_to_process);

        // Measure write latency
        TIME_NOW(&start_time);
        ssize_t bytes_written = pwrite(thread_args->fd, buffer, BUFFER_SIZE, random_loc);
        TIME_NOW(&end_time);

        if (bytes_written == -1) {
            perror("pwrite");
            pthread_exit(NULL);
        }

        double latency = TIME_DIFFERENCE_NSEC(start_time, end_time);
        printf("T%d, W, %.5f\n", thread_args->thread_id, latency);
    }

    dbg_printf("Writer Thread %d: Stopped writing because all reader threads have finished.\n", thread_args->thread_id);
    pthread_exit(NULL);
}

// Thread function to read data from random locations on a raw device
void *read_random_data(void *arg) {
    thread_arg_t *thread_args = (thread_arg_t *)arg;
    char buffer[BUFFER_SIZE];
    struct timespec start_time, end_time;

    size_t total_read = 0;
    while (total_read < thread_args->bytes_to_process) {
        // Generate random offset within the assigned range
        off_t random_loc = random_offset(thread_args->start_offset, thread_args->bytes_to_process);
        ssize_t bytes_to_read = (thread_args->bytes_to_process - total_read > BUFFER_SIZE) ? BUFFER_SIZE : (thread_args->bytes_to_process - total_read);

        // Measure read latency
        TIME_NOW(&start_time);
        ssize_t bytes_read = pread(thread_args->fd, buffer, bytes_to_read, random_loc);
        TIME_NOW(&end_time);

        if (bytes_read == -1) {
            perror("pread");
            pthread_exit(NULL);
        }

        double latency = TIME_DIFFERENCE_NSEC(start_time, end_time);
        printf("T%d, R, %.5f\n", thread_args->thread_id, latency);

        total_read += bytes_read;
    }

    dbg_printf("Reader Thread %d: Finished reading %ld bytes\n", thread_args->thread_id, total_read);

    // Signal that this reader thread is complete
    pthread_mutex_lock(thread_args->mutex);
    (*thread_args->reader_done_count)++;
    pthread_mutex_unlock(thread_args->mutex);

    pthread_exit(NULL);
}

off_t get_device_size(int fd) {
    off_t size;
    if (ioctl(fd, BLKGETSIZE64, &size) != 0) {
        perror("ioctl");
        return -1;
    }
    return size;
}

void print_usage(char *prog_name) {
    fprintf(stderr, "Usage: %s -d <device_path> -w <number_of_writer_threads> -r <number_of_reader_threads> -s <total_size_to_process_gb> [-D use O_DIRECT|O_SYNC] [-o (read only)] [-l (load only)]\n", prog_name);
}

int main(int argc, char *argv[]) {
    // Check if the program is run as root
    if (geteuid() != 0) {
        fprintf(stderr, "Please run as root\n");
        return -1;
    }

    int opt;
    char *device_path = NULL;
    int num_writer_threads = -1;
    int num_reader_threads = -1;
    size_t total_size_to_process = 0;
    int read_only = 0;  // Default to false
    int load_only = 0;       // Default to false
    int use_o_direct = 0;    // O_DIRECT flag (default to off)
    size_t alignment_size = BUFFER_SIZE;  // Default alignment size

    while ((opt = getopt(argc, argv, "d:w:r:s:olD")) != -1) {
        switch (opt) {
            case 'd':
                device_path = optarg;
                break;
            case 'w':
                num_writer_threads = atoi(optarg);
                break;
            case 'r':
                num_reader_threads = atoi(optarg);
                break;
            case 's':
                total_size_to_process = atol(optarg) * GIGABYTE;
                break;
            case 'o':
                read_only = 1;
                break;
            case 'l':
                load_only = 1;
                break;
            case 'D':
                use_o_direct = 1;
                break;
            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Ensure required arguments are provided
    if (device_path == NULL || num_writer_threads < 0 || num_reader_threads < 0 || total_size_to_process < 0) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    size_t size_per_read_thread = 0;
    if (!load_only) {
        size_per_read_thread = total_size_to_process / num_reader_threads;
    }

    size_t size_per_write_thread = 0;
    if (!read_only) {
        size_per_write_thread = total_size_to_process / num_writer_threads;
    }

    // Open the device with optional O_DIRECT
    int flags = O_RDWR;
    if (use_o_direct) {
        flags |= (O_DIRECT|O_SYNC);
    }

    int fd = open(device_path, flags);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    off_t device_size = get_device_size(fd);
    if (device_size < total_size_to_process) {
        fprintf(stderr, "Total size to process %lu exceeds device size: %lu\n", total_size_to_process, device_size);
        close(fd);
        return -1;
    }

    // Seed the random number generator
    srand(time(NULL));

    // Step 1: Fill the device with random data using multiple threads
    if (load_only) {
        fill_device_with_random_data(fd, total_size_to_process, num_writer_threads);
    } else {
        pthread_t writer_threads[num_writer_threads];
        pthread_t reader_threads[num_reader_threads];
        thread_arg_t writer_args[num_writer_threads];
        thread_arg_t reader_args[num_reader_threads];

        int reader_done_count = 0;
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

        if (!read_only) {
            // Create writer threads
            for (int i = 0; i < num_writer_threads; ++i) {
                writer_args[i].thread_id = i;
                writer_args[i].fd = fd;
                writer_args[i].start_offset = i * size_per_write_thread;
                writer_args[i].bytes_to_process = size_per_write_thread;
                writer_args[i].reader_done_count = &reader_done_count;
                writer_args[i].total_readers = num_reader_threads;
                writer_args[i].mutex = &mutex;

                if (pthread_create(&writer_threads[i], NULL, write_random_data, &writer_args[i]) != 0) {
                    perror("pthread_create");
                    close(fd);
                    return 1;
                }
            }
        }

        // Create reader threads
        for (int i = 0; i < num_reader_threads; ++i) {
            reader_args[i].thread_id = i;
            reader_args[i].fd = fd;
            reader_args[i].start_offset = i * size_per_read_thread;
            reader_args[i].bytes_to_process = size_per_read_thread;
            reader_args[i].reader_done_count = &reader_done_count;
            reader_args[i].total_readers = num_reader_threads;
            reader_args[i].mutex = &mutex;

            if (pthread_create(&reader_threads[i], NULL, read_random_data, &reader_args[i]) != 0) {
                perror("pthread_create");
                close(fd);
                return 1;
            }
        }

        // Wait for reader threads to finish
        for (int i = 0; i < num_reader_threads; ++i) {
            pthread_join(reader_threads[i], NULL);
        }

        if (!read_only) {
            // Wait for writer threads to finish
            for (int i = 0; i < num_writer_threads; ++i) {
                pthread_join(writer_threads[i], NULL);
            }
        }
    }

    close(fd);
    dbg_printf("Completed processing %zu bytes with %d writer threads and %d reader threads on device %s.\n", total_size_to_process, num_writer_threads, num_reader_threads, device_path);
    return 0;
}
