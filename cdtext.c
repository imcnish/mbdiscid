/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * cdtext.c - CD-Text parsing from READ TOC format 5 data
 *
 * CD-Text is stored as 18-byte packs in the lead-in Q-subchannel.
 * This module parses the raw pack data and extracts text fields.
 *
 * Pack structure (18 bytes):
 *   Byte 0:     Pack type (0x80-0x8F)
 *   Byte 1:     Track number (0 = album, 1-99 = track)
 *   Byte 2:     Sequence number
 *   Byte 3:     Character position (bits 0-3) / Block number (bits 4-6) / DBCS flag (bit 7)
 *   Bytes 4-15: Text data (12 bytes)
 *   Bytes 16-17: CRC-16 CCITT
 *
 * Text strings span multiple packs, null-terminated within the 12-byte payload.
 * Track numbers cycle: track 0, track 1, track 2, ... then back to track 0 for
 * the next pack type.
 *
 * Encoding support:
 *   - ISO-8859-1: Converted to UTF-8 (may expand bytes)
 *   - ASCII: Passed through as-is
 *   - Other encodings: Skipped with warning
 */

#include "cdtext.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* CD-Text constants */
#define CDTEXT_PACK_SIZE        18
#define CDTEXT_TEXT_SIZE        12
#define CDTEXT_MAX_BLOCKS       8
#define CDTEXT_MAX_TRACKS       99

/* Size info block structure (pack type 0x8F) */
#define SIZE_INFO_PACKS         3       /* 3 packs of size info per block */

/*
 * CRC-16 CCITT for CD-Text validation
 * Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
 * Initial value: 0x0000 for CD-Text (NOT 0xFFFF like Q-subchannel)
 * Note: CD-Text CRC is inverted before storage
 */
static uint16_t crc16_cdtext(const uint8_t *data, int len)
{
    uint16_t crc = 0;

    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }

    return crc;
}

/*
 * Validate CRC for a CD-Text pack
 * The stored CRC is the inverted CRC of bytes 0-15
 */
bool cdtext_pack_crc_valid(const cdtext_pack_t *pack)
{
    uint16_t calc_crc = crc16_cdtext((const uint8_t *)pack, 16);
    calc_crc = ~calc_crc;  /* CD-Text stores inverted CRC */

    uint16_t stored_crc = ((uint16_t)pack->crc[0] << 8) | pack->crc[1];

    return calc_crc == stored_crc;
}

/*
 * Convert ISO-8859-1 string to UTF-8
 * Returns allocated string (caller must free)
 */
static char *iso8859_1_to_utf8(const uint8_t *src, size_t len)
{
    /* Worst case: each byte becomes 2 UTF-8 bytes */
    char *utf8 = xmalloc(len * 2 + 1);
    char *dst = utf8;

    for (size_t i = 0; i < len && src[i] != '\0'; i++) {
        uint8_t c = src[i];

        if (c < 0x80) {
            /* ASCII: pass through */
            *dst++ = (char)c;
        } else {
            /* ISO-8859-1 0x80-0xFF -> UTF-8 two-byte sequence */
            *dst++ = (char)(0xC0 | (c >> 6));
            *dst++ = (char)(0x80 | (c & 0x3F));
        }
    }

    *dst = '\0';
    return utf8;
}

/*
 * Normalize CD-Text string per spec §6.2.2:
 * - Strip trailing null padding
 * - Strip \r or mixed CRLF artifacts
 * - Trim leading and trailing whitespace
 * - Convert control characters (ASCII < 0x20) to spaces except \n
 */
static void normalize_cdtext_string(char *str)
{
    if (!str || !*str)
        return;

    /* Convert control characters to spaces (except \n) */
    for (char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 && c != '\n') {
            *p = ' ';
        }
    }

    /* Strip \r characters */
    char *dst = str;
    for (char *src = str; *src; src++) {
        if (*src != '\r') {
            *dst++ = *src;
        }
    }
    *dst = '\0';

    /* Trim leading whitespace */
    char *start = str;
    while (*start && isspace((unsigned char)*start))
        start++;

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    /* Trim trailing whitespace */
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

/*
 * Text accumulator for building strings across multiple packs
 */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t capacity;
} text_accum_t;

static void accum_init(text_accum_t *acc)
{
    acc->data = NULL;
    acc->len = 0;
    acc->capacity = 0;
}

static void accum_free(text_accum_t *acc)
{
    free(acc->data);
    acc->data = NULL;
    acc->len = 0;
    acc->capacity = 0;
}

static void accum_append(text_accum_t *acc, const uint8_t *data, size_t len)
{
    if (len == 0)
        return;

    if (acc->len + len + 1 > acc->capacity) {
        acc->capacity = (acc->len + len + 1) * 2;
        if (acc->capacity < 64)
            acc->capacity = 64;
        acc->data = xrealloc(acc->data, acc->capacity);
    }

    memcpy(acc->data + acc->len, data, len);
    acc->len += len;
    acc->data[acc->len] = '\0';
}

static char *accum_finish(text_accum_t *acc, uint8_t charset)
{
    if (!acc->data || acc->len == 0) {
        accum_free(acc);
        return NULL;
    }

    char *result;
    if (charset == CDTEXT_CHARSET_ISO8859_1) {
        result = iso8859_1_to_utf8(acc->data, acc->len);
    } else {
        /* ASCII or fallback: direct copy */
        result = xmalloc(acc->len + 1);
        memcpy(result, acc->data, acc->len);
        result[acc->len] = '\0';
    }

    accum_free(acc);
    normalize_cdtext_string(result);

    /* Return NULL if empty after normalization */
    if (!result[0]) {
        free(result);
        return NULL;
    }

    return result;
}

/*
 * Per-track text accumulator state
 */
typedef struct {
    text_accum_t title;
    text_accum_t performer;
    text_accum_t songwriter;
    text_accum_t composer;
    text_accum_t arranger;
    text_accum_t message;
} track_accum_t;

/*
 * Block parsing state
 */
typedef struct {
    track_accum_t tracks[CDTEXT_MAX_TRACKS + 1];  /* Index 0 = album */
    text_accum_t genre;
    uint8_t charset;
    int first_track;
    int last_track;
    bool size_info_found;
} block_state_t;

static void block_state_init(block_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->charset = CDTEXT_CHARSET_ISO8859_1;  /* Default */
    state->first_track = 1;
    state->last_track = 99;

    for (int i = 0; i <= CDTEXT_MAX_TRACKS; i++) {
        accum_init(&state->tracks[i].title);
        accum_init(&state->tracks[i].performer);
        accum_init(&state->tracks[i].songwriter);
        accum_init(&state->tracks[i].composer);
        accum_init(&state->tracks[i].arranger);
        accum_init(&state->tracks[i].message);
    }
    accum_init(&state->genre);
}

static void block_state_free(block_state_t *state)
{
    for (int i = 0; i <= CDTEXT_MAX_TRACKS; i++) {
        accum_free(&state->tracks[i].title);
        accum_free(&state->tracks[i].performer);
        accum_free(&state->tracks[i].songwriter);
        accum_free(&state->tracks[i].composer);
        accum_free(&state->tracks[i].arranger);
        accum_free(&state->tracks[i].message);
    }
    accum_free(&state->genre);
}

/*
 * Get the accumulator for a specific pack type and track
 */
static text_accum_t *get_track_accum(block_state_t *state, uint8_t pack_type, int track)
{
    if (track < 0 || track > CDTEXT_MAX_TRACKS)
        return NULL;

    switch (pack_type) {
    case CDTEXT_PACK_TITLE:
        return &state->tracks[track].title;
    case CDTEXT_PACK_PERFORMER:
        return &state->tracks[track].performer;
    case CDTEXT_PACK_SONGWRITER:
        return &state->tracks[track].songwriter;
    case CDTEXT_PACK_COMPOSER:
        return &state->tracks[track].composer;
    case CDTEXT_PACK_ARRANGER:
        return &state->tracks[track].arranger;
    case CDTEXT_PACK_MESSAGE:
        return &state->tracks[track].message;
    case CDTEXT_PACK_GENRE:
        if (track == 0)
            return &state->genre;
        return NULL;  /* Genre is album-only */
    default:
        return NULL;
    }
}

/*
 * Process a text pack's payload
 * Text is null-terminated; track number advances on each null
 */
static void process_text_pack(block_state_t *state, const cdtext_pack_t *pack,
                               int *current_track, int verbosity)
{
    (void)verbosity;  /* Reserved for future diagnostic use */

    uint8_t pack_type = pack->pack_type;
    int track = *current_track;

    for (int i = 0; i < CDTEXT_TEXT_SIZE; i++) {
        uint8_t c = pack->text[i];

        if (c == '\0') {
            /* Null terminator: move to next track */
            track++;
            if (track > state->last_track) {
                /* Wrap or stop */
                break;
            }
            continue;
        }

        /* Append character to appropriate accumulator */
        text_accum_t *acc = get_track_accum(state, pack_type, track);
        if (acc) {
            accum_append(acc, &c, 1);
        }
    }

    *current_track = track;
}

/*
 * Parse size information block (pack type 0x8F)
 * Contains character set, track range, and pack counts
 */
static void parse_size_info(block_state_t *state, const uint8_t *raw_data,
                            size_t pack_count, int verbosity)
{
    /* Find size info packs for block 0 */
    for (size_t i = 0; i < pack_count; i++) {
        const cdtext_pack_t *pack = (const cdtext_pack_t *)(raw_data + i * CDTEXT_PACK_SIZE);

        if (pack->pack_type != CDTEXT_PACK_SIZE_INFO)
            continue;

        /* Check block number (bits 4-6 of char_pos) */
        int block = (pack->char_pos >> 4) & 0x07;
        if (block != 0)
            continue;

        /* Validate CRC */
        if (!cdtext_pack_crc_valid(pack)) {
            verbose(2, verbosity, "cdtext: size info pack %zu CRC invalid, skipping", i);
            continue;
        }

        /* Size info spans 3 packs (seq 0, 1, 2) */
        int seq = pack->seq_num;
        if (seq == 0) {
            /* First size info pack:
             * text[0] = character set code
             * text[1] = first track number
             * text[2] = last track number
             */
            state->charset = pack->text[0];
            state->first_track = pack->text[1];
            state->last_track = pack->text[2];
            state->size_info_found = true;

            verbose(2, verbosity, "cdtext: block 0 charset=%d tracks=%d-%d",
                    state->charset, state->first_track, state->last_track);
        }
    }
}

/*
 * Parse raw CD-Text data into cdtext_t structure
 */
int cdtext_parse(const uint8_t *raw_data, size_t len, cdtext_t *cdtext, int verbosity)
{
    /* Initialize output structure */
    memset(cdtext, 0, sizeof(*cdtext));

    if (!raw_data || len == 0) {
        verbose(2, verbosity, "cdtext: no data");
        return 0;
    }

    /* Calculate number of packs */
    size_t pack_count = len / CDTEXT_PACK_SIZE;
    if (pack_count == 0) {
        verbose(2, verbosity, "cdtext: data too short (%zu bytes)", len);
        return 0;
    }

    verbose(1, verbosity, "cdtext: parsing %zu packs (%zu bytes)", pack_count, len);

    /* Initialize block state */
    block_state_t state;
    block_state_init(&state);

    /* First pass: find size info to get charset and track range */
    parse_size_info(&state, raw_data, pack_count, verbosity);

    /* Check encoding support */
    if (state.charset != CDTEXT_CHARSET_ISO8859_1 &&
        state.charset != CDTEXT_CHARSET_ASCII) {
        verbose(1, verbosity, "cdtext: unsupported charset %d (only ISO-8859-1/ASCII supported)",
                state.charset);
        block_state_free(&state);
        return 0;
    }

    /* Track current track number for each pack type */
    int current_track[16] = {0};  /* Indexed by pack_type & 0x0F */

    /* Second pass: parse text packs */
    int valid_packs = 0;
    int invalid_packs = 0;

    for (size_t i = 0; i < pack_count; i++) {
        const cdtext_pack_t *pack = (const cdtext_pack_t *)(raw_data + i * CDTEXT_PACK_SIZE);

        /* Only process block 0 */
        int block = (pack->char_pos >> 4) & 0x07;
        if (block != 0)
            continue;

        /* Skip non-text packs */
        if (pack->pack_type < CDTEXT_PACK_TITLE || pack->pack_type > CDTEXT_PACK_GENRE)
            continue;

        /* Validate CRC */
        if (!cdtext_pack_crc_valid(pack)) {
            invalid_packs++;
            verbose(3, verbosity, "cdtext: pack %zu type 0x%02x CRC invalid",
                    i, pack->pack_type);
            continue;
        }

        valid_packs++;

        /* Get pack type index (0x80-0x87 -> 0-7) */
        int type_idx = pack->pack_type & 0x0F;

        /* Handle sequence number 0: reset track counter */
        if (pack->seq_num == 0) {
            current_track[type_idx] = pack->track_num;
        }

        /* Process text data */
        process_text_pack(&state, pack, &current_track[type_idx], verbosity);
    }

    verbose(1, verbosity, "cdtext: %d valid packs, %d invalid", valid_packs, invalid_packs);

    /* Build output structure */
    cdtext->track_count = state.last_track;

    /* Album fields (track 0) */
    cdtext->album.album = accum_finish(&state.tracks[0].title, state.charset);
    cdtext->album.albumartist = accum_finish(&state.tracks[0].performer, state.charset);
    cdtext->album.lyricist = accum_finish(&state.tracks[0].songwriter, state.charset);
    cdtext->album.composer = accum_finish(&state.tracks[0].composer, state.charset);
    cdtext->album.arranger = accum_finish(&state.tracks[0].arranger, state.charset);
    cdtext->album.comment = accum_finish(&state.tracks[0].message, state.charset);
    cdtext->album.genre = accum_finish(&state.genre, state.charset);

    /* Track fields */
    for (int t = 1; t <= state.last_track && t <= MAX_TRACKS; t++) {
        int idx = t - 1;
        cdtext->tracks[idx].title = accum_finish(&state.tracks[t].title, state.charset);
        cdtext->tracks[idx].artist = accum_finish(&state.tracks[t].performer, state.charset);
        cdtext->tracks[idx].lyricist = accum_finish(&state.tracks[t].songwriter, state.charset);
        cdtext->tracks[idx].composer = accum_finish(&state.tracks[t].composer, state.charset);
        cdtext->tracks[idx].arranger = accum_finish(&state.tracks[t].arranger, state.charset);
        cdtext->tracks[idx].comment = accum_finish(&state.tracks[t].message, state.charset);
    }

    /* Clean up any remaining accumulators */
    block_state_free(&state);

    return 0;
}
