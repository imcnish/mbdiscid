/*
 * mbdiscid - Disc ID calculator
 * toc.c - TOC structures and parsing
 */

#include "toc.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * Initialize TOC structure to safe defaults
 */
void toc_init(toc_t *toc)
{
    memset(toc, 0, sizeof(*toc));
    toc->first_track = 1;
    toc->last_session = 1;
}

/*
 * Parse whitespace-separated integers from string
 * Returns number of values parsed, stores in vals array
 */
static int parse_integers(const char *input, int32_t *vals, int max_vals)
{
    char *str = xstrdup(input);
    char *saveptr;
    char *token;
    int count = 0;

    token = strtok_r(str, " \t\n\r", &saveptr);
    while (token && count < max_vals) {
        /* Check for non-numeric input */
        char *endptr;
        long val = strtol(token, &endptr, 10);

        /* Check if entire token was consumed */
        if (*endptr != '\0') {
            free(str);
            return -1;  /* Non-numeric input */
        }

        vals[count++] = (int32_t)val;
        token = strtok_r(NULL, " \t\n\r", &saveptr);
    }

    free(str);
    return count;
}

/*
 * Parse CDTOC input according to specified format
 */
int toc_parse(toc_t *toc, const char *input, toc_format_t format)
{
    switch (format) {
    case TOC_FORMAT_MUSICBRAINZ:
        return toc_parse_musicbrainz(toc, input);
    case TOC_FORMAT_ACCURATERIP:
        return toc_parse_accuraterip(toc, input);
    case TOC_FORMAT_FREEDB:
        return toc_parse_freedb(toc, input);
    default:
        return -1;
    }
}

/*
 * Parse MusicBrainz format: first last leadout offset1...offsetN
 * All values are frame counts (LBA + 150 pregap)
 */
int toc_parse_musicbrainz(toc_t *toc, const char *input)
{
    int32_t vals[MAX_TRACKS + 4];
    int count = parse_integers(input, vals, MAX_TRACKS + 4);

    if (count < 0)
        return EX_DATAERR;  /* Non-numeric input */

    if (count < 4) {
        /* Need at least: first last leadout offset1 */
        return EX_DATAERR;
    }

    int first = vals[0];
    int last = vals[1];
    int32_t leadout = vals[2];
    int expected_offsets = last - first + 1;

    /* Validate track numbers */
    if (first < 1 || first > 99) {
        return EX_DATAERR;
    }
    if (last < first || last > 99) {
        return EX_DATAERR;
    }

    /* Check we have right number of offsets */
    if (count != 3 + expected_offsets) {
        return EX_DATAERR;
    }

    /* Check for negative values */
    for (int i = 0; i < count; i++) {
        if (vals[i] < 0)
            return EX_DATAERR;
    }

    toc_init(toc);
    toc->first_track = first;
    toc->last_track = last;
    toc->track_count = expected_offsets;
    toc->leadout = leadout - PREGAP_FRAMES;  /* Convert to raw LBA */

    /* Parse track offsets - convert from MusicBrainz format (with pregap) to raw LBA */
    for (int i = 0; i < expected_offsets; i++) {
        int track_num = first + i;
        int32_t offset = vals[3 + i] - PREGAP_FRAMES;  /* Convert to raw LBA */

        /* Check monotonically increasing */
        if (i > 0 && offset <= toc->tracks[i - 1].offset) {
            return EX_DATAERR;
        }

        toc->tracks[i].number = track_num;
        toc->tracks[i].session = 1;
        toc->tracks[i].offset = offset;
        toc->tracks[i].type = TRACK_TYPE_AUDIO;  /* Assume audio for MB format */
    }

    /* Leadout must be after last track */
    if (toc->leadout <= toc->tracks[expected_offsets - 1].offset) {
        return EX_DATAERR;
    }

    /* Calculate track lengths */
    for (int i = 0; i < expected_offsets - 1; i++) {
        toc->tracks[i].length = toc->tracks[i + 1].offset - toc->tracks[i].offset;
    }
    toc->tracks[expected_offsets - 1].length = toc->leadout - toc->tracks[expected_offsets - 1].offset;

    toc->audio_count = expected_offsets;
    toc->audio_leadout = toc->leadout;

    return 0;
}

/*
 * Parse AccurateRip format: count audio first offset1...offsetN leadout
 * All offsets are raw LBA (0-based)
 *
 * count = total tracks
 * audio = number of audio tracks
 * first = first AUDIO track number (track 1 may be data for Mixed Mode)
 * offsets = one for each track, starting from track 1
 */
int toc_parse_accuraterip(toc_t *toc, const char *input)
{
    int32_t vals[MAX_TRACKS + 5];
    int count = parse_integers(input, vals, MAX_TRACKS + 5);

    if (count < 0)
        return EX_DATAERR;  /* Non-numeric input */

    if (count < 5) {
        /* Need at least: count audio first offset1 leadout */
        return EX_DATAERR;
    }

    int track_count = vals[0];
    int audio_count = vals[1];
    int first_audio = vals[2];

    /* Validate */
    if (track_count < 1 || track_count > 99)
        return EX_DATAERR;
    if (audio_count < 0 || audio_count > track_count)
        return EX_DATAERR;
    if (first_audio < 1 || first_audio > 99)
        return EX_DATAERR;

    /* Check for negative values */
    for (int i = 0; i < count; i++) {
        if (vals[i] < 0)
            return EX_DATAERR;
    }

    /* We should have: count audio first + track_count offsets + leadout */
    int expected_count = 3 + track_count + 1;
    if (count != expected_count)
        return EX_DATAERR;

    int32_t leadout = vals[3 + track_count];

    toc_init(toc);
    toc->first_track = 1;  /* Always start at track 1 */
    toc->last_track = track_count;
    toc->track_count = track_count;
    toc->audio_count = audio_count;
    toc->data_count = track_count - audio_count;
    toc->leadout = leadout;

    /* Parse track offsets - track numbers are always 1, 2, 3, ... */
    for (int i = 0; i < track_count; i++) {
        int32_t offset = vals[3 + i];
        int track_num = i + 1;  /* Track numbers start at 1 */

        /* Check monotonically increasing */
        if (i > 0 && offset <= toc->tracks[i - 1].offset)
            return EX_DATAERR;

        toc->tracks[i].number = track_num;
        toc->tracks[i].session = 1;
        toc->tracks[i].offset = offset;

        /* Determine track type based on first_audio:
         * - Mixed Mode (first_audio > 1): tracks before first_audio are data
         * - Enhanced (first_audio == 1): tracks after audio_count are data
         * - Standard (audio_count == track_count): all audio
         */
        if (audio_count == track_count) {
            toc->tracks[i].type = TRACK_TYPE_AUDIO;
        } else if (first_audio > 1) {
            /* Mixed mode: tracks 1 to first_audio-1 are data */
            toc->tracks[i].type = (track_num < first_audio) ? TRACK_TYPE_DATA : TRACK_TYPE_AUDIO;
        } else {
            /* Enhanced: tracks 1 to audio_count are audio, rest are data */
            toc->tracks[i].type = (track_num <= audio_count) ? TRACK_TYPE_AUDIO : TRACK_TYPE_DATA;
        }
    }

    /* Leadout must be after last track */
    if (leadout <= toc->tracks[track_count - 1].offset)
        return EX_DATAERR;

    /* Calculate track lengths */
    for (int i = 0; i < track_count - 1; i++) {
        toc->tracks[i].length = toc->tracks[i + 1].offset - toc->tracks[i].offset;
    }
    toc->tracks[track_count - 1].length = leadout - toc->tracks[track_count - 1].offset;

    /* For AccurateRip ID calculation, audio_leadout is always the disc leadout.
     * The "audio session leadout" concept only applies to actual disc reading,
     * not to ID calculation from TOC data. */
    toc->audio_leadout = leadout;

    return 0;
}

/*
 * Parse FreeDB format: count offset1...offsetN total_seconds
 * Offsets include +150 pregap adjustment
 */
int toc_parse_freedb(toc_t *toc, const char *input)
{
    int32_t vals[MAX_TRACKS + 3];
    int count = parse_integers(input, vals, MAX_TRACKS + 3);

    if (count < 0)
        return EX_DATAERR;  /* Non-numeric input */

    if (count < 3) {
        /* Need at least: count offset1 total_seconds */
        return EX_DATAERR;
    }

    int track_count = vals[0];

    if (track_count < 1 || track_count > 99)
        return EX_DATAERR;

    /* Check for negative values */
    for (int i = 0; i < count; i++) {
        if (vals[i] < 0)
            return EX_DATAERR;
    }

    /* We should have: count + track_count offsets + total_seconds */
    if (count != 1 + track_count + 1)
        return EX_DATAERR;

    int32_t total_seconds = vals[1 + track_count];

    toc_init(toc);
    toc->first_track = 1;
    toc->last_track = track_count;
    toc->track_count = track_count;
    toc->audio_count = track_count;  /* FreeDB doesn't distinguish */

    /* Parse track offsets (input includes +150, convert to raw LBA) */
    for (int i = 0; i < track_count; i++) {
        int32_t offset = vals[1 + i] - PREGAP_FRAMES;  /* Convert to raw LBA */

        /* Check monotonically increasing */
        if (i > 0 && offset <= toc->tracks[i - 1].offset)
            return EX_DATAERR;

        toc->tracks[i].number = i + 1;
        toc->tracks[i].session = 1;
        toc->tracks[i].offset = offset;
        toc->tracks[i].type = TRACK_TYPE_AUDIO;
    }

    /* Calculate leadout from total_seconds (also convert to raw LBA) */
    /* total_seconds = (leadout_with_pregap) / 75 */
    /* So leadout_raw = total_seconds * 75 - 150 */
    toc->leadout = total_seconds * FRAMES_PER_SECOND - PREGAP_FRAMES;
    toc->audio_leadout = toc->leadout;

    /* Calculate track lengths */
    for (int i = 0; i < track_count - 1; i++) {
        toc->tracks[i].length = toc->tracks[i + 1].offset - toc->tracks[i].offset;
    }
    toc->tracks[track_count - 1].length = toc->leadout - toc->tracks[track_count - 1].offset;

    return 0;
}

/*
 * Validate TOC consistency
 */
int toc_validate(const toc_t *toc)
{
    if (toc->first_track < 1 || toc->first_track > 99)
        return EX_DATAERR;
    if (toc->last_track < toc->first_track || toc->last_track > 99)
        return EX_DATAERR;
    if (toc->track_count < 1)
        return EX_DATAERR;
    if (toc->leadout <= 0)
        return EX_DATAERR;

    /* Check tracks are in order and leadout is after last track */
    for (int i = 0; i < toc->track_count; i++) {
        if (i > 0 && toc->tracks[i].offset <= toc->tracks[i - 1].offset)
            return EX_DATAERR;
    }

    if (toc->leadout <= toc->tracks[toc->track_count - 1].offset)
        return EX_DATAERR;

    return 0;
}

/*
 * Determine disc type from TOC
 */
disc_type_t toc_get_disc_type(const toc_t *toc)
{
    if (toc->data_count == 0)
        return DISC_TYPE_AUDIO;

    /* Check if data track is first */
    if (toc->first_track == 1 && toc->tracks[0].type == TRACK_TYPE_DATA)
        return DISC_TYPE_MIXED;

    /* Check if data track is last (in tracks array) */
    if (toc->tracks[toc->track_count - 1].type == TRACK_TYPE_DATA)
        return DISC_TYPE_ENHANCED;

    /* Check for Enhanced CD where data track is beyond libdiscid's range:
     * data_count > 0 but all tracks in array are audio */
    bool all_audio_in_array = true;
    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_DATA) {
            all_audio_in_array = false;
            break;
        }
    }
    if (all_audio_in_array && toc->data_count > 0) {
        /* Data track exists but not in tracks array = at end = Enhanced CD */
        return DISC_TYPE_ENHANCED;
    }

    /* Unknown layout */
    return DISC_TYPE_UNKNOWN;
}

/*
 * Format TOC as raw string: first last offset1...offsetN leadout
 */
char *toc_format_raw(const toc_t *toc)
{
    size_t bufsize = 16 * (toc->track_count + 3);
    char *buf = xmalloc(bufsize);
    char *p = buf;
    int remaining = (int)bufsize;
    int written;

    written = snprintf(p, remaining, "%d %d", toc->first_track, toc->last_track);
    p += written;
    remaining -= written;

    for (int i = 0; i < toc->track_count; i++) {
        /* Raw format uses offsets with pregap (+150) */
        written = snprintf(p, remaining, " %d", toc->tracks[i].offset + PREGAP_FRAMES);
        p += written;
        remaining -= written;
    }

    snprintf(p, remaining, " %d", toc->leadout + PREGAP_FRAMES);

    return buf;
}

/*
 * Format TOC as MusicBrainz string: first last leadout offset1...offsetN
 *
 * MusicBrainz TOC includes AUDIO TRACKS ONLY:
 * - For Enhanced CDs: exclude data track at end, use audio session leadout
 * - For Mixed Mode: exclude data track at start, use first audio track number
 * - For standard Audio CDs: include all tracks
 */
char *toc_format_musicbrainz(const toc_t *toc)
{
    size_t bufsize = 16 * (toc->track_count + 4);
    char *buf = xmalloc(bufsize);
    char *p = buf;
    int remaining = (int)bufsize;
    int written;

    int first_track = toc->first_track;
    int last_track = toc->last_track;
    int32_t leadout;

    /* Determine disc type - same logic as calc_musicbrainz_id() */
    int last_audio = toc_get_last_audio_track(toc);

    /* Check if this is an Enhanced CD (trailing data track) */
    bool is_enhanced_cd = (last_audio > 0 && last_audio < toc->last_track);

    if (is_enhanced_cd) {
        /* Enhanced CD: exclude trailing data track(s), use audio_leadout */
        last_track = last_audio;
        leadout = toc->audio_leadout;
    } else {
        /* Mixed Mode or standard: include all tracks, use disc leadout */
        leadout = toc->leadout;
    }

    /* MusicBrainz format includes +150 pregap in all values */
    written = snprintf(p, remaining, "%d %d %d",
                       first_track, last_track, leadout + PREGAP_FRAMES);
    p += written;
    remaining -= written;

    /* Output track offsets for tracks in range */
    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].number >= first_track &&
            toc->tracks[i].number <= last_track) {
            written = snprintf(p, remaining, " %d", toc->tracks[i].offset + PREGAP_FRAMES);
            p += written;
            remaining -= written;
        }
    }

    return buf;
}

/*
 * Format TOC as AccurateRip string: count audio first offset1...offsetN leadout
 */
char *toc_format_accuraterip(const toc_t *toc)
{
    size_t bufsize = 16 * (toc->track_count + 5);
    char *buf = xmalloc(bufsize);
    char *p = buf;
    int remaining = (int)bufsize;
    int written;

    /* Determine first audio track */
    int first_audio = toc_get_first_audio_track(toc);
    if (first_audio == 0)
        first_audio = toc->first_track;

    written = snprintf(p, remaining, "%d %d %d",
                       toc->track_count, toc->audio_count, first_audio);
    p += written;
    remaining -= written;

    /* Raw LBA offsets (no pregap adjustment) */
    for (int i = 0; i < toc->track_count; i++) {
        written = snprintf(p, remaining, " %d", toc->tracks[i].offset);
        p += written;
        remaining -= written;
    }

    snprintf(p, remaining, " %d", toc->leadout);

    return buf;
}

/*
 * Format TOC as FreeDB string: count offset1...offsetN total_seconds
 */
char *toc_format_freedb(const toc_t *toc)
{
    size_t bufsize = 16 * (toc->track_count + 3);
    char *buf = xmalloc(bufsize);
    char *p = buf;
    int remaining = (int)bufsize;
    int written;

    written = snprintf(p, remaining, "%d", toc->track_count);
    p += written;
    remaining -= written;

    /* FreeDB uses offsets with +150 pregap */
    for (int i = 0; i < toc->track_count; i++) {
        written = snprintf(p, remaining, " %d", toc->tracks[i].offset + PREGAP_FRAMES);
        p += written;
        remaining -= written;
    }

    /* Total seconds */
    int32_t total_seconds = (toc->leadout + PREGAP_FRAMES) / FRAMES_PER_SECOND;
    snprintf(p, remaining, " %d", total_seconds);

    return buf;
}

/*
 * Get the audio leadout for AccurateRip calculations
 */
int32_t toc_get_audio_leadout(const toc_t *toc)
{
    return toc->audio_leadout;
}

/*
 * Get first audio track number
 */
int toc_get_first_audio_track(const toc_t *toc)
{
    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO)
            return toc->tracks[i].number;
    }
    return 0;
}

/*
 * Get last audio track number
 */
int toc_get_last_audio_track(const toc_t *toc)
{
    for (int i = toc->track_count - 1; i >= 0; i--) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO)
            return toc->tracks[i].number;
    }
    return 0;
}
