/*
 * mbdiscid - Disc ID calculator
 * scsi.h - SCSI command abstraction layer
 */

#ifndef MBDISCID_SCSI_H
#define MBDISCID_SCSI_H

#include <stdint.h>
#include <stdbool.h>

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

#endif /* MBDISCID_SCSI_H */
