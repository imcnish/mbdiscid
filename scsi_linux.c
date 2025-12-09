/*
 * mbdiscid - Disc ID calculator
 * scsi_linux.c - Linux SCSI implementation using SG_IO
 */

#ifdef PLATFORM_LINUX

#include "scsi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>

/* SCSI commands */
#define READ_SUBCHANNEL 0x42
#define READ_CD         0xBE

/* Subchannel data format codes */
#define SUB_Q_CHANNEL_DATA  0x00
#define SUB_Q_MCN           0x02
#define SUB_Q_ISRC          0x03

/* Timeout in milliseconds */
#define SCSI_TIMEOUT 30000

struct scsi_device {
    int fd;
    char error[256];
};

/*
 * Decode 6-bit packed character to ASCII
 * Q-subchannel uses 6-bit encoding: 0='0', 1-9='1'-'9', 17-42='A'-'Z'
 */
static char decode_isrc_char(uint8_t c)
{
    c &= 0x3F;  /* Mask to 6 bits */
    if (c == 0) return '0';
    if (c >= 1 && c <= 9) return '0' + c;
    if (c >= 17 && c <= 42) return 'A' + (c - 17);
    return '?';  /* Invalid */
}

scsi_device_t *scsi_open(const char *device)
{
    scsi_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return NULL;
    }

    dev->fd = open(device, O_RDONLY | O_NONBLOCK);
    if (dev->fd < 0) {
        snprintf(dev->error, sizeof(dev->error),
                 "cannot open device: %s", device);
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
 * Execute SCSI command using SG_IO
 */
static int scsi_cmd(scsi_device_t *dev,
                    unsigned char *cdb, int cdb_len,
                    unsigned char *buf, int buf_len,
                    unsigned char *sense, int sense_len)
{
    struct sg_io_hdr io_hdr;

    memset(&io_hdr, 0, sizeof(io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = cdb_len;
    io_hdr.mx_sb_len = sense_len;
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = buf_len;
    io_hdr.dxferp = buf;
    io_hdr.cmdp = cdb;
    io_hdr.sbp = sense;
    io_hdr.timeout = SCSI_TIMEOUT;

    if (ioctl(dev->fd, SG_IO, &io_hdr) < 0) {
        snprintf(dev->error, sizeof(dev->error), "SG_IO ioctl failed");
        return -1;
    }

    /* Check for SCSI errors */
    if (io_hdr.status != 0 || io_hdr.host_status != 0 || io_hdr.driver_status != 0) {
        snprintf(dev->error, sizeof(dev->error),
                 "SCSI error: status=%d host=%d driver=%d",
                 io_hdr.status, io_hdr.host_status, io_hdr.driver_status);
        return -1;
    }

    return 0;
}

/*
 * Read raw Q-subchannel data at a specific LBA using READ CD command
 *
 * READ CD (0xBE) CDB format:
 *   Byte 0: 0xBE (opcode)
 *   Byte 1: Expected sector type (bits 2-4): 1 = CD-DA
 *   Bytes 2-5: Starting LBA (big endian)
 *   Bytes 6-8: Transfer length (big endian)
 *   Byte 9: Main channel selection flags
 *   Byte 10: Subchannel selection: 1 = raw (96 bytes), 2 = Q only (16 bytes)
 *   Byte 11: 0
 *
 * With subchannel = 1 (raw 96 bytes deinterleaved):
 *   Bytes 0-11: P subchannel
 *   Bytes 12-23: Q subchannel (what we want)
 *   Bytes 24-95: R-W subchannels
 *
 * Q subchannel format (12 bytes):
 *   Byte 0: Control (4 bits) | ADR (4 bits)
 *   Bytes 1-9: Mode-dependent data
 *   Bytes 10-11: CRC-16 CCITT
 */
bool scsi_read_q_subchannel(scsi_device_t *dev, int32_t lba, q_subchannel_t *q)
{
    unsigned char cdb[12];
    unsigned char buf[16];  /* Formatted Q subchannel = 16 bytes */
    unsigned char sense[32];

    memset(q, 0, sizeof(*q));

    if (!dev || dev->fd < 0) {
        return false;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_CD;
    cdb[1] = 0x00;            /* Any sector type */
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8) & 0xFF;
    cdb[5] = lba & 0xFF;
    cdb[6] = 0;               /* Transfer length MSB */
    cdb[7] = 0;
    cdb[8] = 1;               /* Transfer length LSB = 1 sector */
    cdb[9] = 0x00;            /* No main channel data */
    cdb[10] = 0x02;           /* Subchannel = 2 (formatted Q, 16 bytes) */

    memset(buf, 0, sizeof(buf));
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf), sense, sizeof(sense)) < 0) {
        return false;
    }

    /*
     * Formatted Q subchannel (16 bytes) from READ CD:
     * Byte 0: Control (4 bits) | ADR (4 bits)
     * Byte 1: Track number
     * Byte 2: Index
     * Byte 3: Relative minute
     * Byte 4: Relative second
     * Byte 5: Relative frame
     * Byte 6: Zero
     * Byte 7: Absolute minute
     * Byte 8: Absolute second
     * Byte 9: Absolute frame
     * Bytes 10-15: Zero or ISRC/MCN data depending on ADR
     *
     * Note: For ADR=3 (ISRC), the ISRC data replaces position data
     */

    /* Extract control and ADR */
    q->control = (buf[0] >> 4) & 0x0F;
    q->adr = buf[0] & 0x0F;

    /* For formatted Q, there's no CRC - assume valid if we got data */
    q->crc_valid = (buf[0] != 0 || buf[1] != 0);

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
        /* Mode 2: MCN - decode from BCD in bytes 1-7 */
        q->mcn[0] = '0' + ((buf[1] >> 4) & 0x0F);
        q->mcn[1] = '0' + (buf[1] & 0x0F);
        q->mcn[2] = '0' + ((buf[2] >> 4) & 0x0F);
        q->mcn[3] = '0' + (buf[2] & 0x0F);
        q->mcn[4] = '0' + ((buf[3] >> 4) & 0x0F);
        q->mcn[5] = '0' + (buf[3] & 0x0F);
        q->mcn[6] = '0' + ((buf[4] >> 4) & 0x0F);
        q->mcn[7] = '0' + (buf[4] & 0x0F);
        q->mcn[8] = '0' + ((buf[5] >> 4) & 0x0F);
        q->mcn[9] = '0' + (buf[5] & 0x0F);
        q->mcn[10] = '0' + ((buf[6] >> 4) & 0x0F);
        q->mcn[11] = '0' + (buf[6] & 0x0F);
        q->mcn[12] = '0' + ((buf[7] >> 4) & 0x0F);
        q->mcn[13] = '\0';
        q->has_mcn = true;
        q->has_isrc = false;
        break;

    case 3:
        /* Mode 3: ISRC - decode from 6-bit packed + BCD */
        q->isrc[0] = decode_isrc_char(buf[1] >> 2);
        q->isrc[1] = decode_isrc_char(((buf[1] & 0x03) << 4) | (buf[2] >> 4));
        q->isrc[2] = decode_isrc_char(((buf[2] & 0x0F) << 2) | (buf[3] >> 6));
        q->isrc[3] = decode_isrc_char(buf[3] & 0x3F);
        q->isrc[4] = decode_isrc_char(buf[4] >> 2);
        q->isrc[5] = '0' + ((buf[5] >> 4) & 0x0F);
        q->isrc[6] = '0' + (buf[5] & 0x0F);
        q->isrc[7] = '0' + ((buf[6] >> 4) & 0x0F);
        q->isrc[8] = '0' + (buf[6] & 0x0F);
        q->isrc[9] = '0' + ((buf[7] >> 4) & 0x0F);
        q->isrc[10] = '0' + (buf[7] & 0x0F);
        q->isrc[11] = '0' + ((buf[8] >> 4) & 0x0F);
        q->isrc[12] = '\0';
        q->has_isrc = true;
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
 * Decode a single 16-byte formatted Q-subchannel buffer into q_subchannel_t
 */
static void decode_q_buffer(const uint8_t *buf, q_subchannel_t *q)
{
    memset(q, 0, sizeof(*q));

    /* Extract control and ADR */
    q->control = (buf[0] >> 4) & 0x0F;
    q->adr = buf[0] & 0x0F;

    /* For formatted Q, there's no CRC - assume valid if we got data */
    q->crc_valid = (buf[0] != 0 || buf[1] != 0);

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
        /* Mode 2: MCN - decode from BCD in bytes 1-7 */
        q->mcn[0] = '0' + ((buf[1] >> 4) & 0x0F);
        q->mcn[1] = '0' + (buf[1] & 0x0F);
        q->mcn[2] = '0' + ((buf[2] >> 4) & 0x0F);
        q->mcn[3] = '0' + (buf[2] & 0x0F);
        q->mcn[4] = '0' + ((buf[3] >> 4) & 0x0F);
        q->mcn[5] = '0' + (buf[3] & 0x0F);
        q->mcn[6] = '0' + ((buf[4] >> 4) & 0x0F);
        q->mcn[7] = '0' + (buf[4] & 0x0F);
        q->mcn[8] = '0' + ((buf[5] >> 4) & 0x0F);
        q->mcn[9] = '0' + (buf[5] & 0x0F);
        q->mcn[10] = '0' + ((buf[6] >> 4) & 0x0F);
        q->mcn[11] = '0' + (buf[6] & 0x0F);
        q->mcn[12] = '0' + ((buf[7] >> 4) & 0x0F);
        q->mcn[13] = '\0';
        q->has_mcn = true;
        q->has_isrc = false;
        break;

    case 3:
        /* Mode 3: ISRC - decode from 6-bit packed + BCD */
        q->isrc[0] = decode_isrc_char(buf[1] >> 2);
        q->isrc[1] = decode_isrc_char(((buf[1] & 0x03) << 4) | (buf[2] >> 4));
        q->isrc[2] = decode_isrc_char(((buf[2] & 0x0F) << 2) | (buf[3] >> 6));
        q->isrc[3] = decode_isrc_char(buf[3] & 0x3F);
        q->isrc[4] = decode_isrc_char(buf[4] >> 2);
        q->isrc[5] = '0' + ((buf[5] >> 4) & 0x0F);
        q->isrc[6] = '0' + (buf[5] & 0x0F);
        q->isrc[7] = '0' + ((buf[6] >> 4) & 0x0F);
        q->isrc[8] = '0' + (buf[6] & 0x0F);
        q->isrc[9] = '0' + ((buf[7] >> 4) & 0x0F);
        q->isrc[10] = '0' + (buf[7] & 0x0F);
        q->isrc[11] = '0' + ((buf[8] >> 4) & 0x0F);
        q->isrc[12] = '\0';
        q->has_isrc = true;
        q->has_mcn = false;
        break;

    default:
        q->has_isrc = false;
        q->has_mcn = false;
        break;
    }
}

/*
 * Read multiple Q-subchannel frames in a single SCSI command
 * Returns number of frames successfully read
 */
int scsi_read_q_subchannel_batch(scsi_device_t *dev, int32_t lba, int count, q_subchannel_t *q)
{
    unsigned char cdb[12];
    unsigned char sense[32];

    if (!dev || dev->fd < 0 || count <= 0) {
        return 0;
    }

    /* Limit count to avoid excessive buffer sizes */
    if (count > 256) {
        count = 256;
    }

    /* Allocate buffer for all frames: 16 bytes per frame for formatted Q */
    size_t bufsize = (size_t)count * 16;
    unsigned char *buf = malloc(bufsize);
    if (!buf) {
        return 0;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_CD;
    cdb[1] = 0x00;            /* Any sector type */
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8) & 0xFF;
    cdb[5] = lba & 0xFF;
    cdb[6] = (count >> 16) & 0xFF;  /* Transfer length MSB */
    cdb[7] = (count >> 8) & 0xFF;
    cdb[8] = count & 0xFF;          /* Transfer length LSB */
    cdb[9] = 0x00;            /* No main channel data */
    cdb[10] = 0x02;           /* Subchannel = 2 (formatted Q, 16 bytes per sector) */

    memset(buf, 0, bufsize);
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, bufsize, sense, sizeof(sense)) < 0) {
        free(buf);
        return 0;
    }

    /* Decode each frame */
    for (int i = 0; i < count; i++) {
        decode_q_buffer(&buf[i * 16], &q[i]);
    }

    free(buf);
    return count;
}

/*
 * Read ISRC for a specific track using READ SUB-CHANNEL command
 * (High-level interface - kept for compatibility)
 *
 * CDB format for READ SUB-CHANNEL (42h):
 *   Byte 0: 42h (opcode)
 *   Byte 1: reserved
 *   Byte 2: bit 6 = MSF (0=LBA), bit 5 = SubQ (1=return Q subchannel)
 *   Byte 3: Sub-channel data format (03h = ISRC)
 *   Byte 4-5: reserved
 *   Byte 6: Track number
 *   Byte 7-8: Allocation length
 *   Byte 9: Control
 */
bool scsi_read_isrc(scsi_device_t *dev, int track, char *isrc)
{
    unsigned char cdb[10];
    unsigned char buf[24];  /* Response is 24 bytes for ISRC */
    unsigned char sense[32];

    isrc[0] = '\0';

    if (!dev || dev->fd < 0 || track < 1 || track > 99) {
        return false;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_SUBCHANNEL;
    cdb[2] = 0x40;           /* SubQ = 1, return Q subchannel data */
    cdb[3] = SUB_Q_ISRC;     /* Data format: ISRC */
    cdb[6] = track;          /* Track number */
    cdb[7] = 0;              /* Allocation length MSB */
    cdb[8] = sizeof(buf);    /* Allocation length LSB */

    memset(buf, 0, sizeof(buf));
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf), sense, sizeof(sense)) < 0) {
        return false;
    }

    /* Check TCVAL bit (Track Code Valid) at byte 8, bit 7 */
    if (!(buf[8] & 0x80)) {
        /* ISRC not valid for this track */
        return false;
    }

    /* ISRC is in bytes 9-20 as ASCII */
    /* Check if it's all zeros or spaces */
    bool valid = false;
    for (int i = 9; i < 21; i++) {
        if (buf[i] != 0 && buf[i] != ' ' && buf[i] != '0') {
            valid = true;
            break;
        }
    }

    if (!valid) {
        return false;
    }

    /* Copy ISRC (12 characters) */
    memcpy(isrc, &buf[9], 12);
    isrc[12] = '\0';

    return true;
}

/*
 * Read MCN using READ SUB-CHANNEL command
 * (High-level interface - kept for compatibility)
 *
 * Same CDB format but with data format 02h for MCN
 */
bool scsi_read_mcn(scsi_device_t *dev, char *mcn)
{
    unsigned char cdb[10];
    unsigned char buf[24];  /* Response is 24 bytes for MCN */
    unsigned char sense[32];

    mcn[0] = '\0';

    if (!dev || dev->fd < 0) {
        return false;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_SUBCHANNEL;
    cdb[2] = 0x40;           /* SubQ = 1 */
    cdb[3] = SUB_Q_MCN;      /* Data format: MCN */
    cdb[6] = 0;              /* Track (not used for MCN) */
    cdb[7] = 0;              /* Allocation length MSB */
    cdb[8] = sizeof(buf);    /* Allocation length LSB */

    memset(buf, 0, sizeof(buf));
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf), sense, sizeof(sense)) < 0) {
        return false;
    }

    /* Check MCVAL bit (MCN Valid) at byte 8, bit 7 */
    if (!(buf[8] & 0x80)) {
        /* MCN not valid */
        return false;
    }

    /* MCN is in bytes 9-21 as ASCII (13 characters) */
    /* Check if it's all zeros */
    bool valid = false;
    for (int i = 9; i < 22; i++) {
        if (buf[i] != 0 && buf[i] != ' ' && buf[i] != '0') {
            valid = true;
            break;
        }
    }

    if (!valid) {
        return false;
    }

    /* Copy MCN (13 characters) */
    memcpy(mcn, &buf[9], 13);
    mcn[13] = '\0';

    return true;
}

/*
 * Read TOC to get track control bytes
 * Uses READ TOC command (0x43) format 0 (TOC)
 *
 * CDB format:
 *   Byte 0: 0x43 (opcode)
 *   Byte 1: bit 1 = MSF (0=LBA)
 *   Byte 2: Format (0 = TOC)
 *   Byte 6: Track/session number (0 = all)
 *   Byte 7-8: Allocation length
 */
bool scsi_read_toc_control(scsi_device_t *dev, int *first_track, int *last_track,
                           uint8_t *control)
{
    unsigned char cdb[10];
    unsigned char buf[804];  /* Max: 4 header + 100 tracks * 8 bytes */
    unsigned char sense[32];

    if (!dev || dev->fd < 0) {
        return false;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x43;            /* READ TOC */
    cdb[1] = 0x00;            /* LBA format */
    cdb[2] = 0x00;            /* Format 0 = TOC */
    cdb[6] = 0;               /* Starting track */
    cdb[7] = (sizeof(buf) >> 8) & 0xFF;  /* Allocation length MSB */
    cdb[8] = sizeof(buf) & 0xFF;         /* Allocation length LSB */

    memset(buf, 0, sizeof(buf));
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf), sense, sizeof(sense)) < 0) {
        return false;
    }

    /* Parse TOC header */
    /* Bytes 0-1: TOC data length (excluding these 2 bytes) */
    /* Byte 2: First track number */
    /* Byte 3: Last track number */
    *first_track = buf[2];
    *last_track = buf[3];

    /* Initialize control array */
    memset(control, 0, 100);

    /* Parse track descriptors starting at byte 4 */
    /* Each descriptor is 8 bytes:
     *   Byte 0: Reserved
     *   Byte 1: ADR (upper 4 bits) | Control (lower 4 bits)
     *   Byte 2: Track number
     *   Byte 3: Reserved
     *   Bytes 4-7: Track start address (LBA)
     */
    int toc_len = ((buf[0] << 8) | buf[1]) + 2;
    int num_descriptors = (toc_len - 4) / 8;

    for (int i = 0; i < num_descriptors; i++) {
        int offset = 4 + i * 8;
        int track_num = buf[offset + 2];
        uint8_t ctrl = buf[offset + 1] & 0x0F;  /* Control is lower 4 bits */

        if (track_num < 100) {
            control[track_num] = ctrl;
        }
    }

    return true;
}

#endif /* PLATFORM_LINUX */
