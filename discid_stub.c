/*
 * libdiscid stub implementation for testing
 * Implements discid_put and ID calculation, but not device reading
 */

#include "discid/discid.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* SHA-1 implementation for MusicBrainz ID calculation */
#include <stdint.h>

/* SHA-1 context */
typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

static void SHA1Init(SHA1_CTX *ctx);
static void SHA1Update(SHA1_CTX *ctx, const uint8_t *data, size_t len);
static void SHA1Final(uint8_t digest[20], SHA1_CTX *ctx);

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void SHA1Transform(uint32_t state[5], const uint8_t buffer[64])
{
    uint32_t a, b, c, d, e, w[80];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i*4] << 24) |
               ((uint32_t)buffer[i*4+1] << 16) |
               ((uint32_t)buffer[i*4+2] << 8) |
               ((uint32_t)buffer[i*4+3]);
    }
    for (i = 16; i < 80; i++) {
        w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

    for (i = 0; i < 20; i++) {
        uint32_t t = ROL(a, 5) + ((b & c) | (~b & d)) + e + w[i] + 0x5A827999;
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    for (i = 20; i < 40; i++) {
        uint32_t t = ROL(a, 5) + (b ^ c ^ d) + e + w[i] + 0x6ED9EBA1;
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    for (i = 40; i < 60; i++) {
        uint32_t t = ROL(a, 5) + ((b & c) | (b & d) | (c & d)) + e + w[i] + 0x8F1BBCDC;
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    for (i = 60; i < 80; i++) {
        uint32_t t = ROL(a, 5) + (b ^ c ^ d) + e + w[i] + 0xCA62C1D6;
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void SHA1Init(SHA1_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

static void SHA1Update(SHA1_CTX *ctx, const uint8_t *data, size_t len)
{
    size_t i, j;
    j = (ctx->count[0] >> 3) & 63;
    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < (len << 3)) ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    if (j + len > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        SHA1Transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64) {
            SHA1Transform(ctx->state, &data[i]);
        }
        j = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void SHA1Final(uint8_t digest[20], SHA1_CTX *ctx)
{
    uint8_t finalcount[8];
    uint8_t c = 0x80;
    int i;

    for (i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4) ? 0 : 1] >>
            ((3 - (i & 3)) * 8)) & 255);
    }
    SHA1Update(ctx, &c, 1);
    while ((ctx->count[0] & 504) != 448) {
        c = 0;
        SHA1Update(ctx, &c, 1);
    }
    SHA1Update(ctx, finalcount, 8);
    for (i = 0; i < 20; i++) {
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

/* DiscId structure */
struct DiscId {
    int first_track;
    int last_track;
    int offsets[101];  /* offsets[0] = leadout, offsets[1..100] = track offsets */
    char id[29];       /* MusicBrainz ID (28 chars + null) */
    char freedb_id[9]; /* FreeDB ID (8 chars + null) */
    char error[256];
    char mcn[14];
    char isrc[100][13];
};

/* Base64 alphabet for MusicBrainz IDs */
static const char MB_BASE64[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";

/* Convert 3 bytes to 4 base64 characters */
static void base64_encode(const uint8_t *in, char *out, int len)
{
    int i, j;
    for (i = 0, j = 0; i < len; i += 3, j += 4) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= (uint32_t)in[i + 2];
        
        out[j] = MB_BASE64[(v >> 18) & 0x3F];
        out[j + 1] = MB_BASE64[(v >> 12) & 0x3F];
        out[j + 2] = (i + 1 < len) ? MB_BASE64[(v >> 6) & 0x3F] : '-';
        out[j + 3] = (i + 2 < len) ? MB_BASE64[v & 0x3F] : '-';
    }
}

/* Calculate digit sum for FreeDB */
static int digit_sum(int n)
{
    int sum = 0;
    while (n > 0) {
        sum += n % 10;
        n /= 10;
    }
    return sum;
}

/* Calculate FreeDB ID */
static void calc_freedb_id(DiscId *d)
{
    int n = 0;
    int track_count = d->last_track - d->first_track + 1;
    
    for (int i = d->first_track; i <= d->last_track; i++) {
        int offset_seconds = d->offsets[i] / 75;
        n += digit_sum(offset_seconds + 2);
    }
    
    int t = (d->offsets[0] - d->offsets[d->first_track]) / 75;
    uint32_t id = ((n % 255) << 24) | (t << 8) | track_count;
    
    snprintf(d->freedb_id, sizeof(d->freedb_id), "%08x", id);
}

/* Calculate MusicBrainz ID */
static void calc_mb_id(DiscId *d)
{
    SHA1_CTX ctx;
    uint8_t digest[20];
    /* Format: 2 hex for first + 2 hex for last + 8 hex for leadout + 99*8 hex for tracks + null */
    /* = 4 + 8 + 792 + 1 = 805 bytes */
    char data[1024];
    int len;
    
    /* Format: first track, last track, leadout, 99 track offsets */
    len = snprintf(data, sizeof(data), "%02X%02X", d->first_track, d->last_track);
    
    /* Leadout */
    len += snprintf(data + len, sizeof(data) - len, "%08X", d->offsets[0]);
    
    /* Track offsets (99 slots, padded with zeros) */
    for (int i = 1; i <= 99; i++) {
        if (i >= d->first_track && i <= d->last_track) {
            len += snprintf(data + len, sizeof(data) - len, "%08X", d->offsets[i]);
        } else {
            len += snprintf(data + len, sizeof(data) - len, "%08X", 0);
        }
    }
    
    SHA1Init(&ctx);
    SHA1Update(&ctx, (uint8_t *)data, len);
    SHA1Final(digest, &ctx);
    
    /* Encode as base64 (20 bytes -> 28 chars with padding replacement) */
    base64_encode(digest, d->id, 20);
    d->id[28] = '\0';
    
    /* Replace padding - MusicBrainz uses - instead of = */
    for (int i = 0; i < 28; i++) {
        if (d->id[i] == '=') d->id[i] = '-';
    }
}

DiscId *discid_new(void)
{
    DiscId *d = calloc(1, sizeof(DiscId));
    return d;
}

void discid_free(DiscId *d)
{
    free(d);
}

int discid_read(DiscId *d, const char *device)
{
    (void)device;
    snprintf(d->error, sizeof(d->error), "device reading not supported in stub");
    return 0;
}

int discid_read_sparse(DiscId *d, const char *device, unsigned int features)
{
    (void)device;
    (void)features;
    snprintf(d->error, sizeof(d->error), "device reading not supported in stub");
    return 0;
}

int discid_put(DiscId *d, int first, int last, int *offsets)
{
    if (first < 1 || first > 99 || last < first || last > 99) {
        snprintf(d->error, sizeof(d->error), "invalid track numbers");
        return 0;
    }
    
    d->first_track = first;
    d->last_track = last;
    
    /* offsets[0] = leadout, offsets[1..n] = track offsets */
    d->offsets[0] = offsets[0];
    for (int i = first; i <= last; i++) {
        d->offsets[i] = offsets[i - first + 1];
    }
    
    calc_freedb_id(d);
    calc_mb_id(d);
    
    return 1;
}

const char *discid_get_id(DiscId *d)
{
    return d->id;
}

const char *discid_get_freedb_id(DiscId *d)
{
    return d->freedb_id;
}

const char *discid_get_submission_url(DiscId *d)
{
    (void)d;
    return NULL;
}

const char *discid_get_default_device(void)
{
#ifdef PLATFORM_MACOS
    return "/dev/rdisk1";
#else
    return "/dev/cdrom";
#endif
}

const char *discid_get_error_msg(DiscId *d)
{
    return d->error;
}

const char *discid_get_version_string(void)
{
    return "libdiscid-stub 1.0";
}

const char *discid_get_mcn(DiscId *d)
{
    return d->mcn[0] ? d->mcn : NULL;
}

const char *discid_get_track_isrc(DiscId *d, int track)
{
    if (track < 1 || track > 99) return NULL;
    return d->isrc[track][0] ? d->isrc[track] : NULL;
}

int discid_get_first_track_num(DiscId *d)
{
    return d->first_track;
}

int discid_get_last_track_num(DiscId *d)
{
    return d->last_track;
}

int discid_get_sectors(DiscId *d)
{
    return d->offsets[0];
}

int discid_get_track_offset(DiscId *d, int track)
{
    if (track < 1 || track > 99) return 0;
    return d->offsets[track];
}

int discid_get_track_length(DiscId *d, int track)
{
    if (track < d->first_track || track > d->last_track) return 0;
    if (track == d->last_track) {
        return d->offsets[0] - d->offsets[track];
    }
    return d->offsets[track + 1] - d->offsets[track];
}
