#include "analyzer.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void display(const Progress *progress, bool final) {
    Queue *queue=progress->queue;
    pthread_mutex_lock(&queue->mutex);
    size_t completed=queue->completed;
    size_t failed=queue->failed;
    pthread_mutex_unlock(&queue->mutex);
    uint64_t bytes=atomic_load_explicit(&queue->progress_bytes,memory_order_relaxed);
    double percent=100.0*(double)completed/(double)queue->jobs->count;
    fprintf(stderr,"%s[progress] files %zu/%zu (%.1f%%) | read %.1f/%.1f MiB | failed %zu | elapsed %.1fs%s",
        progress->terminal?"\r\033[2K":"",completed,queue->jobs->count,percent,
        (double)bytes/1048576.0,(double)progress->total_bytes/1048576.0,failed,
        monotonic_seconds()-progress->started,
        final || !progress->terminal?"\n":"");
    fflush(stderr);
}

static void *monitor(void *arg) {
    Progress *progress=arg;
    Queue *queue=progress->queue;
    pthread_mutex_lock(&queue->mutex);
    while (!progress->stop) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME,&deadline);
        deadline.tv_sec++;
        int error=0;
        while (!progress->stop && !error)
            error=pthread_cond_timedwait(&progress->condition,&queue->mutex,&deadline);
        if (progress->stop) break;
        if (error!=ETIMEDOUT) {
            fprintf(stderr,"Progress update failed: %s\n",strerror(error));
            break;
        }
        pthread_mutex_unlock(&queue->mutex);
        display(progress,false);
        pthread_mutex_lock(&queue->mutex);
    }
    pthread_mutex_unlock(&queue->mutex);
    return NULL;
}

int progress_start(Progress *progress, Queue *queue) {
    progress->queue=queue;
    progress->started=monotonic_seconds();
    progress->terminal=isatty(STDERR_FILENO)!=0;
    for (size_t i=0;i<queue->jobs->count;i++) progress->total_bytes+=queue->jobs->items[i].size;
    int error=pthread_cond_init(&progress->condition,NULL);
    if (error) return error;
    display(progress,false);
    error=pthread_create(&progress->thread,NULL,monitor,progress);
    if (error) {
        if (progress->terminal) fputc('\n',stderr);
        pthread_cond_destroy(&progress->condition);
        return error;
    }
    progress->active=true;
    return 0;
}

void progress_stop(Progress *progress) {
    if (!progress->active) return;
    pthread_mutex_lock(&progress->queue->mutex);
    progress->stop=true;
    pthread_cond_signal(&progress->condition);
    pthread_mutex_unlock(&progress->queue->mutex);
    int error=pthread_join(progress->thread,NULL);
    if (error) {
        fprintf(stderr,"Progress thread join failed: %s\n",strerror(error));
        exit(2);
    }
    display(progress,true);
    pthread_cond_destroy(&progress->condition);
    progress->active=false;
}
