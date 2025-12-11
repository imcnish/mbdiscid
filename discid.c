/*
 * mbdiscid - Disc ID calculator
 * discid.c - Disc ID calculations
 */

#include "discid.h"
#include "toc.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <discid/discid.h>

/*
 * Calculate sum of decimal digits
 */
static int digit_sum(int n)
{
    int sum = 0;
    while (n > 0) {
        sum += n % 10;
        n /= 10;
    }
    return sum;
}

/*
 * Calculate FreeDB/CDDB disc ID
 *
 * Algorithm:
 * - For each track, compute digit sum of offset_seconds (where offset includes +150 pregap)
 * - Sum all digit sums to get 'n'
 * - Total length in seconds 't' = (leadout - first_track_offset) / 75, rounded
 * - ID = ((n % 255) << 24) | (t << 8) | track_count
 *
 * Note: Internal TOC stores raw LBA, we add pregap for FreeDB calculation
 */
char *calc_freedb_id(const toc_t *toc)
{
    char *id = xmalloc(FREEDB_ID_LENGTH + 1);

    /* Sum of digit sums for each track */
    int n = 0;
    for (int i = 0; i < toc->track_count; i++) {
        /* Convert raw LBA to FreeDB offset (with +150 pregap) */
        int offset_frames = toc->tracks[i].offset + PREGAP_FRAMES;
        int offset_seconds = offset_frames / FRAMES_PER_SECOND;
        n += digit_sum(offset_seconds);
    }

    /* Total disc length in seconds
     * CDDB spec: t = floor(leadout_sec) - floor(first_track_sec)
     * NOT: t = floor((leadout - first) / 75)
     * These differ due to independent truncation */
    int leadout_frames = toc->leadout + PREGAP_FRAMES;
    int first_offset_frames = toc->tracks[0].offset + PREGAP_FRAMES;
    int t = (leadout_frames / FRAMES_PER_SECOND) - (first_offset_frames / FRAMES_PER_SECOND);

    /* Compute disc ID */
    uint32_t disc_id = ((n % 255) << 24) | (t << 8) | toc->track_count;

    snprintf(id, FREEDB_ID_LENGTH + 1, "%08x", disc_id);
    return id;
}

/*
 * Calculate AccurateRip disc ID
 *
 * Format: NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX
 *
 * - NNN = audio track count (3 digits)
 * - Disc ID 1 = sum of all audio track LBAs + DISC leadout
 * - Disc ID 2 = sum of (LBA × track_index) for audio tracks + (DISC leadout × (audio_count+1))
 * - Disc ID 3 = FreeDB disc ID (uses ALL tracks, not just audio)
 *
 * Note: AccurateRip uses the DISC leadout for ID calculation, even for Enhanced CDs.
 * This differs from MusicBrainz which uses the audio session leadout.
 */
char *calc_accuraterip_id(const toc_t *toc)
{
    char *id = xmalloc(AR_ID_LENGTH + 1);

    /* Calculate Disc ID 1: sum of audio track offsets + disc leadout */
    uint32_t disc_id1 = 0;
    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO) {
            disc_id1 += (uint32_t)toc->tracks[i].offset;
        }
    }
    disc_id1 += (uint32_t)toc->leadout;

    /* Calculate Disc ID 2: sum of (max(offset,1) × audio_track_index) + (leadout × (audio_count + 1))
     * Uses RELATIVE audio track indices (1, 2, 3...) not absolute track numbers.
     * The max(offset, 1) ensures first audio track at LBA 0 contributes at least 1 */
    uint32_t disc_id2 = 0;
    int audio_index = 1;  /* Relative index for audio tracks */
    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO) {
            int32_t offset = toc->tracks[i].offset;
            if (offset < 1) offset = 1;  /* min contribution of 1 for disc_id2 */
            disc_id2 += (uint32_t)offset * (uint32_t)audio_index;
            audio_index++;
        }
    }
    disc_id2 += (uint32_t)toc->leadout * (uint32_t)(toc->audio_count + 1);

    /* Get FreeDB ID (uses ALL tracks) */
    char *freedb_id = calc_freedb_id(toc);

    snprintf(id, AR_ID_LENGTH + 1, "%03d-%08x-%08x-%s",
             toc->audio_count, disc_id1, disc_id2, freedb_id);

    free(freedb_id);
    return id;
}

/*
 * Calculate MusicBrainz disc ID using libdiscid
 *
 * Per MusicBrainz documentation (https://musicbrainz.org/doc/Disc_ID_Calculation):
 *
 * Mixed Mode CDs (data track FIRST at position 1):
 *   - Include ALL tracks including the data track
 *   - Use disc leadout
 *   - Example: Sarah McLachlan "The Freedom Sessions" - tracks 1-9 where track 1 is data
 *
 * Enhanced CDs / CD-Extra (data track LAST / trailing):
 *   - Exclude trailing data track(s)
 *   - Use audio session leadout (data_track_offset - 11400, or session 1 leadout)
 *   - Per libdiscid API: "For discs with additional data tracks, the trailing
 *     data tracks should be ignored"
 *
 * Standard Audio CDs:
 *   - Include all tracks
 *   - Use disc leadout
 */
char *calc_musicbrainz_id(const toc_t *toc)
{
    DiscId *disc = discid_new();
    if (!disc)
        return NULL;

    /* Build offsets array for libdiscid
     * libdiscid wants: offsets[0] = leadout, offsets[track_num] = track offset */
    int offsets[MAX_TRACKS + 1];
    memset(offsets, 0, sizeof(offsets));

    int first_track = toc->first_track;
    int last_track = toc->last_track;
    int32_t leadout;

    /* Determine disc type and adjust accordingly */
    int first_audio = toc_get_first_audio_track(toc);
    int last_audio = toc_get_last_audio_track(toc);

    if (first_audio == 0) {
        /* No audio tracks - can't calculate MusicBrainz ID */
        discid_free(disc);
        return NULL;
    }

    /* Check if this is an Enhanced CD (trailing data track) */
    bool is_enhanced_cd = (last_audio < toc->last_track);

    if (is_enhanced_cd) {
        /* Enhanced CD: exclude trailing data track(s), use audio_leadout */
        last_track = last_audio;
        leadout = toc->audio_leadout;
    } else {
        /* Mixed Mode or standard: include all tracks, use disc leadout */
        leadout = toc->leadout;
    }

    /* Leadout in frames (with pregap) */
    offsets[0] = leadout + PREGAP_FRAMES;

    /* Collect track offsets for tracks in range */
    for (int i = 0; i < toc->track_count; i++) {
        int track_num = toc->tracks[i].number;
        if (track_num >= first_track && track_num <= last_track) {
            offsets[track_num] = toc->tracks[i].offset + PREGAP_FRAMES;
        }
    }

    int result = discid_put(disc, first_track, last_track, offsets);

    if (!result) {
        discid_free(disc);
        return NULL;
    }

    char *id = xstrdup(discid_get_id(disc));
    discid_free(disc);

    return id;
}

/*
 * Get MusicBrainz submission URL
 */
char *get_musicbrainz_url(const char *disc_id)
{
    if (!disc_id)
        return NULL;

    size_t len = strlen("https://musicbrainz.org/cdtoc/") + strlen(disc_id) + 1;
    char *url = xmalloc(len);
    snprintf(url, len, "https://musicbrainz.org/cdtoc/%s", disc_id);
    return url;
}

/*
 * Get libdiscid version string
 */
const char *get_libdiscid_version(void)
{
    return discid_get_version_string();
}
