/*
 * mbdiscid - Disc ID calculator
 * device.c - Device handling using libdiscid
 */

#include "device.h"
#include "toc.h"
#include "isrc.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <discid/discid.h>

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
 * Read TOC from device using libdiscid
 */
int device_read_toc(const char *device, toc_t *toc, int verbosity)
{
    DiscId *disc = discid_new();
    if (!disc) {
        return EX_SOFTWARE;
    }
    
    /* Normalize device path (e.g., /dev/diskN -> /dev/rdiskN on macOS) */
    char *dev_path = device_normalize_path(device);
    
    verbose(1, verbosity, "Opening device: %s", dev_path);
    
    /* Read TOC with MCN only - ISRC will be read via raw SCSI later */
    int result = discid_read_sparse(disc, dev_path, DISCID_FEATURE_MCN);
    if (!result) {
        const char *err = discid_get_error_msg(disc);
        error("cannot read disc: %s", err ? err : "unknown error");
        discid_free(disc);
        free(dev_path);
        return EX_IOERR;
    }
    
    free(dev_path);
    
    toc_init(toc);
    
    toc->first_track = discid_get_first_track_num(disc);
    toc->last_track = discid_get_last_track_num(disc);
    toc->track_count = toc->last_track - toc->first_track + 1;
    
    /* Get leadout (sectors includes +150 pregap, convert to raw LBA) */
    toc->leadout = discid_get_sectors(disc) - PREGAP_FRAMES;
    
    verbose(2, verbosity, "TOC: first=%d last=%d leadout=%d",
            toc->first_track, toc->last_track, toc->leadout);
    
    /* Read track information */
    int audio_count = 0;
    int data_count = 0;
    
    /* Read track control bytes via SCSI for proper type detection */
    uint8_t track_control[100] = {0};
    bool have_control = false;
    int scsi_first = 0, scsi_last = 0;
    scsi_device_t *scsi = scsi_open(device);
    if (scsi) {
        have_control = scsi_read_toc_control(scsi, &scsi_first, &scsi_last, track_control);
        if (have_control) {
            verbose(2, verbosity, "SCSI TOC: first=%d last=%d", scsi_first, scsi_last);
        }
        scsi_close(scsi);
    }
    
    /* Check for additional data tracks beyond libdiscid's range (Enhanced CD) */
    int additional_data_tracks = 0;
    if (have_control && scsi_last > toc->last_track) {
        for (int t = toc->last_track + 1; t <= scsi_last; t++) {
            if (track_control[t] & 0x04) {
                additional_data_tracks++;
                verbose(2, verbosity, "Track %d: data (from SCSI TOC, not in libdiscid)", t);
            }
        }
    }
    
    for (int i = 0; i < toc->track_count; i++) {
        int track_num = toc->first_track + i;
        
        toc->tracks[i].number = track_num;
        /* Offset from libdiscid includes +150, convert to raw LBA */
        toc->tracks[i].offset = discid_get_track_offset(disc, track_num) - PREGAP_FRAMES;
        toc->tracks[i].length = discid_get_track_length(disc, track_num);
        
        /* Determine track type from control byte */
        /* Control bit 2 (0x04): 0 = audio, 1 = data */
        if (have_control && (track_control[track_num] & 0x04)) {
            toc->tracks[i].type = TRACK_TYPE_DATA;
            data_count++;
        } else {
            toc->tracks[i].type = TRACK_TYPE_AUDIO;
            audio_count++;
        }
        
        /* ISRC will be read via raw SCSI later - leave empty for now */
        toc->tracks[i].isrc[0] = '\0';
        
        verbose(2, verbosity, "Track %d: offset=%d length=%d type=%s",
                track_num, toc->tracks[i].offset, toc->tracks[i].length,
                toc->tracks[i].type == TRACK_TYPE_DATA ? "data" : "audio");
    }
    
    toc->audio_count = audio_count;
    toc->data_count = data_count + additional_data_tracks;
    
    /* For Enhanced CDs (audio tracks followed by data track), 
     * audio_leadout should be end of last audio track, not disc leadout */
    if (additional_data_tracks > 0 && audio_count > 0 && data_count == 0) {
        /* All tracks from libdiscid are audio, data track(s) at end = Enhanced CD */
        /* audio_leadout is end of last audio track = start of first data track */
        /* The libdiscid leadout is actually the audio leadout for Enhanced CDs */
        toc->audio_leadout = toc->leadout;
        verbose(2, verbosity, "Enhanced CD detected: %d audio tracks + %d data tracks",
                audio_count, additional_data_tracks);
    } else {
        toc->audio_leadout = toc->leadout;
    }
    
    discid_free(disc);
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
        verbose(1, verbosity, "MCN: %s", mcn);
    } else {
        mcn[0] = '\0';
        verbose(1, verbosity, "No MCN found");
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
 * TODO: Implement using libcdio or raw SCSI commands
 */
int device_read_cdtext(const char *device, cdtext_t *cdtext, int verbosity)
{
    (void)device;
    (void)verbosity;
    
    /* Initialize empty CD-Text */
    memset(cdtext, 0, sizeof(*cdtext));
    
    /* CD-Text reading not yet implemented */
    /* TODO: Implement using libcdio or raw SCSI commands */
    
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
