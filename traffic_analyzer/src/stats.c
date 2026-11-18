#include "analyzer.h"
#include <stdlib.h>

static size_t hash_key(uint64_t x) {
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(x ^ (x >> 31));
}

static int grow(Map *m) {
    size_t capacity = m->capacity ? m->capacity * 2 : 1024;
    if (capacity < m->capacity || capacity > SIZE_MAX / sizeof(Entry)) return -1;
    Entry *slots = calloc(capacity, sizeof(*slots));
    if (!slots) return -1;
    for (size_t i = 0; i < m->capacity; i++) {
        if (!m->slots[i].used) continue;
        size_t at = hash_key(m->slots[i].key) & (capacity - 1);
        while (slots[at].used) at = (at + 1) & (capacity - 1);
        slots[at] = m->slots[i];
    }
    free(m->slots); m->slots = slots; m->capacity = capacity;
    return 0;
}

Entry *map_get(Map *m, uint64_t key) {
    /* Grow before returning a pointer: callers must not retain it across inserts. */
    if (!m->capacity || m->count >= m->capacity * 7 / 10)
        if (grow(m)) return NULL;
    size_t at = hash_key(key) & (m->capacity - 1);
    while (m->slots[at].used && m->slots[at].key != key)
        at = (at + 1) & (m->capacity - 1);
    Entry *e = &m->slots[at];
    if (!e->used) { e->used = true; e->key = key; m->count++; }
    return e;
}

int stats_add(Stats *s, const Record *r, unsigned interval) {
    Entry *e = map_get(&s->hosts, r->source);
    if (!e) return -1;
    e->tx_packets++; e->tx_bytes += r->bytes;
    e = map_get(&s->hosts, r->destination);
    if (!e) return -1;
    e->rx_packets++; e->rx_bytes += r->bytes;
    e = map_get(&s->times, (uint64_t)r->timestamp / interval * interval);
    if (!e) return -1;
    e->tx_packets++; e->tx_bytes += r->bytes;
    s->packets++; s->bytes += r->bytes;
    return 0;
}

static int merge_map(Map *dst, const Map *src) {
    for (size_t i = 0; i < src->capacity; i++) {
        const Entry *a = &src->slots[i];
        if (!a->used) continue;
        Entry *b = map_get(dst, a->key);
        if (!b) return -1;
        b->tx_packets += a->tx_packets; b->tx_bytes += a->tx_bytes;
        b->rx_packets += a->rx_packets; b->rx_bytes += a->rx_bytes;
    }
    return 0;
}

int stats_merge(Stats *dst, const Stats *src) {
    if (merge_map(&dst->hosts, &src->hosts) || merge_map(&dst->times, &src->times)) return -1;
    dst->packets += src->packets; dst->bytes += src->bytes;
    dst->invalid += src->invalid; dst->skipped += src->skipped;
    return 0;
}

void stats_free(Stats *s) { free(s->hosts.slots); free(s->times.slots); *s = (Stats){0}; }
