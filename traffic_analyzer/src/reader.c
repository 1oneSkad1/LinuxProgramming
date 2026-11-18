#include "analyzer.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

/* 1: complete, 0: clean EOF, -1: read error, -2: truncated record. */
int reader_exact(Reader *r, void *out, size_t n) {
    size_t copied=0;
    while (copied<n) {
        if (r->begin==r->end) {
            ssize_t got;
            do { got=read(r->fd,r->buffer,r->capacity); } while (got<0 && errno==EINTR);
            if (got<0) return -1;
            if (!got) return copied ? -2 : 0;
            r->bytes_read+=(uint64_t)got;
            if (r->progress_bytes)
                atomic_fetch_add_explicit(r->progress_bytes,(uint64_t)got,memory_order_relaxed);
            r->begin=0; r->end=(size_t)got;
        }
        size_t take=r->end-r->begin;
        if (take>n-copied) take=n-copied;
        memcpy((unsigned char *)out+copied,r->buffer+r->begin,take);
        r->begin+=take; copied+=take; r->offset+=take;
    }
    return 1;
}
