/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * discid.h - Disc ID calculations
 */

#ifndef MBDISCID_DISCID_H
#define MBDISCID_DISCID_H

#include "types.h"

/*
 * Calculate AccurateRip Disc ID
 * Format: NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX
 *   NNN = audio track count (3 digits, zero-padded)
 *   Field 1 = sum of audio track LBAs + audio leadout
 *   Field 2 = sum of (LBA × track_number) for audio + (audio_leadout × (last_audio+1))
 *   Field 3 = FreeDB disc ID
 *
 * Returns allocated string, caller must free
 */
char *calc_accuraterip_id(const toc_t *toc);

/*
 * Calculate FreeDB/CDDB Disc ID
 * 8 hex digits
 *
 * Returns allocated string, caller must free
 */
char *calc_freedb_id(const toc_t *toc);

/*
 * Calculate MusicBrainz Disc ID using libdiscid
 * 28-character base64-like string
 *
 * Returns allocated string or NULL on error, caller must free
 */
char *calc_musicbrainz_id(const toc_t *toc);

/*
 * Get MusicBrainz submission URL
 * Returns allocated string, caller must free
 */
char *get_musicbrainz_url(const char *disc_id);

/*
 * Get libdiscid version string
 */
const char *get_libdiscid_version(void);

#endif /* MBDISCID_DISCID_H */
