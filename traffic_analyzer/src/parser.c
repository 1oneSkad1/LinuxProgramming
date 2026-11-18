#include "analyzer.h"

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }

/* Custom 12-byte LE storage header, followed directly by an IPv4 header.
 * Return 1 for IPv4, 0 for an unstored/non-IPv4 record, -1 if malformed.
 * Fragments are valid for host aggregation; no transport header is required. */
int parse_record(const unsigned char *h, const unsigned char *p, Record *r) {
    uint16_t real=le16(h+8), stored=le16(h+10);
    if (le32(h+4)>=1000000 || stored>real) return -1;
    if (!stored) return 0;
    if ((p[0]>>4)!=4) return 0;
    if (stored<20) return -1;
    unsigned ihl=(p[0]&15)*4;
    unsigned total=(unsigned)p[2]<<8 | p[3];
    if (ihl<20 || ihl>stored || total<ihl || total>real) return -1;
    *r=(Record){.timestamp=le32(h),.source=be32(p+12),.destination=be32(p+16),.bytes=real};
    return 1;
}
