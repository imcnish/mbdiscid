/*
 * mbdiscid - Disc ID calculator
 * isrc.h - ISRC acquisition per spec §5
 */

#ifndef MBDISCID_ISRC_H
#define MBDISCID_ISRC_H

#include "types.h"
#include "scsi.h"

/*
 * Read ISRCs from disc using spec §5 algorithm:
 * - Raw subchannel reading at specific LBA positions
 * - Tranche-based sampling (4 tranches × 32 frames = 128 samples)
 * - CRC validation per frame
 * - Probe strategy for n≥5 tracks (3 probes at 33/50/67%)
 * - Majority voting with strong majority rule (2:1)
 * - Rescue sampling (2 additional tranches) if no majority
 * - Early termination if no ISRCs detected in probes
 *
 * toc: TOC with track info (modified in place - ISRCs filled in)
 * device: device path (will be opened/closed internally)
 * verbosity: verbosity level for diagnostics
 *
 * Returns number of tracks with valid ISRCs found (0 is valid, not an error)
 * Returns -1 on device error
 */
int isrc_read_disc(toc_t *toc, const char *device, int verbosity);

/*
 * Validate ISRC format per spec §5.1.3:
 * - 2 uppercase letters (country code)
 * - 3 alphanumeric (registrant)
 * - 2 digits (year)
 * - 5 digits (designation)
 * - Not all zeros
 *
 * Returns true if valid
 */
bool isrc_validate(const char *isrc);

#endif /* MBDISCID_ISRC_H */
