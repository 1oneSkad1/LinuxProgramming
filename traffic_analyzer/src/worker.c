#include "analyzer.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(Job *j, uint64_t offset, const char *reason) {
    if (!j->error[0]) snprintf(j->error,sizeof(j->error),"offset=%llu: %s",(unsigned long long)offset,reason);
    j->status=-1;
}

static bool process(Job *j, Worker *w, unsigned char *buffer) {
    int fd=open(j->path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) { fail(j,0,strerror(errno)); return false; }
    struct stat before, after;
    if (fstat(fd,&before) || !S_ISREG(before.st_mode) || (uint64_t)before.st_size!=j->size) {
        fail(j,0,"input is no longer the expected regular file"); close(fd); return false;
    }
    Reader reader={.fd=fd,.buffer=buffer,.capacity=w->options->buffer_size,
        .progress_bytes=w->options->quiet?NULL:&w->queue->progress_bytes};
    unsigned char h[12],payload[UINT16_MAX];
    bool fatal=false;
    for (;;) {
        uint64_t start=reader.offset;
        int rc=reader_exact(&reader,h,sizeof(h));
        if (!rc) break;
        if (rc<0) { fail(j,start,rc==-1?strerror(errno):"truncated storage header"); break; }
        unsigned stored=(unsigned)h[10]|(unsigned)h[11]<<8;
        if (stored) {
            rc=reader_exact(&reader,payload,stored);
            if (rc!=1) { fail(j,start,rc==-1?strerror(errno):"truncated packet data"); break; }
        }
        j->records++;
        Record record;
        rc=parse_record(h,payload,&record);
        if (rc<0) {
            j->invalid++; w->stats.invalid++;
            fail(j,start,"malformed record (check LE header, lengths, timestamp and IPv4 header)");
            /* The uint16 length defines the next boundary; continue with a partial result. */
            continue;
        }
        if (!rc) { j->skipped++; w->stats.skipped++; continue; }
        if (stats_add(&w->stats,&record,w->options->interval)) {
            fail(j,start,"out of memory while aggregating; statistics are incomplete"); fatal=true; break;
        }
    }
    j->bytes_read=reader.bytes_read;
    if (fstat(fd,&after) || before.st_size!=after.st_size ||
        before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec)
        fail(j,reader.offset,"file changed during analysis");
    if (close(fd)) fail(j,reader.offset,"close failed");
    if (!j->status) j->status=1;
    return fatal;
}

void *worker_run(void *arg) {
    Worker *w=arg;
    unsigned char *buffer=malloc(w->options->buffer_size);
    if (!buffer) {
        pthread_mutex_lock(&w->queue->mutex); w->queue->fatal=true; pthread_mutex_unlock(&w->queue->mutex);
        return NULL;
    }
    for (;;) {
        pthread_mutex_lock(&w->queue->mutex);
        size_t at=w->queue->next;
        bool stop=w->queue->fatal || at>=w->queue->jobs->count;
        if (!stop) w->queue->next++;
        pthread_mutex_unlock(&w->queue->mutex);
        if (stop) break;
        bool fatal=process(&w->queue->jobs->items[at],w,buffer);
        pthread_mutex_lock(&w->queue->mutex);
        w->queue->completed++;
        if (w->queue->jobs->items[at].status!=1) w->queue->failed++;
        if (fatal) w->queue->fatal=true;
        pthread_mutex_unlock(&w->queue->mutex);
        if (fatal) break;
    }
    free(buffer); return NULL;
}
