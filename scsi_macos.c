/*
 * mbdiscid - Disc ID calculator
 * scsi_macos.c - macOS SCSI implementation using IOKit
 */

#ifdef PLATFORM_MACOS

#include "scsi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <IOKit/storage/IOCDMediaBSDClient.h>

struct scsi_device {
    int fd;                      /* File descriptor for BSD device */
    char error[256];             /* Error message buffer */
};

/*
 * CRC-16 CCITT for Q-subchannel validation
 * Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
 */
static uint16_t crc16_ccitt(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;

    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }

    return crc;
}

/*
 * Decode 6-bit packed character to ASCII
 */
static char decode_isrc_char(uint8_t c)
{
    c &= 0x3F;
    if (c == 0) return '0';
    if (c >= 1 && c <= 9) return '0' + c;
    if (c >= 17 && c <= 42) return 'A' + (c - 17);
    return '?';
}

/*
 * Decode ISRC from raw Q-subchannel mode 3 data
 */
static bool decode_isrc_from_q(const uint8_t *q, char *isrc)
{
    if ((q[0] & 0x0F) != 3) {
        return false;
    }

    isrc[0] = decode_isrc_char(q[1] >> 2);
    isrc[1] = decode_isrc_char(((q[1] & 0x03) << 4) | (q[2] >> 4));
    isrc[2] = decode_isrc_char(((q[2] & 0x0F) << 2) | (q[3] >> 6));
    isrc[3] = decode_isrc_char(q[3] & 0x3F);
    isrc[4] = decode_isrc_char(q[4] >> 2);
    isrc[5] = '0' + ((q[5] >> 4) & 0x0F);
    isrc[6] = '0' + (q[5] & 0x0F);
    isrc[7] = '0' + ((q[6] >> 4) & 0x0F);
    isrc[8] = '0' + (q[6] & 0x0F);
    isrc[9] = '0' + ((q[7] >> 4) & 0x0F);
    isrc[10] = '0' + (q[7] & 0x0F);
    isrc[11] = '0' + ((q[8] >> 4) & 0x0F);
    isrc[12] = '\0';
    return true;
}

/*
 * Decode MCN from raw Q-subchannel mode 2 data
 */
static bool decode_mcn_from_q(const uint8_t *q, char *mcn)
{
    if ((q[0] & 0x0F) != 2) {
        return false;
    }

    mcn[0] = '0' + ((q[1] >> 4) & 0x0F);
    mcn[1] = '0' + (q[1] & 0x0F);
    mcn[2] = '0' + ((q[2] >> 4) & 0x0F);
    mcn[3] = '0' + (q[2] & 0x0F);
    mcn[4] = '0' + ((q[3] >> 4) & 0x0F);
    mcn[5] = '0' + (q[3] & 0x0F);
    mcn[6] = '0' + ((q[4] >> 4) & 0x0F);
    mcn[7] = '0' + (q[4] & 0x0F);
    mcn[8] = '0' + ((q[5] >> 4) & 0x0F);
    mcn[9] = '0' + (q[5] & 0x0F);
    mcn[10] = '0' + ((q[6] >> 4) & 0x0F);
    mcn[11] = '0' + (q[6] & 0x0F);
    mcn[12] = '0' + ((q[7] >> 4) & 0x0F);
    mcn[13] = '\0';
    return true;
}

scsi_device_t *scsi_open(const char *device)
{
    scsi_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return NULL;
    }

    dev->fd = open(device, O_RDONLY | O_NONBLOCK);
    if (dev->fd < 0) {
        snprintf(dev->error, sizeof(dev->error), "cannot open device: %s", device);
        free(dev);
        return NULL;
    }

    return dev;
}

void scsi_close(scsi_device_t *dev)
{
    if (dev) {
        if (dev->fd >= 0) {
            close(dev->fd);
        }
        free(dev);
    }
}

const char *scsi_error(scsi_device_t *dev)
{
    return dev ? dev->error : "null device";
}

/*
 * Read raw Q-subchannel data at a specific LBA using DKIOCCDREAD
 *
 * macOS DKIOCCDREAD can read subchannel data with the sector.
 * We request Q-subchannel (16 bytes) along with (or instead of) sector data.
 */
bool scsi_read_q_subchannel(scsi_device_t *dev, int32_t lba, q_subchannel_t *q)
{
    memset(q, 0, sizeof(*q));

    if (!dev || dev->fd < 0) {
        return false;
    }

    /*
     * Use DKIOCCDREAD with subchannel request
     * sectorType = kCDSectorTypeCDDA for audio
     * sectorArea = kCDSectorAreaSubChannelQ for Q subchannel only
     */
    dk_cd_read_t cd_read;
    memset(&cd_read, 0, sizeof(cd_read));

    /* 16 bytes for Q subchannel data */
    uint8_t buf[16];

    cd_read.offset = lba * kCDSectorSizeCDDA;  /* Convert LBA to byte offset */
    cd_read.sectorArea = kCDSectorAreaSubChannelQ;
    cd_read.sectorType = kCDSectorTypeCDDA;
    cd_read.buffer = buf;
    cd_read.bufferLength = sizeof(buf);

    if (ioctl(dev->fd, DKIOCCDREAD, &cd_read) < 0) {
        snprintf(dev->error, sizeof(dev->error),
                 "DKIOCCDREAD failed for LBA %d", lba);
        return false;
    }

    /*
     * The Q subchannel from DKIOCCDREAD may already be deinterleaved.
     * Format: 16 bytes where first 12 are the actual Q data + padding.
     * Check what we got and extract accordingly.
     */

    /* Validate CRC-16 over the first 12 bytes */
    uint16_t crc = crc16_ccitt(buf, 12);
    q->crc_valid = (crc == 0);

    /* Extract control and ADR */
    q->control = (buf[0] >> 4) & 0x0F;
    q->adr = buf[0] & 0x0F;

    /* Decode based on ADR mode */
    switch (q->adr) {
    case 1:
        /* Mode 1: Position data */
        q->track = buf[1];
        q->index = buf[2];
        q->has_isrc = false;
        q->has_mcn = false;
        break;

    case 2:
        /* Mode 2: MCN */
        if (decode_mcn_from_q(buf, q->mcn)) {
            q->has_mcn = true;
        }
        q->has_isrc = false;
        break;

    case 3:
        /* Mode 3: ISRC */
        if (decode_isrc_from_q(buf, q->isrc)) {
            q->has_isrc = true;
        }
        q->has_mcn = false;
        break;

    default:
        q->has_isrc = false;
        q->has_mcn = false;
        break;
    }

    return true;
}

/*
 * Read multiple Q-subchannel frames
 * On macOS, DKIOCCDREAD may not efficiently support multi-sector reads,
 * so we implement this as a loop. Linux gets the real batch benefit.
 */
int scsi_read_q_subchannel_batch(scsi_device_t *dev, int32_t lba, int count, q_subchannel_t *q)
{
    if (!dev || dev->fd < 0 || count <= 0) {
        return 0;
    }

    int success = 0;
    for (int i = 0; i < count; i++) {
        if (scsi_read_q_subchannel(dev, lba + i, &q[i])) {
            success++;
        } else {
            /* On read failure, zero out the entry */
            memset(&q[i], 0, sizeof(q[i]));
        }
    }

    return success;
}

/*
 * Read ISRC using IOKit CD media ioctl
 * (High-level interface - kept for compatibility)
 *
 * macOS provides DKIOCCDREADISRC ioctl for reading ISRC
 */
bool scsi_read_isrc(scsi_device_t *dev, int track, char *isrc)
{
    isrc[0] = '\0';

    if (!dev || dev->fd < 0 || track < 1 || track > 99) {
        return false;
    }

    /* Use DKIOCCDREADISRC ioctl */
    dk_cd_read_isrc_t isrc_req;
    memset(&isrc_req, 0, sizeof(isrc_req));
    isrc_req.track = track;

    if (ioctl(dev->fd, DKIOCCDREADISRC, &isrc_req) < 0) {
        snprintf(dev->error, sizeof(dev->error),
                 "DKIOCCDREADISRC failed for track %d", track);
        return false;
    }

    /* Check if ISRC is valid (non-zero) */
    bool valid = false;
    for (int i = 0; i < 12; i++) {
        if (isrc_req.isrc[i] != '\0' && isrc_req.isrc[i] != '0') {
            valid = true;
            break;
        }
    }

    if (!valid) {
        return false;
    }

    /* Copy ISRC - it's already a null-terminated string */
    memcpy(isrc, isrc_req.isrc, 12);
    isrc[12] = '\0';

    return true;
}

/*
 * Read MCN using IOKit CD media ioctl
 * (High-level interface - kept for compatibility)
 */
bool scsi_read_mcn(scsi_device_t *dev, char *mcn)
{
    mcn[0] = '\0';

    if (!dev || dev->fd < 0) {
        return false;
    }

    /* Use DKIOCCDREADMCN ioctl */
    dk_cd_read_mcn_t mcn_req;
    memset(&mcn_req, 0, sizeof(mcn_req));

    if (ioctl(dev->fd, DKIOCCDREADMCN, &mcn_req) < 0) {
        snprintf(dev->error, sizeof(dev->error), "DKIOCCDREADMCN failed");
        return false;
    }

    /* Check if MCN is valid (non-zero) */
    bool valid = false;
    for (int i = 0; i < 13; i++) {
        if (mcn_req.mcn[i] != '\0' && mcn_req.mcn[i] != '0') {
            valid = true;
            break;
        }
    }

    if (!valid) {
        return false;
    }

    /* Copy MCN */
    memcpy(mcn, mcn_req.mcn, 13);
    mcn[13] = '\0';

    return true;
}

/*
 * Read TOC to get track control bytes using DKIOCCDREADTOC
 */
bool scsi_read_toc_control(scsi_device_t *dev, int *first_track, int *last_track,
                           uint8_t *control)
{
    if (!dev || dev->fd < 0) {
        return false;
    }

    /* Use DKIOCCDREADTOC to get full TOC as raw data */
    dk_cd_read_toc_t toc_req;
    uint8_t toc_data[804];  /* Max: 4 header + 100 tracks * 8 bytes */

    memset(&toc_req, 0, sizeof(toc_req));
    memset(toc_data, 0, sizeof(toc_data));

    toc_req.format = kCDTOCFormatTOC;
    toc_req.buffer = toc_data;
    toc_req.bufferLength = sizeof(toc_data);

    if (ioctl(dev->fd, DKIOCCDREADTOC, &toc_req) < 0) {
        snprintf(dev->error, sizeof(dev->error), "DKIOCCDREADTOC failed");
        return false;
    }

    /* Parse TOC header (same format as SCSI READ TOC response)
     * Bytes 0-1: TOC data length (big endian)
     * Byte 2: First track number
     * Byte 3: Last track number
     */
    *first_track = toc_data[2];
    *last_track = toc_data[3];

    /* Initialize control array */
    memset(control, 0, 100);

    /* Parse track descriptors starting at byte 4
     * Each descriptor is 8 bytes:
     *   Byte 0: Reserved
     *   Byte 1: ADR (upper 4 bits) | Control (lower 4 bits)
     *   Byte 2: Track number
     *   Byte 3: Reserved
     *   Bytes 4-7: Track start address
     */
    uint16_t toc_len = ((uint16_t)toc_data[0] << 8) | toc_data[1];
    int num_descriptors = (toc_len - 2) / 8;

    for (int i = 0; i < num_descriptors && i < 100; i++) {
        int offset = 4 + i * 8;
        int track_num = toc_data[offset + 2];
        uint8_t ctrl = toc_data[offset + 1] & 0x0F;

        if (track_num >= 1 && track_num <= 99) {
            control[track_num] = ctrl;
        }
    }

    return true;
}

#endif /* PLATFORM_MACOS */
