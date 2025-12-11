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
#define READ_TOC        0x43

/* Subchannel data format codes */
#define SUB_Q_CHANNEL_DATA  0x00
#define SUB_Q_MCN           0x02
#define SUB_Q_ISRC          0x03

/* READ TOC format codes */
#define TOC_FORMAT_TOC      0x00
#define TOC_FORMAT_FULL     0x02
#define TOC_FORMAT_CDTEXT   0x05

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
 * Read TOC to get track control bytes (basic TOC format 0)
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
    cdb[0] = READ_TOC;
    cdb[1] = 0x00;            /* LBA format */
    cdb[2] = TOC_FORMAT_TOC;  /* Format 0 = TOC */
    cdb[6] = 0;               /* Starting track */
    cdb[7] = (sizeof(buf) >> 8) & 0xFF;  /* Allocation length MSB */
    cdb[8] = sizeof(buf) & 0xFF;         /* Allocation length LSB */

    memset(buf, 0, sizeof(buf));
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf), sense, sizeof(sense)) < 0) {
        return false;
    }

    /* Parse TOC header */
    *first_track = buf[2];
    *last_track = buf[3];

    /* Initialize control array */
    memset(control, 0, 100);

    /* Parse track descriptors starting at byte 4 */
    int toc_len = ((buf[0] << 8) | buf[1]) + 2;
    int num_descriptors = (toc_len - 4) / 8;

    for (int i = 0; i < num_descriptors; i++) {
        int offset = 4 + i * 8;
        int track_num = buf[offset + 2];
        uint8_t ctrl = buf[offset + 1] & 0x0F;

        if (track_num < 100) {
            control[track_num] = ctrl;
        }
    }

    return true;
}

/*
 * Read Full TOC (format 2) to get complete track info including multi-session
 *
 * Full TOC format returns 11-byte descriptors:
 *   Byte 0: Session number
 *   Byte 1: ADR (high nibble) | CONTROL (low nibble)
 *   Byte 2: TNO (always 0 in Full TOC)
 *   Byte 3: POINT (track number or special: A0, A1, A2)
 *   Bytes 4-6: MIN, SEC, FRAME (running time in track)
 *   Byte 7: Zero
 *   Bytes 8-10: PMIN, PSEC, PFRAME (start position for tracks, or special data)
 *
 * Special POINT values:
 *   A0: PMIN = first track number in session
 *   A1: PMIN = last track number in session
 *   A2: PMIN/PSEC/PFRAME = leadout position for session
 *
 * Populates:
 *   - first_track, last_track: track range
 *   - control[]: control nibble for each track (index 1-99)
 *   - session[]: session number for each track (index 1-99)
 *   - offsets[]: LBA offset for each track (index 1-99)
 *   - session_leadouts[]: leadout LBA for each session (index 0-9 for sessions 1-10)
 *   - last_session: highest session number
 *
 * Returns true on success, false on error
 */
bool scsi_read_full_toc(scsi_device_t *dev, int *first_track, int *last_track,
                        uint8_t *control, uint8_t *session, int32_t *offsets,
                        int32_t *session_leadouts, int *last_session)
{
    unsigned char cdb[10];
    unsigned char buf[1104];  /* 4-byte header + up to 100 descriptors * 11 bytes */
    unsigned char sense[32];

    if (!dev || dev->fd < 0) {
        return false;
    }

    /* Initialize outputs */
    memset(control, 0, 100);
    memset(session, 0, 100);
    memset(offsets, 0, 100 * sizeof(int32_t));
    memset(session_leadouts, 0, 10 * sizeof(int32_t));
    *first_track = 99;
    *last_track = 1;
    *last_session = 1;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_TOC;
    cdb[1] = 0x02;            /* MSF format bit (we'll convert to LBA) */
    cdb[2] = TOC_FORMAT_FULL; /* Format 2 = Full TOC */
    cdb[6] = 1;               /* Starting session */
    cdb[7] = (sizeof(buf) >> 8) & 0xFF;
    cdb[8] = sizeof(buf) & 0xFF;

    memset(buf, 0, sizeof(buf));
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf), sense, sizeof(sense)) < 0) {
        return false;
    }

    /* Parse Full TOC header */
    uint16_t toc_len = ((uint16_t)buf[0] << 8) | buf[1];

    if (toc_len < 2) {
        return false;
    }

    /* buf[2] = first session, buf[3] = last session (from header) */
    *last_session = buf[3];
    if (*last_session < 1) *last_session = 1;
    if (*last_session > 10) *last_session = 10;

    /* Parse Full TOC descriptors (11 bytes each, starting at byte 4) */
    int desc_count = (toc_len - 2) / 11;

    for (int i = 0; i < desc_count && i < 100; i++) {
        int offset = 4 + i * 11;

        uint8_t sess = buf[offset + 0];
        uint8_t ctrl_adr = buf[offset + 1];
        uint8_t point = buf[offset + 3];
        uint8_t pmin = buf[offset + 8];
        uint8_t psec = buf[offset + 9];
        uint8_t pframe = buf[offset + 10];

        /* Track session for last_session calculation */
        if (sess > *last_session && sess <= 10) {
            *last_session = sess;
        }

        /* Track entries have POINT = 1-99 */
        if (point >= 1 && point <= 99) {
            control[point] = ctrl_adr & 0x0F;  /* CONTROL is low nibble */
            session[point] = sess;

            /* Convert MSF to LBA: (M*60 + S)*75 + F - 150 */
            int32_t lba = ((int32_t)pmin * 60 + psec) * 75 + pframe - 150;
            offsets[point] = lba;

            if (point < *first_track) *first_track = point;
            if (point > *last_track) *last_track = point;
        }
        /* A0 entry: PMIN contains first track of session */
        else if (point == 0xA0) {
            uint8_t session_first = pmin;
            if (session_first >= 1 && session_first <= 99) {
                if (session_first < *first_track) *first_track = session_first;
            }
        }
        /* A1 entry: PMIN contains last track of session */
        else if (point == 0xA1) {
            uint8_t session_last = pmin;
            if (session_last >= 1 && session_last <= 99) {
                if (session_last > *last_track) *last_track = session_last;
            }
        }
        /* A2 entry: leadout position for this session */
        else if (point == 0xA2) {
            int32_t leadout_lba = ((int32_t)pmin * 60 + psec) * 75 + pframe - 150;
            if (sess >= 1 && sess <= 10) {
                session_leadouts[sess - 1] = leadout_lba;
            }
        }
    }

    /* Validate we found something */
    if (*first_track > *last_track) {
        *first_track = 1;
        *last_track = 1;
        return false;
    }

    return true;
}

/*
 * Read raw CD-Text data using READ TOC/PMA/ATIP command (format 5)
 *
 * READ TOC command (0x43) with format 5 returns CD-Text packs:
 *   Bytes 0-1: Data length (big-endian, excludes these 2 bytes)
 *   Bytes 2-3: Reserved
 *   Bytes 4+:  CD-Text packs (18 bytes each)
 */
bool scsi_read_cdtext_raw(scsi_device_t *dev, uint8_t **data, size_t *len)
{
    unsigned char cdb[10];
    unsigned char sense[32];

    *data = NULL;
    *len = 0;

    if (!dev || dev->fd < 0) {
        return false;
    }

    /* First, query with minimal buffer to get actual length */
    unsigned char header[4];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_TOC;
    cdb[1] = 0x00;            /* LBA format (not relevant for CD-Text) */
    cdb[2] = TOC_FORMAT_CDTEXT;  /* Format 5 = CD-Text */
    cdb[6] = 0;               /* Track/session (0 for CD-Text) */
    cdb[7] = 0;               /* Allocation length MSB */
    cdb[8] = 4;               /* Allocation length LSB = header only */

    memset(header, 0, sizeof(header));
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), header, sizeof(header), sense, sizeof(sense)) < 0) {
        snprintf(dev->error, sizeof(dev->error), "CD-Text query failed");
        return false;
    }

    /* Parse data length from header (big-endian) */
    uint16_t data_len = ((uint16_t)header[0] << 8) | header[1];

    /* data_len excludes the 2-byte length field itself */
    /* Total response size = data_len + 2 */
    /* CD-Text packs start at byte 4, so pack data length = data_len - 2 */

    if (data_len < 2) {
        /* No CD-Text data (just header, no packs) */
        return false;
    }

    size_t total_len = data_len + 2;
    size_t pack_data_len = data_len - 2;

    /* Sanity check: must be multiple of 18-byte packs */
    if (pack_data_len % 18 != 0) {
        snprintf(dev->error, sizeof(dev->error),
                 "CD-Text data length %zu not multiple of 18", pack_data_len);
        return false;
    }

    /* Sanity check: reasonable maximum (255 packs = 4590 bytes) */
    if (total_len > 8192) {
        snprintf(dev->error, sizeof(dev->error),
                 "CD-Text data length %zu exceeds maximum", total_len);
        return false;
    }

    /* Allocate buffer and read full data */
    unsigned char *buf = malloc(total_len);
    if (!buf) {
        return false;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_TOC;
    cdb[1] = 0x00;
    cdb[2] = TOC_FORMAT_CDTEXT;
    cdb[6] = 0;
    cdb[7] = (total_len >> 8) & 0xFF;
    cdb[8] = total_len & 0xFF;

    memset(buf, 0, total_len);
    memset(sense, 0, sizeof(sense));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, total_len, sense, sizeof(sense)) < 0) {
        free(buf);
        snprintf(dev->error, sizeof(dev->error), "CD-Text read failed");
        return false;
    }

    /* Return pack data (skip 4-byte header) */
    *data = malloc(pack_data_len);
    if (!*data) {
        free(buf);
        return false;
    }

    memcpy(*data, buf + 4, pack_data_len);
    *len = pack_data_len;

    free(buf);
    return true;
}

#endif /* PLATFORM_LINUX */
