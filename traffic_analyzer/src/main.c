#include "analyzer.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double monotonic_seconds(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); exit(2); }
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}

static void usage(FILE *f) {
    fprintf(f,"Usage: traffic_analyzer --input PATH --output NEW_DIR [options]\n"
        "  --mode sequential|threaded  (default threaded)\n"
        "  --workers N                1..128 (default 4)\n"
        "  --interval SECONDS         1..86400 (default 60)\n"
        "  --top K                    1..100000 (default 10)\n"
        "  --buffer-size BYTES        16..67108864 (default 1048576)\n"
        "  --timezone-minutes OFFSET  -720..840 (default 540, Asia/Seoul)\n"
        "  --quiet                    Disable progress and phase messages\n"
        "Directory input: only immediate pkt_YYYY.MM.DD_HH.MM files.\n"
        "Exit codes: 0 complete, 1 partial file errors, 2 usage/system/output error.\n");
}

static long number(const char *s,long min,long max) {
    char *end=NULL; errno=0;
    long value=strtol(s,&end,10);
    if (errno || !*s || *end || value<min || value>max) {
        fprintf(stderr,"Invalid numeric option: %s\n",s); exit(2);
    }
    return value;
}

int main(int argc,char **argv) {
    double start=monotonic_seconds();
    Options o={.mode="threaded",.workers=4,.interval=60,.top=10,.buffer_size=1048576,.timezone_minutes=540};
    static const struct option opts[]={
        {"input",1,NULL,'i'},{"output",1,NULL,'o'},{"mode",1,NULL,'m'},
        {"workers",1,NULL,'w'},{"interval",1,NULL,'s'},{"top",1,NULL,'k'},
        {"buffer-size",1,NULL,'b'},{"timezone-minutes",1,NULL,'z'},
        {"quiet",0,NULL,'q'},{"help",0,NULL,'h'},{NULL,0,NULL,0}};
    int c;
    while ((c=getopt_long(argc,argv,"",opts,NULL))!=-1) {
        switch(c) {
        case 'i':o.input=optarg;break; case 'o':o.output=optarg;break; case 'm':o.mode=optarg;break;
        case 'w':o.workers=(unsigned)number(optarg,1,128);break;
        case 's':o.interval=(unsigned)number(optarg,1,86400);break;
        case 'k':o.top=(unsigned)number(optarg,1,100000);break;
        case 'b':o.buffer_size=(size_t)number(optarg,16,67108864);break;
        case 'z':o.timezone_minutes=(int)number(optarg,-720,840);break;
        case 'q':o.quiet=true;break;
        case 'h':usage(stdout);return 0;
        default:usage(stderr);return 2;
        }
    }
    if (!o.input || !o.output || optind!=argc ||
        (strcmp(o.mode,"sequential") && strcmp(o.mode,"threaded"))) { usage(stderr); return 2; }
    if (!strcmp(o.mode,"sequential")) o.workers=1;
    Jobs jobs={0}; Stats total={0};
    if (!o.quiet) fprintf(stderr,"Scanning input files...\n");
    if (scan_files(o.input,&jobs)) { perror("scan input"); jobs_free(&jobs); return 2; }
    if (!jobs.count) { fprintf(stderr,"No matching packet files in input.\n"); jobs_free(&jobs); return 2; }
    if (output_create(o.output)) { perror("create NEW output directory"); jobs_free(&jobs); return 2; }
    Queue queue={.jobs=&jobs};
    atomic_init(&queue.progress_bytes,0);
    Progress progress={0};
    int err=pthread_mutex_init(&queue.mutex,NULL);
    if (err) { fprintf(stderr,"mutex: %s\n",strerror(err)); jobs_free(&jobs); return 2; }
    Worker *workers=calloc(o.workers,sizeof(*workers));
    pthread_t *threads=calloc(o.workers,sizeof(*threads));
    int result=2;
    if (!workers || !threads) { fprintf(stderr,"Out of memory.\n"); goto cleanup; }
    for (unsigned i=0;i<o.workers;i++) workers[i]=(Worker){.queue=&queue,.options=&o};
    if (!o.quiet) {
        err=progress_start(&progress,&queue);
        if (err) fprintf(stderr,"Cannot display periodic progress: %s\n",strerror(err));
    }
    double processing_start=monotonic_seconds();
    unsigned created=0;
    if (!strcmp(o.mode,"sequential")) worker_run(&workers[0]);
    else {
        for (;created<o.workers;created++) {
            err=pthread_create(&threads[created],NULL,worker_run,&workers[created]);
            if (err) {
                fprintf(stderr,"pthread_create: %s\n",strerror(err));
                pthread_mutex_lock(&queue.mutex);queue.fatal=true;pthread_mutex_unlock(&queue.mutex);break;
            }
        }
        for (unsigned i=0;i<created;i++) {
            err=pthread_join(threads[i],NULL);
            if (err) { fprintf(stderr,"pthread_join: %s\n",strerror(err)); exit(2); }
        }
    }
    double processing_seconds=monotonic_seconds()-processing_start;
    progress_stop(&progress);
    if (queue.fatal) { fprintf(stderr,"Fatal worker failure; no statistics published.\n"); goto cleanup; }
    if (!o.quiet) fprintf(stderr,"Merging statistics...\n");
    for (unsigned i=0;i<o.workers;i++) {
        if (stats_merge(&total,&workers[i].stats)) { fprintf(stderr,"Out of memory merging statistics.\n"); goto cleanup; }
        stats_free(&workers[i].stats);
    }
    if (!o.quiet) fprintf(stderr,"Saving CSV reports...\n");
    if (report_write(&o,&jobs,&total)) { perror("write report"); goto cleanup; }
    double elapsed=monotonic_seconds()-start;
    if (metrics_write(&o,&jobs,elapsed,processing_seconds)) { perror("write metrics"); goto cleanup; }
    result=0;
    for (size_t i=0;i<jobs.count;i++) if (jobs.items[i].status!=1) result=1;
    printf("status=%s files=%zu packets=%llu bytes=%llu hosts=%zu elapsed=%.6f seconds\n",
        result?"partial":"complete",jobs.count,(unsigned long long)total.packets,
        (unsigned long long)total.bytes,total.hosts.count,elapsed);
    printf("Results: %s\n",o.output);
cleanup:
    progress_stop(&progress);
    if (workers) for (unsigned i=0;i<o.workers;i++) stats_free(&workers[i].stats);
    free(workers);free(threads);stats_free(&total);jobs_free(&jobs);pthread_mutex_destroy(&queue.mutex);
    return result;
}
