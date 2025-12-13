/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * cdtext.h - CD-Text parsing from READ TOC format 5 data
 *
 * CD-Text is stored in the lead-in area of a CD as 18-byte packs in the
 * Q-subchannel. This module parses the raw pack data returned by the
 * READ TOC/PMA/ATIP command (format code 5).
 *
 * Encoding support:
 *   - ISO-8859-1 (Latin-1) - most common, converted to UTF-8
 *   - ASCII (7-bit) - passed through as-is
 *   - Other encodings (MS-JIS, Korean, etc.) are NOT supported and will
 *     result in the affected text blocks being skipped with a warning.
 *
 * References:
 *   - Red Book (IEC 60908) - CD-Text specification
 *   - MMC-3 (SCSI Multimedia Commands) - READ TOC format 5
 */

#ifndef MBDISCID_CDTEXT_H
#define MBDISCID_CDTEXT_H

#include "types.h"
#include <stdint.h>
#include <stddef.h>

/*
 * CD-Text pack types (from Red Book)
 */
#define CDTEXT_PACK_TITLE       0x80    /* Album title (track 0) / Track title */
#define CDTEXT_PACK_PERFORMER   0x81    /* Album artist (track 0) / Track artist */
#define CDTEXT_PACK_SONGWRITER  0x82    /* Lyricist/songwriter */
#define CDTEXT_PACK_COMPOSER    0x83    /* Composer */
#define CDTEXT_PACK_ARRANGER    0x84    /* Arranger */
#define CDTEXT_PACK_MESSAGE     0x85    /* Message/comment */
#define CDTEXT_PACK_DISC_ID     0x86    /* Disc identification (not used) */
#define CDTEXT_PACK_GENRE       0x87    /* Genre identification */
#define CDTEXT_PACK_TOC_INFO    0x88    /* TOC information (not used) */
#define CDTEXT_PACK_TOC_INFO2   0x89    /* Second TOC (not used) */
#define CDTEXT_PACK_RESERVED_8A 0x8A    /* Reserved */
#define CDTEXT_PACK_RESERVED_8B 0x8B    /* Reserved */
#define CDTEXT_PACK_RESERVED_8C 0x8C    /* Reserved */
#define CDTEXT_PACK_CLOSED_INFO 0x8D    /* For internal use (not used) */
#define CDTEXT_PACK_UPC_ISRC    0x8E    /* UPC/EAN or ISRC (not used) */
#define CDTEXT_PACK_SIZE_INFO   0x8F    /* Size information block */

/*
 * CD-Text character set codes (from Size Information block)
 */
#define CDTEXT_CHARSET_ISO8859_1    0x00    /* ISO-8859-1 (Latin-1) */
#define CDTEXT_CHARSET_ASCII        0x01    /* 7-bit ASCII */
#define CDTEXT_CHARSET_MSJIS        0x80    /* MS-JIS (Japanese) */
#define CDTEXT_CHARSET_KOREAN       0x81    /* Korean */
#define CDTEXT_CHARSET_MANDARIN     0x82    /* Mandarin Chinese */

/*
 * CD-Text pack structure (18 bytes)
 */
typedef struct {
    uint8_t pack_type;      /* Pack type (0x80-0x8F) */
    uint8_t track_num;      /* Track number (0 = album, 1-99 = track) */
    uint8_t seq_num;        /* Sequence number within pack type */
    uint8_t char_pos;       /* Character position / block info */
    uint8_t text[12];       /* Text data */
    uint8_t crc[2];         /* CRC-16 CCITT (big-endian) */
} cdtext_pack_t;

/*
 * Parse raw CD-Text data into cdtext_t structure
 *
 * raw_data: Raw CD-Text data from READ TOC format 5 (after 4-byte header)
 * len: Length of raw data in bytes
 * cdtext: Output structure (will be initialized/cleared)
 * verbosity: Verbosity level for diagnostic messages
 *
 * Returns 0 on success (even if no text found), negative on error
 *
 * Notes:
 *   - Only block 0 (primary language) is parsed
 *   - Only ISO-8859-1 and ASCII encodings are supported
 *   - Invalid CRC packs are skipped
 *   - Text fields are allocated and must be freed with cdtext_free()
 */
int cdtext_parse(const uint8_t *raw_data, size_t len, cdtext_t *cdtext, int verbosity);

/*
 * Validate CRC-16 CCITT for a CD-Text pack
 *
 * pack: Pointer to 18-byte pack
 *
 * Returns true if CRC is valid
 */
bool cdtext_pack_crc_valid(const cdtext_pack_t *pack);

#endif /* MBDISCID_CDTEXT_H */
