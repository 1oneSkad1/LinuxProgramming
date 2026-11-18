#ifndef ANALYZER_H
#define ANALYZER_H
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

typedef struct { uint64_t key, tx_packets, tx_bytes, rx_packets, rx_bytes; bool used; } Entry;
typedef struct { Entry *slots; size_t capacity, count; } Map;
typedef struct {
    Map hosts, times;
    uint64_t packets, bytes, skipped, invalid;
} Stats;
typedef struct { uint32_t timestamp, source, destination; uint16_t bytes; } Record;
typedef struct {
    const char *input, *output, *mode;
    unsigned workers, interval, top;
    size_t buffer_size;
    int timezone_minutes;
    bool quiet;
} Options;
typedef struct {
    char *path;
    uint64_t size, bytes_read, records, invalid, skipped;
    int status;
    char error[320];
} Job;
typedef struct { Job *items; size_t count, capacity; } Jobs;
typedef struct {
    Jobs *jobs;
    size_t next;
    size_t completed, failed;
    atomic_uint_fast64_t progress_bytes;
    pthread_mutex_t mutex;
    bool fatal;
} Queue;
typedef struct {
    Queue *queue;
    pthread_t thread;
    pthread_cond_t condition;
    double started;
    uint64_t total_bytes;
    bool active, stop, terminal;
} Progress;
typedef struct { Queue *queue; const Options *options; Stats stats; } Worker;
typedef struct {
    int fd;
    unsigned char *buffer;
    size_t capacity, begin, end;
    uint64_t offset, bytes_read;
    atomic_uint_fast64_t *progress_bytes;
} Reader;

Entry *map_get(Map *, uint64_t);
void stats_free(Stats *);
int stats_add(Stats *, const Record *, unsigned);
int stats_merge(Stats *, const Stats *);
int scan_files(const char *, Jobs *);
void jobs_free(Jobs *);
int reader_exact(Reader *, void *, size_t);
int parse_record(const unsigned char *, const unsigned char *, Record *);
void *worker_run(void *);
int progress_start(Progress *, Queue *);
void progress_stop(Progress *);
double monotonic_seconds(void);
int output_create(const char *);
int report_write(const Options *, const Jobs *, Stats *);
int metrics_write(const Options *, const Jobs *, double, double);
#endif
