/*
 * mbdiscid - Disc ID calculator
 * scsi.h - SCSI command abstraction layer
 */

#ifndef MBDISCID_SCSI_H
#define MBDISCID_SCSI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* SCSI command result */
typedef struct {
    int status;           /* 0 = success, non-zero = error */
    uint8_t sense_key;    /* SCSI sense key if error */
    uint8_t asc;          /* Additional sense code */
    uint8_t ascq;         /* Additional sense code qualifier */
} scsi_result_t;

/* Q-subchannel data from a single frame */
typedef struct {
    uint8_t control;      /* Control nibble (4 bits) */
    uint8_t adr;          /* ADR nibble (4 bits) */
    uint8_t track;        /* Track number */
    uint8_t index;        /* Index */
    char isrc[13];        /* ISRC if ADR=3, null-terminated */
    char mcn[14];         /* MCN if ADR=2, null-terminated */
    bool crc_valid;       /* True if CRC passed */
    bool has_isrc;        /* True if ADR=3 and valid ISRC present */
    bool has_mcn;         /* True if ADR=2 and valid MCN present */
} q_subchannel_t;

/* Opaque SCSI device handle */
typedef struct scsi_device scsi_device_t;

/*
 * Open SCSI device
 * Returns NULL on failure
 */
scsi_device_t *scsi_open(const char *device);

/*
 * Close SCSI device
 */
void scsi_close(scsi_device_t *dev);

/*
 * Read raw Q-subchannel data at a specific LBA
 * Uses READ CD command (0xBE) with subchannel selector
 *
 * lba: logical block address to read
 * q: output structure for Q-subchannel data
 *
 * Returns true if read succeeded, false on I/O error
 * Check q->crc_valid to verify data integrity
 * Check q->has_isrc / q->has_mcn for content type
 */
bool scsi_read_q_subchannel(scsi_device_t *dev, int32_t lba, q_subchannel_t *q);

/*
 * Read multiple Q-subchannel frames in a single SCSI command
 * Much more efficient than calling scsi_read_q_subchannel() in a loop
 *
 * lba: starting logical block address
 * count: number of frames to read
 * q: output array, must have space for 'count' entries
 *
 * Returns number of frames successfully read (may be less than count on error)
 */
int scsi_read_q_subchannel_batch(scsi_device_t *dev, int32_t lba, int count, q_subchannel_t *q);

/*
 * Read ISRC for a specific track using READ SUB-CHANNEL command
 * (High-level interface - drive handles subchannel reading internally)
 *
 * track: track number (1-99)
 * isrc: output buffer, must be at least 13 bytes (12 chars + null)
 *
 * Returns true if valid ISRC was read, false otherwise
 * On success, isrc contains null-terminated 12-character ISRC
 * On failure, isrc[0] = '\0'
 */
bool scsi_read_isrc(scsi_device_t *dev, int track, char *isrc);

/*
 * Read MCN using READ SUB-CHANNEL command
 * (High-level interface - drive handles subchannel reading internally)
 *
 * mcn: output buffer, must be at least 14 bytes (13 chars + null)
 *
 * Returns true if valid MCN was read, false otherwise
 */
bool scsi_read_mcn(scsi_device_t *dev, char *mcn);

/*
 * Get last error message
 */
const char *scsi_error(scsi_device_t *dev);

/*
 * Read TOC to get track control bytes (for determining audio vs data tracks)
 *
 * first_track: output - first track number
 * last_track: output - last track number
 * control: output array, must have space for 100 entries (tracks 0-99)
 *          control[track_num] = control byte for that track
 *          Bit 2 (0x04): 0 = audio, 1 = data
 *
 * Returns true on success
 */
bool scsi_read_toc_control(scsi_device_t *dev, int *first_track, int *last_track,
                           uint8_t *control);

/*
 * Read Full TOC (format 2) to get complete track info including multi-session
 *
 * Full TOC provides:
 *   - Track numbers, offsets, and control bytes
 *   - Session information for each track
 *   - Per-session leadout positions
 *
 * Parameters:
 *   first_track: output - first track number
 *   last_track: output - last track number
 *   control: output array[100] - control nibble for each track (index 1-99)
 *   session: output array[100] - session number for each track (index 1-99)
 *   offsets: output array[100] - LBA offset for each track (index 1-99)
 *   session_leadouts: output array[10] - leadout LBA for each session (index 0-9)
 *   last_session: output - highest session number
 *
 * Returns true on success, false on error
 */
bool scsi_read_full_toc(scsi_device_t *dev, int *first_track, int *last_track,
                        uint8_t *control, uint8_t *session, int32_t *offsets,
                        int32_t *session_leadouts, int *last_session);

/*
 * Read raw CD-Text data using READ TOC/PMA/ATIP command (format 5)
 *
 * data: output pointer to allocated buffer (caller must free)
 * len: output length of data in bytes
 *
 * Returns true on success, false on error or no CD-Text present
 * On success, *data contains raw CD-Text packs (caller must free)
 * On failure or no CD-Text, *data = NULL, *len = 0
 */
bool scsi_read_cdtext_raw(scsi_device_t *dev, uint8_t **data, size_t *len);

#endif /* MBDISCID_SCSI_H */
