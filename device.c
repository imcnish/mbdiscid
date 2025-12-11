/*
 * mbdiscid - Disc ID calculator
 * device.c - Device handling using libdiscid
 */

#include "device.h"
#include "toc.h"
#include "isrc.h"
#include "cdtext.h"
#include "scsi.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <discid/discid.h>

#ifdef PLATFORM_MACOS
#include <sys/ioctl.h>
#include <IOKit/storage/IOCDMediaBSDClient.h>

/*
 * Full TOC descriptor structure for parsing
 */
typedef struct {
    uint8_t session;
    uint8_t ctrl_adr;
    uint8_t tno;
    uint8_t point;
    uint8_t min;
    uint8_t sec;
    uint8_t frame;
    uint8_t zero;
    uint8_t pmin;
    uint8_t psec;
    uint8_t pframe;
} full_toc_desc_t;

/*
 * Read Full TOC using BSD ioctl (macOS only)
 * Uses Full TOC format (0x02) to get tracks from all sessions (needed for Enhanced CDs)
 *
 * Populates:
 *   - first_track, last_track: track range
 *   - control[]: control nibble for each track
 *   - session[]: session number for each track (NEW)
 *   - offsets[]: LBA offset for each track (NEW)
 *   - session_leadouts[]: leadout LBA for each session (NEW)
 *   - last_session: highest session number
 *
 * Returns true on success, false on error
 */
static bool read_full_toc_ioctl(const char *device, int *first_track,
                                 int *last_track, uint8_t *control,
                                 uint8_t *session, int32_t *offsets,
                                 int32_t *session_leadouts, int *last_session)
{
    int fd = open(device, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return false;

    /* Buffer for Full TOC: 4-byte header + up to 100 descriptors (11 bytes each) */
    uint8_t buf[1104];
    dk_cd_read_toc_t toc_req = {0};
    toc_req.format = kCDTOCFormatTOC;  /* 0x02 = Full TOC */
    toc_req.buffer = buf;
    toc_req.bufferLength = sizeof(buf);

    if (ioctl(fd, DKIOCCDREADTOC, &toc_req) < 0) {
        close(fd);
        return false;
    }
    close(fd);

    /* Parse Full TOC header */
    uint16_t toc_len = (buf[0] << 8) | buf[1];

    /* Initialize */
    memset(control, 0, 100);
    memset(session, 0, 100);
    memset(offsets, 0, 100 * sizeof(int32_t));
    memset(session_leadouts, 0, 10 * sizeof(int32_t));  /* Max 10 sessions */
    *first_track = 99;
    *last_track = 1;
    *last_session = 1;

    /* Parse Full TOC descriptors (11 bytes each, starting at byte 4) */
    int desc_count = (toc_len - 2) / 11;
    for (int i = 0; i < desc_count && i < 100; i++) {
        full_toc_desc_t *desc = (full_toc_desc_t *)&buf[4 + i * 11];
        uint8_t point = desc->point;
        uint8_t sess = desc->session;

        if (sess > *last_session && sess <= 99) {
            *last_session = sess;
        }

        /* Track entries have POINT = 1-99 */
        if (point >= 1 && point <= 99) {
            control[point] = desc->ctrl_adr & 0x0F;  /* CONTROL is low nibble */
            session[point] = sess;
            /* Convert MSF to LBA: (M*60 + S)*75 + F - 150 */
            int32_t lba = ((int32_t)desc->pmin * 60 + desc->psec) * 75 + desc->pframe - 150;
            offsets[point] = lba;

            if (point < *first_track) *first_track = point;
            if (point > *last_track) *last_track = point;
        }
        /* A0 entry: PMIN contains first track of session */
        else if (point == 0xA0) {
            uint8_t session_first = desc->pmin;
            if (session_first >= 1 && session_first <= 99) {
                if (session_first < *first_track) *first_track = session_first;
            }
        }
        /* A1 entry: PMIN contains last track of session */
        else if (point == 0xA1) {
            uint8_t session_last = desc->pmin;
            if (session_last >= 1 && session_last <= 99) {
                if (session_last > *last_track) *last_track = session_last;
            }
        }
        /* A2 entry: leadout position for this session */
        else if (point == 0xA2) {
            int32_t leadout_lba = ((int32_t)desc->pmin * 60 + desc->psec) * 75 + desc->pframe - 150;
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
 * Read raw CD-Text data using BSD ioctl (macOS only)
 * Uses DKIOCCDREADTOC with kCDTOCFormatText format
 *
 * Returns allocated buffer in *data (caller must free), length in *len
 * Returns true on success, false on error or no CD-Text present
 */
static bool read_cdtext_ioctl(const char *device, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;

    int fd = open(device, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return false;

    /* CD-Text can be up to ~16KB (255 packs * 18 bytes each, plus overhead) */
    uint8_t buf[16384];
    dk_cd_read_toc_t toc_req = {0};
    toc_req.format = 5;  /* MMC format 5 = CD-Text (kCDTOCFormatText may not be defined) */
    toc_req.buffer = buf;
    toc_req.bufferLength = sizeof(buf);

    if (ioctl(fd, DKIOCCDREADTOC, &toc_req) < 0) {
        close(fd);
        return false;
    }
    close(fd);

    /* Parse header: first 2 bytes are big-endian length */
    uint16_t cdtext_len = (buf[0] << 8) | buf[1];
    if (cdtext_len < 2) {
        return false;  /* No CD-Text data */
    }

    /* Total data is cdtext_len + 2 (for length field itself) */
    /* But actual packs start at byte 4 (after 2-byte length + 2 reserved bytes) */
    size_t total_len = cdtext_len + 2;
    if (total_len <= 4) {
        return false;  /* No actual CD-Text packs */
    }

    /* Copy just the CD-Text packs (skip 4-byte header) */
    size_t pack_len = total_len - 4;
    *data = xmalloc(pack_len);
    memcpy(*data, buf + 4, pack_len);
    *len = pack_len;

    return true;
}
#endif

/*
 * Normalize device path for platform
 * On macOS: convert /dev/diskN to /dev/rdiskN (raw device)
 */
char *device_normalize_path(const char *device)
{
    if (!device)
        return NULL;

#ifdef PLATFORM_MACOS
    /* Check if it's /dev/diskN (not already /dev/rdiskN) */
    if (strncmp(device, "/dev/disk", 9) == 0 && device[9] != '\0') {
        /* Check it's not already raw */
        if (strncmp(device, "/dev/rdisk", 10) != 0) {
            /* Convert /dev/diskN to /dev/rdiskN */
            size_t len = strlen(device) + 2;  /* +1 for 'r', +1 for null */
            char *raw = xmalloc(len);
            snprintf(raw, len, "/dev/r%s", device + 5);  /* Skip "/dev/" */
            return raw;
        }
    }
#endif

    return xstrdup(device);
}

/*
 * Get default device for platform
 */
const char *device_get_default(void)
{
    return discid_get_default_device();
}

/*
 * Read TOC from device using libdiscid + SCSI/ioctl for full TOC
 */
int device_read_toc(const char *device, toc_t *toc, int verbosity)
{
    DiscId *disc = discid_new();
    if (!disc) {
        return EX_SOFTWARE;
    }

    /* Normalize device path (e.g., /dev/diskN -> /dev/rdiskN on macOS) */
    char *dev_path = device_normalize_path(device);

    verbose(1, verbosity, "device: opening %s", dev_path);

    /* Read TOC with MCN only - ISRC will be read via raw SCSI later */
    int result = discid_read_sparse(disc, dev_path, DISCID_FEATURE_MCN);
    if (!result) {
        const char *err = discid_get_error_msg(disc);
        error("cannot read disc: %s", err ? err : "unknown error");
        discid_free(disc);
        free(dev_path);
        return EX_IOERR;
    }

    toc_init(toc);

    /* Get basic TOC info from libdiscid */
    int libdiscid_first = discid_get_first_track_num(disc);
    int libdiscid_last = discid_get_last_track_num(disc);
    int32_t libdiscid_leadout = discid_get_sectors(disc) - PREGAP_FRAMES;

    verbose(2, verbosity, "toc: libdiscid reports tracks %d-%d, leadout %d",
            libdiscid_first, libdiscid_last, libdiscid_leadout);

    /* Read Full TOC via SCSI/ioctl for complete track info including multi-session */
    uint8_t track_control[100] = {0};
    uint8_t track_session[100] = {0};
    int32_t track_offsets[100] = {0};
    int32_t session_leadouts[10] = {0};
    int scsi_first = 0, scsi_last = 0;
    int last_session = 1;
    bool have_full_toc = false;

#ifdef PLATFORM_MACOS
    /* On macOS, use BSD ioctl */
    have_full_toc = read_full_toc_ioctl(device, &scsi_first, &scsi_last,
                                         track_control, track_session, track_offsets,
                                         session_leadouts, &last_session);
    if (have_full_toc) {
        verbose(2, verbosity, "toc: full TOC reports tracks %d-%d, %d session(s)",
                scsi_first, scsi_last, last_session);
    }
#else
    /* On Linux, use SCSI Full TOC (format 2) */
    scsi_device_t *scsi = scsi_open(device);
    if (scsi) {
        have_full_toc = scsi_read_full_toc(scsi, &scsi_first, &scsi_last,
                                            track_control, track_session, track_offsets,
                                            session_leadouts, &last_session);
        if (have_full_toc) {
            verbose(2, verbosity, "toc: full TOC reports tracks %d-%d, %d session(s)",
                    scsi_first, scsi_last, last_session);
        }
        scsi_close(scsi);
    }
#endif

    /* Determine actual track range (use SCSI if available and has more tracks) */
    int actual_first = libdiscid_first;
    int actual_last = (have_full_toc && scsi_last > libdiscid_last) ? scsi_last : libdiscid_last;

    toc->first_track = actual_first;
    toc->last_track = actual_last;
    toc->last_session = last_session;

    /* Build track array with all tracks */
    int audio_count = 0;
    int data_count = 0;
    int track_idx = 0;

    for (int t = actual_first; t <= actual_last; t++) {
        track_t *track = &toc->tracks[track_idx];
        track->number = t;

        /* Get offset - prefer SCSI if available, fall back to libdiscid */
        if (have_full_toc && track_offsets[t] != 0) {
            track->offset = track_offsets[t];
        } else if (t >= libdiscid_first && t <= libdiscid_last) {
            track->offset = discid_get_track_offset(disc, t) - PREGAP_FRAMES;
        } else {
            /* Track not in libdiscid range - must use SCSI offset */
            track->offset = track_offsets[t];
        }

        /* Session number */
        track->session = have_full_toc ? track_session[t] : 1;
        if (track->session == 0) track->session = 1;

        /* Control nibble */
        track->control = have_full_toc ? track_control[t] : 0;

        /* Determine track type from control byte (bit 2: 0=audio, 1=data) */
        if (have_full_toc && (track_control[t] & 0x04)) {
            track->type = TRACK_TYPE_DATA;
            data_count++;
        } else {
            track->type = TRACK_TYPE_AUDIO;
            audio_count++;
        }

        /* ISRC will be read via raw SCSI later */
        track->isrc[0] = '\0';

        track_idx++;
    }

    toc->track_count = track_idx;
    toc->audio_count = audio_count;
    toc->data_count = data_count;

    /* Determine leadout */
    if (have_full_toc && session_leadouts[last_session - 1] > 0) {
        toc->leadout = session_leadouts[last_session - 1];
    } else {
        toc->leadout = libdiscid_leadout;
    }

    /* Determine audio_leadout for AccurateRip calculations */
    /* For Enhanced CDs: audio_leadout = start of first data track (end of audio session) */
    /* For other discs: audio_leadout = disc leadout */
    toc->audio_leadout = toc->leadout;

    if (last_session > 1 && have_full_toc) {
        /* Multi-session disc - audio leadout is session 1 leadout */
        if (session_leadouts[0] > 0) {
            toc->audio_leadout = session_leadouts[0];
            verbose(2, verbosity, "toc: multi-session, audio leadout = %d (session 1)",
                    toc->audio_leadout);
        }
    } else if (data_count > 0 && audio_count > 0) {
        /* Single session with mixed content - find first data track */
        for (int i = 0; i < track_idx; i++) {
            if (toc->tracks[i].type == TRACK_TYPE_DATA) {
                /* For Enhanced CD: data at end, audio_leadout = start of data track */
                if (i > 0 && toc->tracks[i-1].type == TRACK_TYPE_AUDIO) {
                    toc->audio_leadout = toc->tracks[i].offset;
                    verbose(2, verbosity, "toc: Enhanced CD layout, audio leadout = %d",
                            toc->audio_leadout);
                }
                break;
            }
        }
    }

    /* Calculate track lengths */
    for (int i = 0; i < track_idx - 1; i++) {
        toc->tracks[i].length = toc->tracks[i + 1].offset - toc->tracks[i].offset;
    }
    if (track_idx > 0) {
        toc->tracks[track_idx - 1].length = toc->leadout - toc->tracks[track_idx - 1].offset;
    }

    verbose(1, verbosity, "toc: %d tracks (%d audio, %d data)",
            toc->track_count, toc->audio_count, toc->data_count);

    for (int i = 0; i < track_idx; i++) {
        verbose(2, verbosity, "toc: track %d: session %d, offset %d, length %d, %s",
                toc->tracks[i].number, toc->tracks[i].session,
                toc->tracks[i].offset, toc->tracks[i].length,
                toc->tracks[i].type == TRACK_TYPE_DATA ? "data" : "audio");
    }

    discid_free(disc);
    free(dev_path);
    return 0;
}

/*
 * Read MCN from device
 */
int device_read_mcn(const char *device, char *mcn, int verbosity)
{
    DiscId *disc = discid_new();
    if (!disc) {
        return EX_SOFTWARE;
    }

    /* Suppress stderr during MCN reading - libdiscid may emit warnings
     * for drives that don't support MCN, but per spec §6.1.1, absence
     * of optional metadata must produce no output */
    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }

    int result = discid_read_sparse(disc, device, DISCID_FEATURE_MCN);

    /* Restore stderr */
    if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }

    if (!result) {
        discid_free(disc);
        return EX_IOERR;
    }

    const char *disc_mcn = discid_get_mcn(disc);
    if (disc_mcn && strlen(disc_mcn) == MCN_LENGTH && is_valid_mcn(disc_mcn)) {
        strncpy(mcn, disc_mcn, MCN_LENGTH);
        mcn[MCN_LENGTH] = '\0';
        verbose(1, verbosity, "mcn: %s", mcn);
    } else {
        mcn[0] = '\0';
        verbose(1, verbosity, "mcn: not present");
    }

    discid_free(disc);
    return 0;
}

/*
 * Read ISRCs from device using spec §5 algorithm
 */
int device_read_isrc(const char *device, toc_t *toc, int verbosity)
{
    int result = isrc_read_disc(toc, device, verbosity);

    /* isrc_read_disc returns -1 on error, >= 0 for count of ISRCs found */
    if (result < 0) {
        return EX_IOERR;
    }

    return 0;
}

/*
 * Read CD-Text from device
 * On macOS: uses BSD ioctl (DKIOCCDREADTOC with kCDTOCFormatText)
 * On Linux: uses SCSI READ TOC format 5
 */
int device_read_cdtext(const char *device, cdtext_t *cdtext, int verbosity)
{
    /* Initialize empty CD-Text */
    memset(cdtext, 0, sizeof(*cdtext));

    /* Normalize device path */
    char *dev_path = device_normalize_path(device);

    verbose(1, verbosity, "cdtext: reading from %s", dev_path);

    uint8_t *raw_data = NULL;
    size_t raw_len = 0;

#ifdef PLATFORM_MACOS
    /* On macOS, use BSD ioctl to avoid SCSI/Disk Arbitration complexity */
    if (!read_cdtext_ioctl(dev_path, &raw_data, &raw_len)) {
        verbose(1, verbosity, "cdtext: not present");
        free(dev_path);
        return 0;  /* Not an error - CD-Text is optional */
    }
#else
    /* On Linux, use SCSI (works fine without exclusive access issues) */
    scsi_device_t *scsi = scsi_open(dev_path);
    if (!scsi) {
        verbose(1, verbosity, "cdtext: failed to open device");
        free(dev_path);
        return 0;  /* Not an error - CD-Text is optional */
    }

    if (!scsi_read_cdtext_raw(scsi, &raw_data, &raw_len)) {
        verbose(1, verbosity, "cdtext: not present");
        scsi_close(scsi);
        free(dev_path);
        return 0;  /* Not an error - CD-Text is optional */
    }

    scsi_close(scsi);
#endif

    free(dev_path);

    verbose(2, verbosity, "cdtext: %zu bytes of raw data", raw_len);

    /* Parse CD-Text packs */
    int result = cdtext_parse(raw_data, raw_len, cdtext, verbosity);

    free(raw_data);

    if (result < 0) {
        verbose(1, verbosity, "cdtext: parsing failed");
        return 0;  /* Not an error - treat parse failure as "no CD-Text" */
    }

    return 0;
}

/*
 * Read all disc information
 */
int device_read_disc(const char *device, disc_info_t *disc, int flags, int verbosity)
{
    int ret;

    memset(disc, 0, sizeof(*disc));

    /* Normalize device path (e.g., /dev/diskN -> /dev/rdiskN on macOS) */
    char *dev_path = device_normalize_path(device);

    /* Read TOC (always required) */
    ret = device_read_toc(dev_path, &disc->toc, verbosity);
    if (ret != 0) {
        free(dev_path);
        return ret;
    }

    /* Determine disc type */
    disc->type = toc_get_disc_type(&disc->toc);

    /* Read MCN if requested */
    if (flags & READ_MCN) {
        ret = device_read_mcn(dev_path, disc->ids.mcn, verbosity);
        if (ret == 0 && disc->ids.mcn[0] != '\0') {
            disc->has_mcn = true;
        }
    }

    /* Read ISRCs if requested */
    if (flags & READ_ISRC) {
        ret = device_read_isrc(dev_path, &disc->toc, verbosity);
        if (ret == 0) {
            /* Check if any valid ISRCs were found */
            for (int i = 0; i < disc->toc.track_count; i++) {
                if (disc->toc.tracks[i].isrc[0] != '\0') {
                    disc->has_isrc = true;
                    break;
                }
            }
        }
    }

    /* Read CD-Text if requested */
    if (flags & READ_CDTEXT) {
        ret = device_read_cdtext(dev_path, &disc->cdtext, verbosity);
        if (ret == 0) {
            /* Check if we got any CD-Text */
            if (disc->cdtext.album.album || disc->cdtext.album.albumartist) {
                disc->has_cdtext = true;
            } else {
                /* Also check for any track-level text */
                for (int i = 0; i < disc->cdtext.track_count; i++) {
                    if (disc->cdtext.tracks[i].title || disc->cdtext.tracks[i].artist) {
                        disc->has_cdtext = true;
                        break;
                    }
                }
            }
        }
    }

    free(dev_path);
    return 0;
}

/*
 * List optical drives
 * Per spec §8.7: output exactly as produced by system tools
 * If no drives, output is empty with EX_OK
 */
int device_list_drives(void)
{
#ifdef PLATFORM_MACOS
    /* §8.7.3: Print drutil output exactly as produced */
    int ret = system("drutil status 2>/dev/null");
    (void)ret;  /* Ignore return - empty output for no drives per §8.7.1 */
#else
    /* §8.7.2: Print lsblk output exactly as produced */
    int ret = system("lsblk -dp -I 11 -o NAME,VENDOR,MODEL,REV 2>/dev/null");
    (void)ret;  /* Ignore return - empty output for no drives per §8.7.1 */
#endif

    return EX_OK;
}

/*
 * Free CD-Text structure
 */
void cdtext_free(cdtext_t *cdtext)
{
    if (!cdtext)
        return;

    free(cdtext->album.album);
    free(cdtext->album.albumartist);
    free(cdtext->album.genre);
    free(cdtext->album.lyricist);
    free(cdtext->album.composer);
    free(cdtext->album.arranger);
    free(cdtext->album.comment);

    for (int i = 0; i < cdtext->track_count; i++) {
        free(cdtext->tracks[i].title);
        free(cdtext->tracks[i].artist);
        free(cdtext->tracks[i].lyricist);
        free(cdtext->tracks[i].composer);
        free(cdtext->tracks[i].arranger);
        free(cdtext->tracks[i].comment);
    }

    memset(cdtext, 0, sizeof(*cdtext));
}
