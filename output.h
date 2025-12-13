/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * output.h - Output formatting
 */

#ifndef MBDISCID_OUTPUT_H
#define MBDISCID_OUTPUT_H

#include "types.h"

/*
 * Print section header (for All mode only)
 */
void output_section_header(const char *name);

/*
 * Output functions for single-mode output (no headers)
 */

/* Type mode output */
void output_type(const disc_info_t *disc);

/* Text mode output (CD-Text) */
void output_text(const disc_info_t *disc);

/* MCN mode output */
void output_mcn(const disc_info_t *disc);

/* ISRC mode output */
void output_isrc(const disc_info_t *disc);

/* Raw TOC output */
void output_raw_toc(const toc_t *toc);

/* AccurateRip output */
void output_accuraterip_toc(const toc_t *toc);
void output_accuraterip_id(const char *id);

/* FreeDB output */
void output_freedb_toc(const toc_t *toc);
void output_freedb_id(const char *id);

/* MusicBrainz output */
void output_musicbrainz_toc(const toc_t *toc);
void output_musicbrainz_id(const char *id);
void output_musicbrainz_url(const char *url);

/*
 * All mode output (with headers and separators)
 */
void output_all(const disc_info_t *disc, const options_t *opts);

/*
 * Open URL in browser
 * Returns 0 on success, non-zero on failure
 */
int output_open_url(const char *url);

#endif /* MBDISCID_OUTPUT_H */
