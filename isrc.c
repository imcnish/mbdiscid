/*
 * mbdiscid - Disc ID calculator
 * isrc.c - ISRC acquisition per spec §5
 */

#include "isrc.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Configuration per spec §5 */
#define PROBE_COUNT          3
#define MIN_TRACKS_FOR_PROBE 5
#define MAX_CANDIDATES       8
#define INITIAL_TRANCHES     3
#define RESCUE_TRANCHES      1
#define FRAMES_PER_TRANCHE   192
#define BOOKEND_FRAMES       (2 * 75)
#define SHORT_TRACK_THRESHOLD ((2 * BOOKEND_FRAMES) + ((INITIAL_TRANCHES + RESCUE_TRANCHES + 1) * FRAMES_PER_TRANCHE))
#define EARLY_STOP_VALID_FRAMES 64

typedef struct {
    char isrc[13];
    int count;
} isrc_candidate_t;

typedef struct {
    isrc_candidate_t candidates[MAX_CANDIDATES];
    int num_candidates;
    int total_valid;
    int total_read;
} isrc_collector_t;

bool isrc_validate(const char *isrc)
{
    if (!isrc || strlen(isrc) != 12) {
        return false;
    }

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

    if (!isupper((unsigned char)isrc[0]) || !isupper((unsigned char)isrc[1])) {
        return false;
    }

    for (int i = 2; i < 5; i++) {
        if (!isalnum((unsigned char)isrc[i])) {
            return false;
        }
    }

    if (!isdigit((unsigned char)isrc[5]) || !isdigit((unsigned char)isrc[6])) {
        return false;
    }

    for (int i = 7; i < 12; i++) {
        if (!isdigit((unsigned char)isrc[i])) {
            return false;
        }
    }

    return true;
}

static bool is_short_track(const track_t *track)
{
    return track->length < SHORT_TRACK_THRESHOLD;
}

static void collector_add(isrc_collector_t *c, const char *isrc)
{
    if (!isrc_validate(isrc)) {
        return;
    }

    c->total_valid++;

    for (int i = 0; i < c->num_candidates; i++) {
        if (strcmp(c->candidates[i].isrc, isrc) == 0) {
            c->candidates[i].count++;
            return;
        }
    }

    if (c->num_candidates < MAX_CANDIDATES) {
        strncpy(c->candidates[c->num_candidates].isrc, isrc, 12);
        c->candidates[c->num_candidates].isrc[12] = '\0';
        c->candidates[c->num_candidates].count = 1;
        c->num_candidates++;
    }
}

static const char *collector_get_majority(isrc_collector_t *c)
{
    if (c->num_candidates == 0) {
        return NULL;
    }

    int max_idx = 0;
    int max_count = c->candidates[0].count;

    for (int i = 1; i < c->num_candidates; i++) {
        if (c->candidates[i].count > max_count) {
            max_count = c->candidates[i].count;
            max_idx = i;
        }
    }

    int second_max = 0;
    for (int i = 0; i < c->num_candidates; i++) {
        if (i != max_idx && c->candidates[i].count > second_max) {
            second_max = c->candidates[i].count;
        }
    }

    if (max_count >= 2 && (second_max == 0 || max_count >= 2 * second_max)) {
        return c->candidates[max_idx].isrc;
    }

    return NULL;
}

/*
 * Format all candidates as a string for verbose output
 * Returns allocated string, caller must free
 */
static char *collector_format_candidates(isrc_collector_t *c)
{
    if (c->num_candidates == 0) {
        return xstrdup("(none)");
    }

    /* Estimate buffer size: 12 chars ISRC + "×" + count + ", " per candidate */
    size_t bufsize = c->num_candidates * 24;
    char *buf = xmalloc(bufsize);
    buf[0] = '\0';

    for (int i = 0; i < c->num_candidates; i++) {
        char entry[32];
        snprintf(entry, sizeof(entry), "%s×%d", c->candidates[i].isrc, c->candidates[i].count);
        if (i > 0) {
            strcat(buf, ", ");
        }
        strcat(buf, entry);
    }

    return buf;
}

static int select_probe_tracks(const toc_t *toc, int *probe_indices, int verbosity)
{
    int eligible[MAX_TRACKS];
    int num_eligible = 0;

    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO && !is_short_track(&toc->tracks[i])) {
            eligible[num_eligible++] = i;
        }
    }

    verbose(2, verbosity, "isrc: %d eligible tracks for probe (of %d audio)",
            num_eligible, toc->audio_count);

    if (num_eligible < PROBE_COUNT) {
        return 0;
    }

    int positions[3];
    positions[0] = num_eligible / 3;
    positions[1] = num_eligible / 2;
    positions[2] = (num_eligible * 2) / 3;

    if (positions[0] == 0 && num_eligible > 3) {
        positions[0] = 1;
    }

    if (positions[2] == num_eligible - 1 && num_eligible > 3) {
        positions[2] = num_eligible - 2;
    }

    if (positions[1] == positions[0]) positions[1]++;
    if (positions[2] == positions[1]) positions[2]++;
    if (positions[2] >= num_eligible) positions[2] = num_eligible - 1;

    for (int i = 0; i < PROBE_COUNT; i++) {
        probe_indices[i] = eligible[positions[i]];
    }

    verbose(2, verbosity, "isrc: probe tracks: %d, %d, %d",
            toc->tracks[probe_indices[0]].number,
            toc->tracks[probe_indices[1]].number,
            toc->tracks[probe_indices[2]].number);

    return PROBE_COUNT;
}

static void calculate_tranche_positions(const track_t *track, int num_tranches,
                                        int32_t *positions, int *frames_per_tranche)
{
    int32_t track_start = track->offset;
    int32_t track_length = track->length;

    int32_t usable_start = track_start + BOOKEND_FRAMES;
    int32_t usable_end = track_start + track_length - BOOKEND_FRAMES;

    if (usable_end <= usable_start) {
        usable_start = track_start;
        usable_end = track_start + track_length;
    }

    int32_t usable_length = usable_end - usable_start;

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

static bool read_track_isrc(scsi_device_t *dev, track_t *track, int verbosity)
{
    isrc_collector_t collector = {0};
    int crc_valid_count = 0;
    int crc_invalid_count = 0;
    int adr_counts[4] = {0};
    int read_errors = 0;

    if (track->length < SHORT_TRACK_THRESHOLD) {
        verbose(2, verbosity, "isrc: track %d: short track (%d frames), full scan",
                track->number, track->length);

        q_subchannel_t *batch = malloc(track->length * sizeof(q_subchannel_t));
        if (!batch) {
            track->isrc[0] = '\0';
            return false;
        }

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

        /* Log all candidates at level 3 */
        if (verbosity >= 3 && collector.num_candidates > 0) {
            char *candidates = collector_format_candidates(&collector);
            verbose(3, verbosity, "isrc: track %d: candidates: %s", track->number, candidates);
            free(candidates);
        }

        const char *winner = collector_get_majority(&collector);
        if (winner) {
            strncpy(track->isrc, winner, 12);
            track->isrc[12] = '\0';
            verbose(2, verbosity, "isrc: track %d: %s (majority %d/%d)",
                    track->number, track->isrc,
                    collector.candidates[0].count, collector.total_valid);
            free(batch);
            return true;
        }

        verbose(3, verbosity, "isrc: track %d: no majority (%d read, %d valid)",
                track->number, collector.total_read, collector.total_valid);
        verbose(3, verbosity, "isrc: track %d: ADR [0:%d 1:%d 2:%d 3:%d] crc_ok:%d crc_bad:%d read_err:%d",
                track->number, adr_counts[0], adr_counts[1], adr_counts[2], adr_counts[3],
                crc_valid_count, crc_invalid_count, read_errors);
        free(batch);
        track->isrc[0] = '\0';
        return false;
    }

    int32_t tranche_pos[INITIAL_TRANCHES + RESCUE_TRANCHES];
    int frames_per_tranche;
    calculate_tranche_positions(track, INITIAL_TRANCHES, tranche_pos, &frames_per_tranche);

    q_subchannel_t *batch = malloc(frames_per_tranche * sizeof(q_subchannel_t));
    if (!batch) {
        track->isrc[0] = '\0';
        return false;
    }

    for (int t = 0; t < INITIAL_TRANCHES; t++) {
        int32_t base_lba = tranche_pos[t];

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
                if (q->crc_valid && q->has_isrc) {
                    collector_add(&collector, q->isrc);
                }
            }
        } else {
            read_errors += frames_per_tranche;
            collector.total_read += frames_per_tranche;
        }

        if (collector.total_valid >= EARLY_STOP_VALID_FRAMES) {
            const char *winner = collector_get_majority(&collector);
            if (winner) {
                strncpy(track->isrc, winner, 12);
                track->isrc[12] = '\0';

                /* Log all candidates at level 3 before returning */
                if (verbosity >= 3 && collector.num_candidates > 0) {
                    char *candidates = collector_format_candidates(&collector);
                    verbose(3, verbosity, "isrc: track %d: candidates: %s", track->number, candidates);
                    free(candidates);
                }

                verbose(2, verbosity, "isrc: track %d: %s (early, %d/%d)",
                        track->number, track->isrc,
                        collector.candidates[0].count, collector.total_valid);
                free(batch);
                return true;
            }
        }
    }

    /* Log all candidates at level 3 */
    if (verbosity >= 3 && collector.num_candidates > 0) {
        char *candidates = collector_format_candidates(&collector);
        verbose(3, verbosity, "isrc: track %d: candidates: %s", track->number, candidates);
        free(candidates);
    }

    const char *winner = collector_get_majority(&collector);
    if (winner) {
        strncpy(track->isrc, winner, 12);
        track->isrc[12] = '\0';
        verbose(2, verbosity, "isrc: track %d: %s (%d/%d)",
                track->number, track->isrc,
                collector.candidates[0].count, collector.total_valid);
        free(batch);
        return true;
    }

    if (collector.num_candidates > 0) {
        verbose(2, verbosity, "isrc: track %d: rescue sampling (%d candidates, no majority)",
                track->number, collector.num_candidates);

        calculate_tranche_positions(track, INITIAL_TRANCHES + RESCUE_TRANCHES,
                                    tranche_pos, &frames_per_tranche);

        for (int t = INITIAL_TRANCHES; t < INITIAL_TRANCHES + RESCUE_TRANCHES; t++) {
            int32_t base_lba = tranche_pos[t];

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
                read_errors += frames_per_tranche;
                collector.total_read += frames_per_tranche;
            }

            winner = collector_get_majority(&collector);
            if (winner) {
                strncpy(track->isrc, winner, 12);
                track->isrc[12] = '\0';

                /* Log all candidates at level 3 */
                if (verbosity >= 3 && collector.num_candidates > 0) {
                    char *candidates = collector_format_candidates(&collector);
                    verbose(3, verbosity, "isrc: track %d: candidates: %s", track->number, candidates);
                    free(candidates);
                }

                verbose(2, verbosity, "isrc: track %d: %s (rescue, %d/%d)",
                        track->number, track->isrc,
                        collector.candidates[0].count, collector.total_valid);
                free(batch);
                return true;
            }
        }

        verbose(2, verbosity, "isrc: track %d: indeterminate (%d candidates, best=%d/%d)",
                track->number, collector.num_candidates,
                collector.candidates[0].count, collector.total_valid);
    } else if (collector.total_valid == 0) {
        verbose(2, verbosity, "isrc: track %d: no ISRC frames (%d read)",
                track->number, collector.total_read);
    } else {
        verbose(2, verbosity, "isrc: track %d: no valid candidates (%d valid frames)",
                track->number, collector.total_valid);
    }

    verbose(3, verbosity, "isrc: track %d: ADR [0:%d 1:%d 2:%d 3:%d] crc_ok:%d crc_bad:%d read_err:%d",
            track->number, adr_counts[0], adr_counts[1], adr_counts[2], adr_counts[3],
            crc_valid_count, crc_invalid_count, read_errors);

    free(batch);
    track->isrc[0] = '\0';
    return false;
}

int isrc_read_disc(toc_t *toc, const char *device, int verbosity)
{
    verbose(1, verbosity, "isrc: starting scan");

    scsi_device_t *dev = scsi_open(device);
    if (!dev) {
        verbose(1, verbosity, "isrc: failed to open device");
        return -1;
    }

    scsi_set_verbosity(dev, verbosity);

    int found_count = 0;

    int audio_count = 0;
    for (int i = 0; i < toc->track_count; i++) {
        if (toc->tracks[i].type == TRACK_TYPE_AUDIO) {
            audio_count++;
        }
    }

    verbose(1, verbosity, "isrc: %d audio tracks to scan", audio_count);

#ifdef PLATFORM_MACOS
    /*
     * macOS: Try batch Q-subchannel reading first (requires exclusive SCSI access).
     * This gives us reliable ISRC reading with CRC validation and majority voting.
     * Fall back to DKIOCCDREADISRC ioctl if batch reading fails (less reliable).
     */

    /* Test if batch reading works by reading a few frames */
    bool batch_works = false;
    if (audio_count > 0) {
        int test_track = -1;
        for (int i = 0; i < toc->track_count; i++) {
            if (toc->tracks[i].type == TRACK_TYPE_AUDIO) {
                test_track = i;
                break;
            }
        }
        if (test_track >= 0) {
            q_subchannel_t test_q[10];
            int32_t test_lba = toc->tracks[test_track].offset + 100;
            verbose(2, verbosity, "isrc: testing batch read at LBA %d", test_lba);
            int test_count = scsi_read_q_subchannel_batch(dev, test_lba, 10, test_q);
            if (test_count > 0) {
                /* Check if we got any valid Q data with CRC */
                int valid_frames = 0;
                for (int i = 0; i < test_count; i++) {
                    if (test_q[i].crc_valid) {
                        valid_frames++;
                    }
                }
                verbose(2, verbosity, "isrc: batch test: %d frames, %d CRC valid",
                        test_count, valid_frames);
                if (valid_frames > 0) {
                    batch_works = true;
                    verbose(1, verbosity, "isrc: using batch subchannel with CRC validation");
                }
            } else {
                verbose(2, verbosity, "isrc: batch read failed: %s", scsi_error(dev));
            }
        }
    }

    if (!batch_works) {
        /* Fall back to drive-based ISRC reading (less reliable) */
        verbose(1, verbosity, "isrc: WARNING - using drive-based reading (no CRC validation)");

        for (int i = 0; i < toc->track_count; i++) {
            if (toc->tracks[i].type != TRACK_TYPE_AUDIO) {
                continue;
            }

            track_t *track = &toc->tracks[i];
            char isrc[13];

            if (scsi_read_isrc(dev, track->number, isrc)) {
                strncpy(track->isrc, isrc, sizeof(track->isrc) - 1);
                track->isrc[sizeof(track->isrc) - 1] = '\0';
                found_count++;
                verbose(2, verbosity, "isrc: track %d: %s", track->number, isrc);
            } else {
                verbose(2, verbosity, "isrc: track %d: not found", track->number);
            }
        }

        scsi_close(dev);
        verbose(1, verbosity, "isrc: scan complete, %d found", found_count);
        return found_count;
    }

    /* Batch reading works - use the same algorithm as Linux */
#endif

    bool disc_has_isrc = false;

    /* Use batch Q-subchannel reading with majority voting */
    if (audio_count >= MIN_TRACKS_FOR_PROBE) {
        int probe_indices[PROBE_COUNT];
        int num_probes = select_probe_tracks(toc, probe_indices, verbosity);

        if (num_probes == PROBE_COUNT) {
            verbose(1, verbosity, "isrc: probing %d tracks", num_probes);

            for (int i = 0; i < num_probes; i++) {
                track_t *track = &toc->tracks[probe_indices[i]];
                if (read_track_isrc(dev, track, verbosity)) {
                    disc_has_isrc = true;
                    found_count++;
                    verbose(1, verbosity, "isrc: probe hit on track %d", track->number);
                }
            }

            if (!disc_has_isrc) {
                verbose(1, verbosity, "isrc: no ISRCs in probe tracks, skipping full scan");
                scsi_close(dev);
                return 0;
            }

            verbose(1, verbosity, "isrc: scanning remaining tracks");
            for (int i = 0; i < toc->track_count; i++) {
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
            goto full_scan;
        }
    } else {
full_scan:
        verbose(1, verbosity, "isrc: full scan of %d audio tracks", audio_count);

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

    verbose(1, verbosity, "isrc: scan complete, %d found", found_count);
    return found_count;
}
