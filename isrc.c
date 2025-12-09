/*
 * mbdiscid - Disc ID calculator
 * isrc.c - ISRC acquisition per spec §5
 *
 * Implements raw subchannel reading with:
 * - Tranche-based sampling at specific LBA positions
 * - CRC validation per frame
 * - Majority voting with strong majority rule
 * - Probe strategy for early termination
 */

#include "isrc.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Configuration per spec §5 */
#define PROBE_COUNT          3       /* Number of probe tracks */
#define MIN_TRACKS_FOR_PROBE 5       /* Use probing only if >= 5 audio tracks */
#define MAX_CANDIDATES       8       /* Max different ISRCs to track per track */
#define INITIAL_TRANCHES     3       /* Tranches per track */
#define RESCUE_TRANCHES      1       /* Additional tranche if indeterminate */
#define FRAMES_PER_TRANCHE   192     /* Frames per tranche */

/* Bookend exclusion: avoid first and last N frames of track */
#define BOOKEND_FRAMES       (2 * 75) /* ~2 seconds at 75 fps */

/* Short track threshold - full scan if track can't fit bookends + non-overlapping tranches */
/* Tranches are positioned at step intervals where step = usable_length / (num_tranches + 1) */
/* For last tranche to fit: usable_length >= (num_tranches + 1) * FRAMES_PER_TRANCHE */
/* Must account for rescue tranches too */
#define SHORT_TRACK_THRESHOLD ((2 * BOOKEND_FRAMES) + ((INITIAL_TRANCHES + RESCUE_TRANCHES + 1) * FRAMES_PER_TRANCHE))

/* Early termination threshold per §5.3.1 */
#define EARLY_STOP_VALID_FRAMES 64

/* ISRC candidate with vote count */
typedef struct {
    char isrc[13];
    int count;
} isrc_candidate_t;

/* Per-track sample collector */
typedef struct {
    isrc_candidate_t candidates[MAX_CANDIDATES];
    int num_candidates;
    int total_valid;      /* Total valid CRC frames */
    int total_read;       /* Total frames read (including invalid) */
} isrc_collector_t;

/*
 * Validate ISRC format per spec §5.1.3
 */
bool isrc_validate(const char *isrc)
{
    if (!isrc || strlen(isrc) != 12) {
        return false;
    }

    /* Check for all zeros */
    bool all_zero = true;
    for (int i = 0; i < 12; i++) {
        if (isrc[i] != '0') {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        return false;
    }

    /* Country code: 2 uppercase letters */
    if (!isupper((unsigned char)isrc[0]) || !isupper((unsigned char)isrc[1])) {
        return false;
    }

    /* Registrant: 3 alphanumeric */
    for (int i = 2; i < 5; i++) {
        if (!isalnum((unsigned char)isrc[i])) {
            return false;
        }
    }

    /* Year: 2 digits */
    if (!isdigit((unsigned char)isrc[5]) || !isdigit((unsigned char)isrc[6])) {
        return false;
    }

    /* Designation: 5 digits */
    for (int i = 7; i < 12; i++) {
        if (!isdigit((unsigned char)isrc[i])) {
            return false;
        }
    }

    return true;
}

/*
 * Check if track is "short" per spec §5.2.3
 */
static bool is_short_track(const track_t *track)
{
    return track->length < SHORT_TRACK_THRESHOLD;
}

/*
 * Add valid ISRC sample to collector
 */
static void collector_add(isrc_collector_t *c, const char *isrc)
{
    if (!isrc_validate(isrc)) {
        return;
    }

    c->total_valid++;

    /* Look for existing candidate */
    for (int i = 0; i < c->num_candidates; i++) {
        if (strcmp(c->candidates[i].isrc, isrc) == 0) {
            c->candidates[i].count++;
            return;
        }
    }

    /* Add new candidate */
    if (c->num_candidates < MAX_CANDIDATES) {
        strncpy(c->candidates[c->num_candidates].isrc, isrc, 12);
        c->candidates[c->num_candidates].isrc[12] = '\0';
        c->candidates[c->num_candidates].count = 1;
        c->num_candidates++;
    }
}

/*
 * Check strong majority rule per spec §5.4.1
 * Returns winning ISRC or NULL if no strong majority
 *
 * Strong majority: count(v_max) >= 2 * max(count(other))
 */
static const char *collector_get_majority(isrc_collector_t *c)
{
    if (c->num_candidates == 0) {
        return NULL;
    }

    /* Find candidate with highest count */
    int max_idx = 0;
    int max_count = c->candidates[0].count;

    for (int i = 1; i < c->num_candidates; i++) {
        if (c->candidates[i].count > max_count) {
            max_count = c->candidates[i].count;
            max_idx = i;
        }
    }

    /* Find highest count among other candidates */
    int second_max = 0;
    for (int i = 0; i < c->num_candidates; i++) {
        if (i != max_idx && c->candidates[i].count > second_max) {
            second_max = c->candidates[i].count;
        }
    }

    /* Strong majority: count(v_max) >= 2 * max(count(other)) */
    /* Also require at least 2 votes for the winner */
    if (max_count >= 2 && (second_max == 0 || max_count >= 2 * second_max)) {
        return c->candidates[max_idx].isrc;
    }

    return NULL;
}

/*
 * Select probe tracks per spec §5.2.2
 * Returns number of probe tracks selected (stored in probe_indices)
 */
static int select_probe_tracks(const toc_t *toc, int *probe_indices, int verbosity)
{
    /* Build list of eligible audio tracks (non-short) */
    int eligible[MAX_TRACKS];
    int num_eligible = 0;

    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO && !is_short_track(&toc->tracks[i])) {
            eligible[num_eligible++] = i;
        }
    }

    verbose(2, verbosity, "ISRC probe: %d eligible tracks out of %d audio",
            num_eligible, toc->audio_count);

    if (num_eligible < PROBE_COUNT) {
        /* Not enough eligible tracks - scan all audio tracks */
        return 0;
    }

    /* Select at 33%, 50%, 67% positions */
    /* Avoid first and last if possible */
    int positions[3];
    positions[0] = num_eligible / 3;          /* ~33% */
    positions[1] = num_eligible / 2;          /* 50% */
    positions[2] = (num_eligible * 2) / 3;    /* ~67% */

    /* Adjust to avoid first track (index 0) */
    if (positions[0] == 0 && num_eligible > 3) {
        positions[0] = 1;
    }

    /* Adjust to avoid last track */
    if (positions[2] == num_eligible - 1 && num_eligible > 3) {
        positions[2] = num_eligible - 2;
    }

    /* Ensure distinct positions */
    if (positions[1] == positions[0]) positions[1]++;
    if (positions[2] == positions[1]) positions[2]++;
    if (positions[2] >= num_eligible) positions[2] = num_eligible - 1;

    /* Convert to track indices */
    for (int i = 0; i < PROBE_COUNT; i++) {
        probe_indices[i] = eligible[positions[i]];
    }

    verbose(2, verbosity, "ISRC probe tracks: %d, %d, %d",
            toc->tracks[probe_indices[0]].number,
            toc->tracks[probe_indices[1]].number,
            toc->tracks[probe_indices[2]].number);

    return PROBE_COUNT;
}

/*
 * Calculate LBA positions for tranches within a track
 * Implements bookend exclusion per spec
 */
static void calculate_tranche_positions(const track_t *track, int num_tranches,
                                        int32_t *positions, int *frames_per_tranche)
{
    int32_t track_start = track->offset;
    int32_t track_length = track->length;

    /* Apply bookend exclusion */
    int32_t usable_start = track_start + BOOKEND_FRAMES;
    int32_t usable_end = track_start + track_length - BOOKEND_FRAMES;

    /* Handle short tracks - use full track if bookends don't fit */
    if (usable_end <= usable_start) {
        usable_start = track_start;
        usable_end = track_start + track_length;
    }

    int32_t usable_length = usable_end - usable_start;

    /* Distribute tranches evenly across usable region */
    if (num_tranches == 1) {
        positions[0] = usable_start + usable_length / 2;
    } else {
        int32_t step = usable_length / (num_tranches + 1);
        for (int i = 0; i < num_tranches; i++) {
            positions[i] = usable_start + step * (i + 1);
        }
    }

    *frames_per_tranche = FRAMES_PER_TRANCHE;
}

/*
 * Read ISRC for a single track using raw subchannel reading
 * Implements tranche-based sampling per spec §5.3
 * Returns true if valid ISRC found
 */
static bool read_track_isrc(scsi_device_t *dev, track_t *track, int verbosity)
{
    isrc_collector_t collector = {0};
    int crc_valid_count = 0;
    int crc_invalid_count = 0;
    int adr_counts[4] = {0};  /* Count ADR values 0-3 */
    int read_errors = 0;

    /* Short tracks: scan every frame (no tranches) */
    if (track->length < SHORT_TRACK_THRESHOLD) {
        verbose(2, verbosity, "Track %d: short track (%d frames), full scan",
                track->number, track->length);

        /* Allocate buffer for batch reading */
        q_subchannel_t *batch = malloc(track->length * sizeof(q_subchannel_t));
        if (!batch) {
            track->isrc[0] = '\0';
            return false;
        }

        /* Read entire short track in one batch */
        int read_count = scsi_read_q_subchannel_batch(dev, track->offset, track->length, batch);

        if (read_count > 0) {
            for (int i = 0; i < read_count; i++) {
                collector.total_read++;
                q_subchannel_t *q = &batch[i];

                if (q->crc_valid) {
                    crc_valid_count++;
                    if (q->adr < 4) adr_counts[q->adr]++;
                } else {
                    crc_invalid_count++;
                }
                if (q->crc_valid && q->has_isrc) {
                    collector_add(&collector, q->isrc);
                }
            }
        } else {
            read_errors = track->length;
            collector.total_read = track->length;
        }

        /* Check for majority winner */
        const char *winner = collector_get_majority(&collector);
        if (winner) {
            strncpy(track->isrc, winner, 12);
            track->isrc[12] = '\0';
            verbose(2, verbosity, "Track %d: ISRC %s (%d/%d valid, %d read)",
                    track->number, track->isrc,
                    collector.candidates[0].count, collector.total_valid,
                    collector.total_read);
            free(batch);
            return true;
        }

        verbose(3, verbosity, "Track %d: no ISRC found in short track (%d read)",
                track->number, collector.total_read);
        verbose(3, verbosity, "Track %d stats: read_errors=%d crc_valid=%d crc_invalid=%d ADR[0]=%d ADR[1]=%d ADR[2]=%d ADR[3]=%d",
                track->number, read_errors, crc_valid_count, crc_invalid_count,
                adr_counts[0], adr_counts[1], adr_counts[2], adr_counts[3]);
        free(batch);
        track->isrc[0] = '\0';
        return false;
    }

    /* Normal tracks: tranche-based sampling */

    /* Calculate tranche positions */
    int32_t tranche_pos[INITIAL_TRANCHES + RESCUE_TRANCHES];
    int frames_per_tranche;
    calculate_tranche_positions(track, INITIAL_TRANCHES, tranche_pos, &frames_per_tranche);

    /* Allocate buffer for batch reading */
    q_subchannel_t *batch = malloc(frames_per_tranche * sizeof(q_subchannel_t));
    if (!batch) {
        track->isrc[0] = '\0';
        return false;
    }

    /* Initial tranches per §5.3.1 */
    for (int t = 0; t < INITIAL_TRANCHES; t++) {
        int32_t base_lba = tranche_pos[t];

        /* Read all frames in this tranche with single SCSI command */
        int read_count = scsi_read_q_subchannel_batch(dev, base_lba, frames_per_tranche, batch);

        if (read_count > 0) {
            for (int f = 0; f < read_count; f++) {
                collector.total_read++;
                q_subchannel_t *q = &batch[f];

                if (q->crc_valid) {
                    crc_valid_count++;
                    if (q->adr < 4) adr_counts[q->adr]++;
                } else {
                    crc_invalid_count++;
                }
                /* Only accept frames with valid CRC and ISRC data */
                if (q->crc_valid && q->has_isrc) {
                    collector_add(&collector, q->isrc);
                }
            }
        } else {
            read_errors += frames_per_tranche;
            collector.total_read += frames_per_tranche;
        }

        /* Early termination per §5.3.1 if enough valid frames */
        if (collector.total_valid >= EARLY_STOP_VALID_FRAMES) {
            const char *winner = collector_get_majority(&collector);
            if (winner) {
                strncpy(track->isrc, winner, 12);
                track->isrc[12] = '\0';
                verbose(2, verbosity, "Track %d: ISRC %s (early, %d/%d valid, %d read)",
                        track->number, track->isrc,
                        collector.candidates[0].count, collector.total_valid,
                        collector.total_read);
                free(batch);
                return true;
            }
        }
    }

    /* Check for majority after initial tranches */
    const char *winner = collector_get_majority(&collector);
    if (winner) {
        strncpy(track->isrc, winner, 12);
        track->isrc[12] = '\0';
        verbose(2, verbosity, "Track %d: ISRC %s (%d/%d valid, %d read)",
                track->number, track->isrc,
                collector.candidates[0].count, collector.total_valid,
                collector.total_read);
        free(batch);
        return true;
    }

    /* Rescue sampling per §5.4.2 if we have candidates but no majority */
    if (collector.num_candidates > 0) {
        verbose(3, verbosity, "Track %d: rescue sampling (%d candidates, no majority)",
                track->number, collector.num_candidates);

        /* Calculate positions for rescue tranches */
        calculate_tranche_positions(track, INITIAL_TRANCHES + RESCUE_TRANCHES,
                                    tranche_pos, &frames_per_tranche);

        for (int t = INITIAL_TRANCHES; t < INITIAL_TRANCHES + RESCUE_TRANCHES; t++) {
            int32_t base_lba = tranche_pos[t];

            /* Read all frames in this tranche with single SCSI command */
            int read_count = scsi_read_q_subchannel_batch(dev, base_lba, frames_per_tranche, batch);

            if (read_count > 0) {
                for (int f = 0; f < read_count; f++) {
                    collector.total_read++;
                    q_subchannel_t *qp = &batch[f];

                    if (qp->crc_valid && qp->has_isrc) {
                        collector_add(&collector, qp->isrc);
                    }
                }
            } else {
                collector.total_read += frames_per_tranche;
            }

            /* Check after each rescue tranche */
            winner = collector_get_majority(&collector);
            if (winner) {
                strncpy(track->isrc, winner, 12);
                track->isrc[12] = '\0';
                verbose(2, verbosity, "Track %d: ISRC %s (rescue, %d/%d valid, %d read)",
                        track->number, track->isrc,
                        collector.candidates[0].count, collector.total_valid,
                        collector.total_read);
                free(batch);
                return true;
            }
        }

        /* Indeterminate per §5.4.3 - no strong majority */
        verbose(2, verbosity, "Track %d: indeterminate (%d candidates, best=%d/%d valid)",
                track->number, collector.num_candidates,
                collector.candidates[0].count, collector.total_valid);
    } else if (collector.total_valid == 0) {
        verbose(3, verbosity, "Track %d: no ISRC frames found (%d read)",
                track->number, collector.total_read);
    } else {
        verbose(3, verbosity, "Track %d: no valid ISRC (%d valid frames, no candidates)",
                track->number, collector.total_valid);
    }

    /* Detailed diagnostics at high verbosity */
    verbose(3, verbosity, "Track %d stats: read_errors=%d crc_valid=%d crc_invalid=%d ADR[0]=%d ADR[1]=%d ADR[2]=%d ADR[3]=%d",
            track->number, read_errors, crc_valid_count, crc_invalid_count,
            adr_counts[0], adr_counts[1], adr_counts[2], adr_counts[3]);

    free(batch);
    track->isrc[0] = '\0';
    return false;
}

/*
 * Main ISRC reading entry point
 */
int isrc_read_disc(toc_t *toc, const char *device, int verbosity)
{
    verbose(1, verbosity, "Starting ISRC scan");

    /* Open device */
    scsi_device_t *dev = scsi_open(device);
    if (!dev) {
        verbose(1, verbosity, "Failed to open device for ISRC reading");
        return -1;
    }

    int found_count = 0;
    bool disc_has_isrc = false;

    /* Count audio tracks */
    int audio_count = 0;
    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO) {
            audio_count++;
        }
    }

    verbose(1, verbosity, "ISRC scan: %d audio tracks", audio_count);

    /* Decide strategy based on track count */
    if (audio_count >= MIN_TRACKS_FOR_PROBE) {
        /* Use probe strategy per §5.2 */
        int probe_indices[PROBE_COUNT];
        int num_probes = select_probe_tracks(toc, probe_indices, verbosity);

        if (num_probes == PROBE_COUNT) {
            /* Probe selected tracks first */
            verbose(1, verbosity, "ISRC: probing %d tracks", num_probes);

            for (int i = 0; i < num_probes; i++) {
                track_t *track = &toc->tracks[probe_indices[i]];
                if (read_track_isrc(dev, track, verbosity)) {
                    disc_has_isrc = true;
                    found_count++;
                    verbose(1, verbosity, "ISRC probe hit: track %d has ISRC",
                            track->number);
                }
            }

            /* If no ISRCs found in probes, disc is ISRC-absent */
            if (!disc_has_isrc) {
                verbose(1, verbosity, "ISRC: no ISRCs found in probe tracks, skipping full scan");
                scsi_close(dev);
                return 0;
            }

            /* Scan remaining tracks */
            verbose(1, verbosity, "ISRC: scanning remaining tracks");
            for (int i = 0; i < toc->track_count; i++) {
                /* Skip non-audio and already-scanned probe tracks */
                if (toc->tracks[i].type != TRACK_TYPE_AUDIO) {
                    continue;
                }

                bool was_probe = false;
                for (int j = 0; j < num_probes; j++) {
                    if (probe_indices[j] == i) {
                        was_probe = true;
                        break;
                    }
                }
                if (was_probe) {
                    continue;
                }

                if (read_track_isrc(dev, &toc->tracks[i], verbosity)) {
                    found_count++;
                }
            }
        } else {
            /* Fall through to full scan */
            goto full_scan;
        }
    } else {
full_scan:
        /* Scan all audio tracks (small disc or not enough eligible for probing) */
        verbose(1, verbosity, "ISRC: scanning all %d audio tracks", audio_count);

        for (int i = 0; i < toc->track_count; i++) {
            if (toc->tracks[i].type != TRACK_TYPE_AUDIO) {
                continue;
            }

            if (read_track_isrc(dev, &toc->tracks[i], verbosity)) {
                found_count++;
            }
        }
    }

    scsi_close(dev);

    verbose(1, verbosity, "ISRC scan complete: %d ISRCs found", found_count);
    return found_count;
}
