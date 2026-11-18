#include "analyzer.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <time.h>

int output_create(const char *path) {
    char *copy=strdup(path);
    if (!copy) return -1;
    size_t n=strlen(copy);
    while (n>1 && copy[n-1]=='/') copy[--n]=0;
    for (char *p=copy+1;*p;p++) if (*p=='/') {
        *p=0;
        if (mkdir(copy,0775) && errno!=EEXIST) { free(copy);return -1; }
        *p='/';
    }
    int rc=mkdir(copy,0775);free(copy);return rc;
}

static FILE *create(const Options *o,const char *name) {
    size_t len=strlen(o->output)+strlen(name)+2;
    char *p=malloc(len);if(!p)return NULL;
    snprintf(p,len,"%s/%s",o->output,name);FILE *f=fopen(p,"wx");free(p);return f;
}
static int done(FILE *f) { int failed=ferror(f);if(fclose(f))failed=1;return failed?-1:0; }
static void csv(FILE *f,const char *s) {
    fputc('"',f);for(;*s;s++){if(*s=='"')fputc('"',f);fputc(*s,f);}fputc('"',f);
}
static int by_key(const void *a,const void *b) {
    uint64_t x=((const Entry *)a)->key,y=((const Entry *)b)->key;return (x>y)-(x<y);
}
static int by_traffic(const void *a,const void *b) {
    const Entry *x=a,*y=b;uint64_t tx=x->tx_bytes+x->rx_bytes,ty=y->tx_bytes+y->rx_bytes;
    return tx==ty?by_key(a,b):(tx>ty?-1:1);
}
static Entry *entries(const Map *m) {
    Entry *out=malloc((m->count?m->count:1)*sizeof(*out));if(!out)return NULL;
    size_t n=0;for(size_t i=0;i<m->capacity;i++)if(m->slots[i].used)out[n++]=m->slots[i];
    qsort(out,n,sizeof(*out),by_key);return out;
}
static void host_row(FILE *f,const Entry *e) {
    uint32_t ip=(uint32_t)e->key;
    fprintf(f,"%u.%u.%u.%u,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"\n",
        ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,e->tx_packets,e->tx_bytes,e->rx_packets,e->rx_bytes,e->tx_bytes+e->rx_bytes);
}

int report_write(const Options *o,const Jobs *jobs,Stats *s) {
    size_t success=0;uint64_t records=0,read_bytes=0;
    for(size_t i=0;i<jobs->count;i++){success+=jobs->items[i].status==1;records+=jobs->items[i].records;read_bytes+=jobs->items[i].bytes_read;}
    FILE *f=create(o,"summary.csv");if(!f)return -1;
    fprintf(f,"status,files,successful_files,failed_files,records,packets,bytes,unique_hosts,skipped_records,invalid_records,bytes_read\n");
    fprintf(f,"%s,%zu,%zu,%zu,%"PRIu64",%"PRIu64",%"PRIu64",%zu,%"PRIu64",%"PRIu64",%"PRIu64"\n",
        success==jobs->count?"complete":"partial",jobs->count,success,jobs->count-success,records,s->packets,s->bytes,s->hosts.count,s->skipped,s->invalid,read_bytes);
    if(done(f))return -1;
    Entry *hosts=entries(&s->hosts);if(!hosts)return -1;
    f=create(o,"hosts.csv");if(!f){free(hosts);return -1;}
    const char *header="ip,tx_packets,tx_bytes,rx_packets,rx_bytes,total_bytes\n";
    fputs(header,f);for(size_t i=0;i<s->hosts.count;i++)host_row(f,&hosts[i]);
    if(done(f)){free(hosts);return -1;}
    qsort(hosts,s->hosts.count,sizeof(*hosts),by_traffic);
    f=create(o,"top_hosts.csv");if(!f){free(hosts);return -1;}
    fputs(header,f);for(size_t i=0;i<s->hosts.count && i<o->top;i++)host_row(f,&hosts[i]);
    free(hosts);if(done(f))return -1;
    Entry *times=entries(&s->times);if(!times)return -1;
    f=create(o,"timeseries.csv");if(!f){free(times);return -1;}
    fputs("epoch_start,local_time,utc_offset_minutes,packets,bytes\n",f);
    for(size_t i=0;i<s->times.count;i++) {
        time_t t=(time_t)((int64_t)times[i].key+(int64_t)o->timezone_minutes*60);
        struct tm tm;char stamp[40];
        if(!gmtime_r(&t,&tm)){free(times);fclose(f);return -1;}
        strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S",&tm);
        fprintf(f,"%"PRIu64",%s,%d,%"PRIu64",%"PRIu64"\n",times[i].key,stamp,o->timezone_minutes,times[i].tx_packets,times[i].tx_bytes);
    }
    free(times);if(done(f))return -1;
    f=create(o,"errors.log");if(!f)return -1;
    for(size_t i=0;i<jobs->count;i++)if(jobs->items[i].status!=1) {
        csv(f,jobs->items[i].path);fputc(',',f);csv(f,jobs->items[i].error);fputc('\n',f);
    }
    return done(f);
}

int metrics_write(const Options *o,const Jobs *jobs,double elapsed,double processing) {
    uint64_t size=0,read_bytes=0,records=0;bool complete=true;
    for(size_t i=0;i<jobs->count;i++) {
        const Job *j=&jobs->items[i];size+=j->size;read_bytes+=j->bytes_read;
        records+=j->records-j->invalid-j->skipped;if(j->status!=1)complete=false;
    }
    struct rusage usage={0};getrusage(RUSAGE_SELF,&usage);
    /* getrusage's high-water value may retain a larger launcher image across
     * exec on Linux. VmHWM belongs to this executable's memory map. */
    long peak_rss=usage.ru_maxrss;
    FILE *status=fopen("/proc/self/status","r");
    if(status) {
        char line[256];long value;
        while(fgets(line,sizeof(line),status))
            if(sscanf(line,"VmHWM: %ld kB",&value)==1){peak_rss=value;break;}
        fclose(status);
    }
    FILE *f=create(o,"run_metrics.csv");if(!f)return -1;
    fputs("status,mode,workers,interval_seconds,top_k,buffer_bytes,timezone_minutes,files,input_bytes,bytes_read,elapsed_seconds,processing_seconds,packets_per_second,input_mib_per_second,max_rss_kib,getrusage_max_rss_kib,input,format,byte_metric\n",f);
    fprintf(f,"%s,%s,%u,%u,%u,%zu,%d,%zu,%"PRIu64",%"PRIu64",%.9f,%.9f,%.3f,%.3f,%ld,%ld,",
        complete?"complete":"partial",o->mode,o->workers,o->interval,o->top,o->buffer_size,o->timezone_minutes,
        jobs->count,size,read_bytes,elapsed,processing,elapsed>0?records/elapsed:0,
        elapsed>0?read_bytes/1048576.0/elapsed:0,peak_rss,usage.ru_maxrss);
    csv(f,o->input);fputs(",pkt_le12_ipv4,pkt_len_original\n",f);return done(f);
}
