/*
 * mbdiscid - Disc ID calculator
 * toc.h - TOC structures and parsing
 */

#ifndef MBDISCID_TOC_H
#define MBDISCID_TOC_H

#include "types.h"

/*
 * Initialize a TOC structure
 */
void toc_init(toc_t *toc);

/*
 * Parse CDTOC input string according to format
 * Returns 0 on success, non-zero on error
 */
int toc_parse(toc_t *toc, const char *input, toc_format_t format);

/*
 * Parse MusicBrainz format: first last leadout offset1...offsetN
 */
int toc_parse_musicbrainz(toc_t *toc, const char *input);

/*
 * Parse AccurateRip format: count audio first offset1...offsetN leadout
 */
int toc_parse_accuraterip(toc_t *toc, const char *input);

/*
 * Parse FreeDB format: count offset1...offsetN total_seconds
 */
int toc_parse_freedb(toc_t *toc, const char *input);

/*
 * Validate TOC consistency
 * Returns 0 on success, non-zero on error
 */
int toc_validate(const toc_t *toc);

/*
 * Determine disc type from TOC
 */
disc_type_t toc_get_disc_type(const toc_t *toc);

/*
 * Format TOC as string for output
 */

/* Raw format: first last offset1...offsetN leadout */
char *toc_format_raw(const toc_t *toc);

/* MusicBrainz format: first last leadout offset1...offsetN */
char *toc_format_musicbrainz(const toc_t *toc);

/* AccurateRip format: count audio first offset1...offsetN leadout */
char *toc_format_accuraterip(const toc_t *toc);

/* FreeDB format: count offset1...offsetN total_seconds */
char *toc_format_freedb(const toc_t *toc);

/*
 * Get the audio leadout for AccurateRip calculations
 * For standard CDs: returns disc leadout
 * For Enhanced CDs: returns start of data track (end of audio session)
 */
int32_t toc_get_audio_leadout(const toc_t *toc);

/*
 * Get first audio track number
 */
int toc_get_first_audio_track(const toc_t *toc);

/*
 * Get last audio track number
 */
int toc_get_last_audio_track(const toc_t *toc);

#endif /* MBDISCID_TOC_H */
