/*
 * mbdiscid - Calculate disc IDs from CD or CDTOC data
 * Copyright (c) 2025 Ian McNish
 * SPDX-License-Identifier: MIT
 */

/* Feature test macros MUST come before any includes */
#ifdef __APPLE__
    #define _DARWIN_C_SOURCE
#else
    #define _XOPEN_SOURCE 500
    #define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sysexits.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <fcntl.h>
#include <discid/discid.h>

/* Platform-specific CD reading via SCSI */
#ifdef __linux__
#include <sys/ioctl.h>
#include <scsi/sg.h>
#endif

#ifdef __APPLE__
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/storage/IOCDTypes.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <AvailabilityMacros.h>
/* kIOMainPortDefault was introduced in macOS 12, use it if available */
#if defined(__MAC_12_0)
#define MBDISCID_IOKIT_PORT kIOMainPortDefault
#else
#define MBDISCID_IOKIT_PORT kIOMasterPortDefault
#endif
#endif

#define VERSION "1.0.3"

/* Platform-specific browser opening */
#ifdef __APPLE__
    #define OPEN_CMD "open"
#elif __linux__
    #define OPEN_CMD "xdg-open"
#else
    #define OPEN_CMD ""
#endif

/* Retry configuration */
#define MCN_RETRIES 3
#define RETRY_DELAY_US 100000  /* 100ms */

/* Mode flags - bit flags for multiple mode selection */
typedef enum {
    MODE_NONE        = 0,
    MODE_RAW         = 1 << 0,
    MODE_ACCURATERIP = 1 << 1,
    MODE_CATALOG     = 1 << 2,   /* MCN */
    MODE_FREEDB      = 1 << 3,
    MODE_ISRC        = 1 << 4,
    MODE_MUSICBRAINZ = 1 << 5,
    MODE_ALL         = MODE_RAW | MODE_ACCURATERIP | MODE_CATALOG | MODE_FREEDB | MODE_ISRC | MODE_MUSICBRAINZ
} disc_mode_t;

/* Action flags - bit flags for multiple action selection */
typedef enum {
    ACTION_NONE = 0,
    ACTION_ID   = 1 << 0,
    ACTION_TOC  = 1 << 1,
    ACTION_URL  = 1 << 2,
    ACTION_OPEN = 1 << 3,
    ACTION_ALL  = ACTION_ID | ACTION_TOC | ACTION_URL
} action_t;

/* Global quiet mode flag */
static int quiet_mode = 0;

/* Error printing macro that respects quiet mode */
#define PRINT_ERROR(...) do { if (!quiet_mode) fprintf(stderr, __VA_ARGS__); } while(0)

/* Mode name helper for error messages */
static const char *
mode_name(disc_mode_t mode)
{
    switch (mode) {
        case MODE_RAW:         return "Raw";
        case MODE_ACCURATERIP: return "AccurateRip";
        case MODE_CATALOG:     return "MCN";
        case MODE_FREEDB:      return "FreeDB";
        case MODE_ISRC:        return "ISRC";
        case MODE_MUSICBRAINZ: return "MusicBrainz";
        default:               return "Unknown";
    }
}

/* Mode flag helper for error messages */
static char
mode_flag(disc_mode_t mode)
{
    switch (mode) {
        case MODE_RAW:         return 'R';
        case MODE_ACCURATERIP: return 'A';
        case MODE_CATALOG:     return 'C';
        case MODE_FREEDB:      return 'F';
        case MODE_ISRC:        return 'I';
        case MODE_MUSICBRAINZ: return 'M';
        case MODE_ALL:         return 'a';
        default:               return '?';
    }
}

/*
 * Suppress stderr temporarily
 * Returns saved stderr fd, or -1 on failure
 */
static int
suppress_stderr(void)
{
    fflush(stderr);
    int saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr == -1)
        return -1;

    int devnull = open("/dev/null", O_WRONLY);
    if (devnull == -1) {
        close(saved_stderr);
        return -1;
    }

    dup2(devnull, STDERR_FILENO);
    close(devnull);
    return saved_stderr;
}

/*
 * Restore stderr from saved fd
 */
static void
restore_stderr(int saved_stderr)
{
    if (saved_stderr != -1) {
        fflush(stderr);
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }
}

/*
 * Validate MCN string
 * Returns 1 if valid, 0 if invalid
 * Valid: 12-13 digit numeric string, not all zeros
 */
static int
validate_mcn(const char *mcn)
{
    if (mcn == NULL || *mcn == '\0')
        return 0;

    size_t len = strlen(mcn);
    if (len < 12 || len > 13)
        return 0;

    int all_zeros = 1;
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)mcn[i]))
            return 0;
        if (mcn[i] != '0')
            all_zeros = 0;
    }

    return !all_zeros;
}

/*
 * Validate ISRC string
 * Returns 1 if valid, 0 if invalid
 * Valid: 12 character alphanumeric string
 */
static int
validate_isrc(const char *isrc)
{
    if (isrc == NULL || *isrc == '\0')
        return 0;

    size_t len = strlen(isrc);
    if (len != 12)
        return 0;

    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)isrc[i]))
            return 0;
    }

    return 1;
}

/*
 * Calculate CDDB/FreeDB disc ID
 * offsets[0] = leadout, offsets[1..num_tracks] = track offsets
 */
static unsigned int
calculate_cddb_id(int num_tracks, int *offsets)
{
    int n = 0;

    /* Sum of digits in track start times (in seconds) */
    for (int i = 1; i <= num_tracks; i++) {
        int time_seconds = offsets[i] / 75;
        while (time_seconds > 0) {
            n += time_seconds % 10;
            time_seconds /= 10;
        }
    }

    /* Total playing time in seconds */
    int total_seconds = (offsets[0] - offsets[1]) / 75;

    /* CDDB ID formula */
    return ((n % 0xff) << 24) | (total_seconds << 8) | num_tracks;
}

/*
 * Calculate AccurateRip disc ID components
 * Uses LBA (offset - 150) for calculations
 */
static void
calculate_accuraterip_id(int num_tracks, int *offsets,
                         unsigned int *disc_id1, unsigned int *disc_id2,
                         unsigned int *cddb_id)
{
    *disc_id1 = 0;
    *disc_id2 = 0;

    /* offsets[0] = leadout, offsets[1..num_tracks] = track offsets */
    for (int i = 1; i <= num_tracks; i++) {
        int lba = offsets[i] - 150;
        *disc_id1 += lba;
        *disc_id2 += (lba ? lba : 1) * i;
    }

    /* Add leadout */
    int leadout_lba = offsets[0] - 150;
    *disc_id1 += leadout_lba;
    *disc_id2 += (leadout_lba ? leadout_lba : 1) * (num_tracks + 1);

    /* Calculate CDDB ID */
    *cddb_id = calculate_cddb_id(num_tracks, offsets);
}

/*
 * Read MCN with retries, suppressing libdiscid warnings
 * Returns allocated string on success, NULL on failure
 */
static char *
read_mcn_with_retry(const char *device)
{
    if (!discid_has_feature(DISCID_FEATURE_MCN)) {
        return NULL;
    }

    for (int attempt = 0; attempt < MCN_RETRIES; attempt++) {
        if (attempt > 0) {
            usleep(RETRY_DELAY_US);
        }

        DiscId *disc = discid_new();
        if (!disc)
            continue;

        /* Suppress libdiscid warnings */
        int saved_stderr = suppress_stderr();
        int read_ok = discid_read_sparse(disc, device, DISCID_FEATURE_READ | DISCID_FEATURE_MCN);
        restore_stderr(saved_stderr);

        if (read_ok) {
            const char *mcn = discid_get_mcn(disc);
            if (validate_mcn(mcn)) {
                char *result = strdup(mcn);
                discid_free(disc);
                return result;
            }
        }
        discid_free(disc);
    }

    return NULL;
}

/*
 * Read ISRC directly using MMC READ SUB-CHANNEL command (0x42)
 * If quick_probe is false: reads audio data first ("warmup") to improve reliability,
 * retries up to 3 times with delays
 * If quick_probe is true: single attempt without warmup (fast check for ISRC presence)
 * Returns allocated string with ISRC, or NULL if not found
 */
#ifdef __linux__
static char *
read_isrc_mmc(const char *device, int track_num, int track_lba, int quick_probe)
{
    char *result = NULL;
    int max_attempts = quick_probe ? 1 : 3;

    for (int attempt = 0; attempt < max_attempts && result == NULL; attempt++) {
        if (attempt > 0) {
            usleep(100000);  /* 100ms between attempts */
        }

        int fd = open(device, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        unsigned char sense[32];
        struct sg_io_hdr io_hdr;

        /*
         * First, read a few sectors from the track to force disc access
         * READ CD command (0xBE) - read 5 sectors of audio
         * This "audio warmup" improves ISRC read reliability on some drives
         */
        if (!quick_probe && track_lba >= 0) {
            unsigned char read_cdb[12] = {
                0xBE,                       /* READ CD */
                0x04,                       /* Expected sector type: CD-DA */
                (track_lba >> 24) & 0xFF,   /* LBA MSB */
                (track_lba >> 16) & 0xFF,
                (track_lba >> 8) & 0xFF,
                track_lba & 0xFF,           /* LBA LSB */
                0x00, 0x00, 0x05,           /* 5 sectors */
                0x10,                       /* User data only */
                0x00,                       /* No subchannel */
                0x00
            };

            /* 5 sectors * 2352 bytes = 11760 bytes */
            unsigned char *audio_buf = malloc(11760);
            if (audio_buf) {
                memset(&io_hdr, 0, sizeof(io_hdr));
                io_hdr.interface_id = 'S';
                io_hdr.cmd_len = 12;
                io_hdr.mx_sb_len = sizeof(sense);
                io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
                io_hdr.dxfer_len = 11760;
                io_hdr.dxferp = audio_buf;
                io_hdr.cmdp = read_cdb;
                io_hdr.sbp = sense;
                io_hdr.timeout = 10000;

                ioctl(fd, SG_IO, &io_hdr);  /* Ignore errors, just force I/O */
                free(audio_buf);
                usleep(50000);  /* 50ms settle time */
            }
        }

        /*
         * Now read ISRC using READ SUB-CHANNEL command (0x42)
         */
        unsigned char cdb[10] = {
            0x42,           /* READ SUB-CHANNEL */
            0x00,
            0x40,           /* SubQ bit set */
            0x03,           /* Format: ISRC */
            0x00,
            0x00,
            (unsigned char)track_num,  /* Track number */
            0x00, 0x18,     /* Allocation length: 24 bytes */
            0x00
        };

        unsigned char buf[24];
        memset(buf, 0, sizeof(buf));

        memset(&io_hdr, 0, sizeof(io_hdr));
        io_hdr.interface_id = 'S';
        io_hdr.cmd_len = 10;
        io_hdr.mx_sb_len = sizeof(sense);
        io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        io_hdr.dxfer_len = sizeof(buf);
        io_hdr.dxferp = buf;
        io_hdr.cmdp = cdb;
        io_hdr.sbp = sense;
        io_hdr.timeout = quick_probe ? 5000 : 30000;  /* 5s for probe, 30s for full */

        if (ioctl(fd, SG_IO, &io_hdr) == 0 && io_hdr.status == 0) {
            /* Check if ISRC is valid (TCVal bit 7) */
            if (buf[8] & 0x80) {
                /* ISRC is in bytes 9-20 as ASCII */
                char isrc[13];
                memcpy(isrc, buf + 9, 12);
                isrc[12] = '\0';

                if (validate_isrc(isrc)) {
                    result = strdup(isrc);
                }
            }
        }

        close(fd);
    }

    return result;
}

#else
/* macOS implementation using IOKit */
#ifdef __APPLE__

/*
 * Helper to get IOKit service from device path
 * Converts /dev/rdiskN or /dev/diskN to matching IOKit service
 */
static io_service_t
get_cd_service_from_path(const char *device)
{
    /* Extract disk number from path */
    const char *disk_part = strstr(device, "disk");
    if (!disk_part) {
        return IO_OBJECT_NULL;
    }

    /* Create matching dictionary for IOCDMedia */
    CFMutableDictionaryRef match_dict = IOServiceMatching(kIOCDMediaClass);
    if (!match_dict) {
        return IO_OBJECT_NULL;
    }

    /* Find all CD media services */
    io_iterator_t iter;
    kern_return_t kr = IOServiceGetMatchingServices(MBDISCID_IOKIT_PORT, match_dict, &iter);
    if (kr != KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }

    io_service_t service;
    io_service_t found_service = IO_OBJECT_NULL;

    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        /* Get BSD name for this service */
        CFStringRef bsd_name = IORegistryEntryCreateCFProperty(
            service, CFSTR("BSD Name"), kCFAllocatorDefault, 0);

        if (bsd_name) {
            char name_buf[64];
            if (CFStringGetCString(bsd_name, name_buf, sizeof(name_buf), kCFStringEncodingUTF8)) {
                /* Check if this matches our device */
                if (strstr(device, name_buf)) {
                    found_service = service;
                    CFRelease(bsd_name);
                    break;
                }
            }
            CFRelease(bsd_name);
        }
        IOObjectRelease(service);
    }

    IOObjectRelease(iter);
    return found_service;
}

/*
 * Read ISRC directly using IOKit SCSI commands
 * If quick_probe is false: reads audio data first ("warmup") to improve reliability,
 * retries up to 3 times with delays
 * If quick_probe is true: single attempt without warmup (fast check for ISRC presence)
 * Returns allocated string with ISRC, or NULL if not found
 */
static char *
read_isrc_mmc(const char *device, int track_num, int track_lba, int quick_probe)
{
    char *result = NULL;
    int max_attempts = quick_probe ? 1 : 3;

    /* Get IOKit service for this device */
    io_service_t service = get_cd_service_from_path(device);
    if (service == IO_OBJECT_NULL) {
        return NULL;
    }

    /* Walk up to find the IOSCSIPeripheralDeviceType05 (CD/DVD device) */
    io_service_t scsi_service = IO_OBJECT_NULL;
    io_service_t current = service;
    IOObjectRetain(current);

    while (current != IO_OBJECT_NULL) {
        if (IOObjectConformsTo(current, "IOSCSIPeripheralDeviceType05") ||
            IOObjectConformsTo(current, "IOBDServices") ||
            IOObjectConformsTo(current, "IODVDServices") ||
            IOObjectConformsTo(current, "IOCompactDiscServices")) {
            scsi_service = current;
            break;
        }

        io_service_t parent = IO_OBJECT_NULL;
        kern_return_t kr = IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent);
        IOObjectRelease(current);

        if (kr != KERN_SUCCESS) {
            current = IO_OBJECT_NULL;
            break;
        }
        current = parent;
    }

    IOObjectRelease(service);

    if (scsi_service == IO_OBJECT_NULL) {
        return NULL;
    }

    /* Create plugin interface */
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score;
    kern_return_t kr = IOCreatePlugInInterfaceForService(
        scsi_service, kIOSCSITaskDeviceUserClientTypeID,
        kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(scsi_service);

    if (kr != KERN_SUCCESS || !plugin) {
        return NULL;
    }

    /* Get SCSITaskDevice interface */
    SCSITaskDeviceInterface **taskDevice = NULL;
    HRESULT hr = (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID), (LPVOID *)&taskDevice);
    (*plugin)->Release(plugin);

    if (hr != S_OK || !taskDevice) {
        return NULL;
    }

    /* Get exclusive access */
    IOReturn ioResult = (*taskDevice)->ObtainExclusiveAccess(taskDevice);
    if (ioResult != kIOReturnSuccess) {
        (*taskDevice)->Release(taskDevice);
        return NULL;
    }

    for (int attempt = 0; attempt < max_attempts && result == NULL; attempt++) {
        if (attempt > 0) {
            usleep(100000);  /* 100ms between attempts */
        }

        SCSITaskInterface **task = NULL;
        SCSITaskStatus taskStatus;
        SCSI_Sense_Data senseData;
        UInt64 transferCount;

        /*
         * First, read a few sectors from the track to force disc access
         * READ CD command (0xBE) - read 5 sectors of audio
         * This "audio warmup" improves ISRC read reliability on some drives
         */
        if (!quick_probe && track_lba >= 0) {
            task = (*taskDevice)->CreateSCSITask(taskDevice);
            if (task) {
                unsigned char read_cdb[12] = {
                    0xBE,                       /* READ CD */
                    0x04,                       /* Expected sector type: CD-DA */
                    (track_lba >> 24) & 0xFF,   /* LBA MSB */
                    (track_lba >> 16) & 0xFF,
                    (track_lba >> 8) & 0xFF,
                    track_lba & 0xFF,           /* LBA LSB */
                    0x00, 0x00, 0x05,           /* 5 sectors */
                    0x10,                       /* User data only */
                    0x00,                       /* No subchannel */
                    0x00
                };

                /* 5 sectors * 2352 bytes = 11760 bytes */
                unsigned char *audio_buf = malloc(11760);
                if (audio_buf) {
                    IOVirtualRange range = { (IOVirtualAddress)audio_buf, 11760 };

                    (*task)->SetCommandDescriptorBlock(task, read_cdb, kSCSICDBSize_12Byte);
                    (*task)->SetScatterGatherEntries(task, &range, 1, 11760,
                                                     kSCSIDataTransfer_FromTargetToInitiator);
                    (*task)->SetTimeoutDuration(task, 10000);

                    (*task)->ExecuteTaskSync(task, &senseData, &taskStatus, &transferCount);
                    /* Ignore errors, just force I/O */

                    free(audio_buf);
                }
                (*task)->Release(task);
                usleep(50000);  /* 50ms settle time */
            }
        }

        /*
         * Now read ISRC using READ SUB-CHANNEL command (0x42)
         */
        task = (*taskDevice)->CreateSCSITask(taskDevice);
        if (!task) {
            continue;
        }

        unsigned char cdb[10] = {
            0x42,           /* READ SUB-CHANNEL */
            0x00,
            0x40,           /* SubQ bit set */
            0x03,           /* Format: ISRC */
            0x00,
            0x00,
            (unsigned char)track_num,  /* Track number */
            0x00, 0x18,     /* Allocation length: 24 bytes */
            0x00
        };

        unsigned char buf[24];
        memset(buf, 0, sizeof(buf));

        IOVirtualRange range = { (IOVirtualAddress)buf, 24 };

        (*task)->SetCommandDescriptorBlock(task, cdb, kSCSICDBSize_10Byte);
        (*task)->SetScatterGatherEntries(task, &range, 1, 24,
                                         kSCSIDataTransfer_FromTargetToInitiator);
        (*task)->SetTimeoutDuration(task, quick_probe ? 5000 : 30000);  /* 5s for probe, 30s for full */

        ioResult = (*task)->ExecuteTaskSync(task, &senseData, &taskStatus, &transferCount);

        if (ioResult == kIOReturnSuccess && taskStatus == kSCSITaskStatus_GOOD) {
            /* Check if ISRC is valid (TCVal bit 7) */
            if (buf[8] & 0x80) {
                /* ISRC is in bytes 9-20 as ASCII */
                char isrc[13];
                memcpy(isrc, buf + 9, 12);
                isrc[12] = '\0';

                if (validate_isrc(isrc)) {
                    result = strdup(isrc);
                }
            }
        }

        (*task)->Release(task);
    }

    (*taskDevice)->ReleaseExclusiveAccess(taskDevice);
    (*taskDevice)->Release(taskDevice);

    return result;
}

#else
/* Other platforms - stub */
static char *
read_isrc_mmc(const char *device, int track_num, int track_lba, int quick_probe)
{
    (void)device;
    (void)track_num;
    (void)track_lba;
    (void)quick_probe;
    return NULL;  /* Not implemented */
}
#endif /* __APPLE__ */
#endif /* __linux__ */

/*
 * Read ISRCs with libdiscid + MMC fallback for missing tracks
 * Strategy:
 *   1. Read TOC to get track info
 *   2. Quick probe middle track to see if disc has ISRCs at all
 *   3. If probe succeeds, try libdiscid ISRC read (fast path)
 *   4. For any missing ISRCs, use direct MMC READ SUB-CHANNEL
 * Returns array of strings (caller must free), NULL on failure
 * Sets *first_track and *last_track
 */
static char **
read_isrcs_with_retry(const char *device, int *first_track, int *last_track)
{
    if (!discid_has_feature(DISCID_FEATURE_ISRC)) {
        return NULL;
    }

    /* First, get TOC to know track info */
    DiscId *toc_disc = discid_new();
    if (!toc_disc) {
        return NULL;
    }

    if (!discid_read_sparse(toc_disc, device, DISCID_FEATURE_READ)) {
        discid_free(toc_disc);
        return NULL;
    }

    *first_track = discid_get_first_track_num(toc_disc);
    *last_track = discid_get_last_track_num(toc_disc);
    int num_tracks = *last_track - *first_track + 1;

    /* Store track offsets for MMC seek */
    int *offsets = malloc(num_tracks * sizeof(int));
    if (!offsets) {
        discid_free(toc_disc);
        return NULL;
    }
    for (int i = *first_track; i <= *last_track; i++) {
        offsets[i - *first_track] = discid_get_track_offset(toc_disc, i);
    }

    discid_free(toc_disc);

    /* Quick probe to see if disc has ISRCs at all.
     * This avoids slow libdiscid ISRC scan on discs without ISRCs.
     * Try middle track first (track 1 has high failure rates on some drives),
     * then try last track with full mode if middle fails.
     * Avoid track 1 as fallback since it's the most error-prone. */
    int probe_track = *first_track + (num_tracks / 2);
    int probe_idx = probe_track - *first_track;
    int probe_lba = offsets[probe_idx] - 150;
    if (probe_lba < 0) probe_lba = 0;

    char *probe_isrc = read_isrc_mmc(device, probe_track, probe_lba, 1);  /* quick probe */

    /* If middle track probe failed, try last track with full mode as fallback.
     * Intermittent failures can occur on any track. */
    if (!probe_isrc) {
        int last_idx = num_tracks - 1;
        int last_lba = offsets[last_idx] - 150;
        if (last_lba < 0) last_lba = 0;
        probe_isrc = read_isrc_mmc(device, *last_track, last_lba, 0);  /* full mode */
        if (probe_isrc) {
            probe_idx = last_idx;  /* Store in last track slot */
        }
    }

    if (!probe_isrc) {
        /* Both probes failed - disc likely has no ISRCs */
        free(offsets);
        return NULL;
    }

    /* Disc has ISRCs - allocate array and store probe result */
    char **isrcs = calloc(num_tracks, sizeof(char *));
    if (!isrcs) {
        free(probe_isrc);
        free(offsets);
        return NULL;
    }

    isrcs[probe_idx] = probe_isrc;

    /* Try libdiscid for remaining tracks (usually fast when disc has ISRCs) */
    DiscId *disc = discid_new();
    if (disc) {
        int saved_stderr = suppress_stderr();
        int read_ok = discid_read_sparse(disc, device, DISCID_FEATURE_READ | DISCID_FEATURE_ISRC);
        restore_stderr(saved_stderr);

        if (read_ok) {
            for (int i = *first_track; i <= *last_track; i++) {
                int idx = i - *first_track;
                if (isrcs[idx] == NULL) {  /* don't overwrite probe result */
                    const char *isrc = discid_get_track_isrc(disc, i);
                    if (validate_isrc(isrc)) {
                        isrcs[idx] = strdup(isrc);
                    }
                }
            }
        }
        discid_free(disc);
    }

    /* Use MMC fallback for any still-missing ISRCs */
    for (int i = *first_track; i <= *last_track; i++) {
        int idx = i - *first_track;
        if (isrcs[idx] == NULL) {
            int lba = offsets[idx] - 150;
            if (lba < 0) lba = 0;
            char *mmc_isrc = read_isrc_mmc(device, i, lba, 0);  /* full mode */
            if (mmc_isrc) {
                isrcs[idx] = mmc_isrc;
            }
        }
    }

    free(offsets);
    return isrcs;
}

/*
 * Free ISRC array
 */
static void
free_isrcs(char **isrcs, int num_tracks)
{
    if (isrcs) {
        for (int i = 0; i < num_tracks; i++) {
            free(isrcs[i]);
        }
        free(isrcs);
    }
}

/* List optical drives */
static int
list_drives(void)
{
#ifdef __APPLE__
    return system("drutil status 2>/dev/null || echo 'No optical drive found'");
#elif defined(__linux__)
    return system("lsblk -dp -I 11 -o NAME,VENDOR,MODEL 2>/dev/null || echo 'No optical drive found'");
#else
    fprintf(stderr, "Drive listing not supported on this platform\n");
    return 1;
#endif
}

/* Help text structures */
struct option_help {
    const char *shortopt;
    const char *longopt;
    const char *description;
};

struct argument_help {
    const char *name;
    const char *description;
};

static const struct option_help option_help[] = {
    { "-R", "--raw",          "Raw TOC mode" },
    { "-A", "--accuraterip",  "AccurateRip mode" },
    { "-C", "--catalog",      "Media Catalog Number (MCN/UPC/EAN) mode" },
    { "-F", "--freedb",       "FreeDB/CDDB mode" },
    { "-I", "--isrc",         "ISRC mode" },
    { "-M", "--musicbrainz",  "MusicBrainz mode" },
    { "-a", "--all",          "All modes and actions (default)" },
    { "-i", "--id",           "Display disc ID" },
    { "-t", "--toc",          "Display TOC" },
    { "-u", "--url",          "Display verification URL" },
    { "-o", "--open",         "Open URL in browser" },
    { "-c", "--calculate",    "Calculate from CDTOC data" },
    { "-l", "--list-drives",  "List optical drives" },
    { "-q", "--quiet",        "Suppress error messages" },
    { "-h", "--help",         "Display this help" },
    { "-V", "--version",      "Display version information" },
    { NULL, NULL, NULL }
};

static const struct argument_help argument_help[] = {
    { "DEVICE",  "CD device (e.g., /dev/rdisk16 on macOS, /dev/sr0 on Linux)" },
    { NULL, NULL }
};

static void
print_options(const struct option_help *opts)
{
    fprintf(stderr, "Options:\n");
    for (; opts->shortopt; opts++) {
        fprintf(stderr, "  %-3s %-17s %s\n", opts->shortopt, opts->longopt, opts->description);
    }
    fprintf(stderr, "\n");
}

static void
print_arguments(const struct argument_help *args)
{
    fprintf(stderr, "Arguments:\n");
    for (; args->name; args++) {
        fprintf(stderr, "  %-20s %s\n", args->name, args->description);
    }
    fprintf(stderr, "\n");
}

static void
print_usage(const char *argv0)
{
    const char *prog;

    prog = strrchr(argv0, '/');
    prog = prog ? prog + 1 : argv0;

    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [options] <DEVICE>\n", prog);
    fprintf(stderr, "  %s [options] -c <TOC>\n", prog);
    fprintf(stderr, "  %s [options] -c\n\n", prog);

    fprintf(stderr, "Calculate disc IDs and TOC from CD or CDTOC data.\n\n");

    print_options(option_help);
    print_arguments(argument_help);

    fprintf(stderr, "Mode Options:\n");
    fprintf(stderr, "  Modes [-R|A|C|F|I|M] are mutually exclusive.\n");
    fprintf(stderr, "  Default is -a (all) when no mode or action is specified.\n");
    fprintf(stderr, "  Default mode is MusicBrainz (-M) when an action is specified.\n\n");

    fprintf(stderr, "TOC Input Formats (with -c):\n");
    fprintf(stderr, "  Raw (-R):         first last offset1 ... offsetN leadout\n");
    fprintf(stderr, "  MusicBrainz (-M): first last leadout offset1 ... offsetN\n");
    fprintf(stderr, "  AccurateRip (-A): count leadout offset1 ... offsetN\n");
    fprintf(stderr, "  FreeDB (-F):      [discid] count offset1 ... offsetN total_seconds\n\n");

    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s /dev/rdisk16                      # All info (default)\n", prog);
    fprintf(stderr, "  %s -M /dev/rdisk16                   # MusicBrainz disc ID\n", prog);
    fprintf(stderr, "  %s -Mtu /dev/rdisk16                 # MusicBrainz TOC and URL\n", prog);
    fprintf(stderr, "  %s -C /dev/rdisk16                   # MCN only\n", prog);
    fprintf(stderr, "  %s -c 1 12 150 17477 ... 198592      # Raw TOC (default)\n", prog);
    fprintf(stderr, "  %s -Mc 1 12 198592 150 17477 ...     # MusicBrainz format\n", prog);
    fprintf(stderr, "  echo \"1 12 150 ... 198592\" | %s -c   # From stdin\n\n", prog);

    fprintf(stderr, "Notes:\n");
    fprintf(stderr, "  - FreeDB was discontinued in 2020.\n");
    fprintf(stderr, "  - URL/open only supported for MusicBrainz mode.\n");
    fprintf(stderr, "  - MCN (-C) and ISRC (-I) require a physical disc.\n");
#ifdef __APPLE__
    fprintf(stderr, "  - On macOS, use raw device paths (e.g., /dev/rdisk16).\n");
#endif
    fprintf(stderr, "\n");
}

static void
print_version(const char *argv0)
{
    const char *prog;

    prog = strrchr(argv0, '/');
    prog = prog ? prog + 1 : argv0;

    printf("%s %s\n", prog, VERSION);
    printf("%s\n", discid_get_version_string());
}

/* Print functions for each mode */

static void
print_raw_toc(DiscId *disc)
{
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);

    printf("%d %d", first_track, last_track);
    for (int i = first_track; i <= last_track; i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    printf(" %d\n", discid_get_sectors(disc));
}

static void
print_accuraterip_id(DiscId *disc)
{
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);
    int num_tracks = last_track - first_track + 1;

    int *offsets = calloc(last_track + 2, sizeof(int));
    if (!offsets) {
        PRINT_ERROR("Error: Memory allocation failed\n");
        return;
    }

    for (int i = first_track; i <= last_track; i++) {
        offsets[i] = discid_get_track_offset(disc, i);
    }
    offsets[0] = discid_get_sectors(disc);

    unsigned int disc_id1, disc_id2, cddb_id;
    calculate_accuraterip_id(num_tracks, offsets, &disc_id1, &disc_id2, &cddb_id);

    printf("%03d-%08x-%08x-%08x\n", num_tracks, disc_id1, disc_id2, cddb_id);

    free(offsets);
}

static void
print_accuraterip_toc(DiscId *disc)
{
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);
    int num_tracks = last_track - first_track + 1;

    /* AccurateRip format: count leadout offset1 ... offsetN */
    printf("%d %d", num_tracks, discid_get_sectors(disc));
    for (int i = first_track; i <= last_track; i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    printf("\n");
}

static void
print_freedb_id(DiscId *disc)
{
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);
    int num_tracks = last_track - first_track + 1;

    int *offsets = calloc(last_track + 2, sizeof(int));
    if (!offsets) {
        PRINT_ERROR("Error: Memory allocation failed\n");
        return;
    }

    for (int i = first_track; i <= last_track; i++) {
        offsets[i] = discid_get_track_offset(disc, i);
    }
    offsets[0] = discid_get_sectors(disc);

    unsigned int cddb_id = calculate_cddb_id(num_tracks, offsets);
    printf("%08x\n", cddb_id);

    free(offsets);
}

static void
print_freedb_toc(DiscId *disc)
{
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);
    int num_tracks = last_track - first_track + 1;

    /* FreeDB/cd-discid format: count offset1 ... offsetN total_seconds */
    printf("%d", num_tracks);
    for (int i = first_track; i <= last_track; i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    printf(" %d\n", discid_get_sectors(disc) / 75);
}

static void
print_musicbrainz_id(DiscId *disc)
{
    printf("%s\n", discid_get_id(disc));
}

static void
print_musicbrainz_toc(DiscId *disc)
{
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);

    /* MusicBrainz format: first last leadout offset1 ... offsetN */
    printf("%d %d %d", first_track, last_track, discid_get_sectors(disc));
    for (int i = first_track; i <= last_track; i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    printf("\n");
}

static void
print_musicbrainz_url(DiscId *disc)
{
    printf("%s\n", discid_get_submission_url(disc));
}

static int
open_url_in_browser(const char *url)
{
#ifdef OPEN_CMD
    if (strlen(OPEN_CMD) == 0) {
        PRINT_ERROR("Error: Browser opening not supported on this platform\n");
        return EX_UNAVAILABLE;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s \"%s\" >/dev/null 2>&1", OPEN_CMD, url);
    int ret = system(cmd);
    if (ret != 0) {
        PRINT_ERROR("Error: Failed to open browser\n");
        return EX_UNAVAILABLE;
    }
    return EX_OK;
#else
    PRINT_ERROR("Error: Browser opening not supported on this platform\n");
    return EX_UNAVAILABLE;
#endif
}

/*
 * Check if string is a valid hex number (for CDDB disc ID detection)
 */
static int
is_hex_string(const char *s)
{
    if (s == NULL || *s == '\0')
        return 0;

    size_t len = strlen(s);
    if (len != 8)  /* CDDB IDs are exactly 8 hex chars */
        return 0;

    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

/*
 * Parse CDTOC data based on mode
 * Returns allocated offsets array (offsets[0] = leadout, offsets[1..N] = track offsets)
 * Sets first_track, last_track
 * Caller must free offsets
 */
static int *
parse_cdtoc(int argc, char *argv[], int optind_val, disc_mode_t mode,
            int *first_track, int *last_track)
{
    int *offsets = NULL;
    int values[102];
    int count = 0;

    /* Collect values from args or stdin */
    if (optind_val < argc) {
        /* From command line - could be separate args or one quoted string */
        for (int i = optind_val; i < argc && count < 102; i++) {
            /* Check if this arg contains spaces (quoted string with multiple values) */
            char *arg_copy = strdup(argv[i]);
            char *token = strtok(arg_copy, " \t");
            while (token != NULL && count < 102) {
                /* Check for hex (CDDB ID) */
                if (is_hex_string(token) && count == 0 && mode == MODE_FREEDB) {
                    /* Skip CDDB ID - we'll recalculate it */
                    token = strtok(NULL, " \t");
                    continue;
                }
                char *endptr;
                long val = strtol(token, &endptr, 10);
                if (*endptr != '\0' || val < 0 || val > INT_MAX) {
                    PRINT_ERROR("Error: TOC: Bad number: %s\n", token);
                    free(arg_copy);
                    return NULL;
                }
                values[count++] = (int)val;
                token = strtok(NULL, " \t");
            }
            free(arg_copy);
        }
    } else if (!isatty(STDIN_FILENO)) {
        /* From stdin */
        char line[4096];
        if (fgets(line, sizeof(line), stdin) == NULL) {
            PRINT_ERROR("Error: TOC: Failed to read from stdin\n");
            return NULL;
        }

        char *token = strtok(line, " \t\n");
        while (token != NULL && count < 102) {
            /* Check for hex (CDDB ID) */
            if (is_hex_string(token) && count == 0 && mode == MODE_FREEDB) {
                /* Skip CDDB ID */
                token = strtok(NULL, " \t\n");
                continue;
            }
            char *endptr;
            long val = strtol(token, &endptr, 10);
            if (*endptr != '\0' || val < 0 || val > INT_MAX) {
                PRINT_ERROR("Error: TOC: Bad number: %s\n", token);
                return NULL;
            }
            values[count++] = (int)val;
            token = strtok(NULL, " \t\n");
        }
    } else {
        PRINT_ERROR("Error: Usage: '-c' requires TOC data\n");
        return NULL;
    }

    if (count < 3) {
        PRINT_ERROR("Error: TOC: Insufficient data (need at least 3 values)\n");
        return NULL;
    }

    int num_tracks;
    int leadout;

    /* Parse based on mode */
    switch (mode) {
        case MODE_MUSICBRAINZ:
            /* Format: first last leadout offset1 ... offsetN */
            *first_track = values[0];
            *last_track = values[1];
            leadout = values[2];
            num_tracks = *last_track - *first_track + 1;

            if (count != num_tracks + 3) {
                PRINT_ERROR("Error: TOC: MusicBrainz format expects %d values, got %d\n",
                        num_tracks + 3, count);
                return NULL;
            }

            offsets = calloc(*last_track + 2, sizeof(int));
            if (!offsets) {
                PRINT_ERROR("Error: Memory allocation failed\n");
                return NULL;
            }

            offsets[0] = leadout;
            for (int i = 0; i < num_tracks; i++) {
                offsets[*first_track + i] = values[3 + i];
            }
            break;

        case MODE_ACCURATERIP:
            /* Format: count leadout offset1 ... offsetN */
            /* OR MusicBrainz format: first last leadout offset1 ... (we detect and convert) */
            if (values[0] >= 1 && values[0] <= 99 &&
                values[1] >= values[0] && values[1] <= 99 &&
                values[2] > values[1]) {
                /* Looks like MusicBrainz format - convert */
                *first_track = values[0];
                *last_track = values[1];
                leadout = values[2];
                num_tracks = *last_track - *first_track + 1;

                if (count != num_tracks + 3) {
                    PRINT_ERROR("Error: TOC: Expected %d values, got %d\n", num_tracks + 3, count);
                    return NULL;
                }

                offsets = calloc(*last_track + 2, sizeof(int));
                if (!offsets) {
                    PRINT_ERROR("Error: Memory allocation failed\n");
                    return NULL;
                }

                offsets[0] = leadout;
                for (int i = 0; i < num_tracks; i++) {
                    offsets[*first_track + i] = values[3 + i];
                }
            } else {
                /* AccurateRip native format: count leadout offset1 ... */
                num_tracks = values[0];
                leadout = values[1];
                *first_track = 1;
                *last_track = num_tracks;

                if (count != num_tracks + 2) {
                    PRINT_ERROR("Error: TOC: AccurateRip format expects %d values, got %d\n",
                            num_tracks + 2, count);
                    return NULL;
                }

                offsets = calloc(num_tracks + 2, sizeof(int));
                if (!offsets) {
                    PRINT_ERROR("Error: Memory allocation failed\n");
                    return NULL;
                }

                offsets[0] = leadout;
                for (int i = 0; i < num_tracks; i++) {
                    offsets[1 + i] = values[2 + i];
                }
            }
            break;

        case MODE_FREEDB:
            /* Format: [discid] count offset1 ... offsetN total_seconds */
            num_tracks = values[0];
            *first_track = 1;
            *last_track = num_tracks;

            if (count != num_tracks + 2) {
                PRINT_ERROR("Error: TOC: FreeDB format expects %d values, got %d\n",
                        num_tracks + 2, count);
                return NULL;
            }

            /* Last value is total_seconds, convert to frames */
            leadout = values[count - 1] * 75;

            offsets = calloc(num_tracks + 2, sizeof(int));
            if (!offsets) {
                PRINT_ERROR("Error: Memory allocation failed\n");
                return NULL;
            }

            offsets[0] = leadout;
            for (int i = 0; i < num_tracks; i++) {
                offsets[1 + i] = values[1 + i];
            }
            break;

        case MODE_RAW:
        default:
            /* Format: first last offset1 ... offsetN leadout */
            *first_track = values[0];
            *last_track = values[1];
            num_tracks = *last_track - *first_track + 1;

            if (count != num_tracks + 3) {
                PRINT_ERROR("Error: TOC: Raw format expects %d values, got %d\n",
                        num_tracks + 3, count);
                return NULL;
            }

            offsets = calloc(*last_track + 2, sizeof(int));
            if (!offsets) {
                PRINT_ERROR("Error: Memory allocation failed\n");
                return NULL;
            }

            /* Last value is leadout */
            offsets[0] = values[count - 1];
            for (int i = 0; i < num_tracks; i++) {
                offsets[*first_track + i] = values[2 + i];
            }
            break;
    }

    /* Validate track range */
    if (*first_track < 1 || *last_track > 99 || *first_track > *last_track) {
        PRINT_ERROR("Error: TOC: Bad track range (first=%d, last=%d)\n",
                *first_track, *last_track);
        free(offsets);
        return NULL;
    }

    /* Validate offsets are monotonically increasing */
    int prev_offset = 0;
    for (int i = *first_track; i <= *last_track; i++) {
        if (offsets[i] < prev_offset) {
            PRINT_ERROR("Error: TOC: Offsets must be monotonically increasing\n");
            free(offsets);
            return NULL;
        }
        prev_offset = offsets[i];
    }

    if (offsets[0] <= prev_offset) {
        PRINT_ERROR("Error: TOC: Leadout must be greater than last track offset\n");
        free(offsets);
        return NULL;
    }

    return offsets;
}

int
main(int argc, char *argv[])
{
    disc_mode_t mode = MODE_NONE;
    action_t action = ACTION_NONE;
    int calculate_mode = 0;
    int opt;

    static struct option long_options[] = {
        {"raw",          no_argument, 0, 'R'},
        {"accuraterip",  no_argument, 0, 'A'},
        {"catalog",      no_argument, 0, 'C'},
        {"freedb",       no_argument, 0, 'F'},
        {"isrc",         no_argument, 0, 'I'},
        {"musicbrainz",  no_argument, 0, 'M'},
        {"all",          no_argument, 0, 'a'},
        {"id",           no_argument, 0, 'i'},
        {"toc",          no_argument, 0, 't'},
        {"url",          no_argument, 0, 'u'},
        {"open",         no_argument, 0, 'o'},
        {"calculate",    no_argument, 0, 'c'},
        {"list-drives",  no_argument, 0, 'l'},
        {"quiet",        no_argument, 0, 'q'},
        {"help",         no_argument, 0, 'h'},
        {"version",      no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "RACFIMaituoclqhV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'R':
                if (mode != MODE_NONE && mode != MODE_RAW) {
                    PRINT_ERROR("Error: Usage: '-%c' and '-R' are mutually exclusive\n", mode_flag(mode));
                    return EX_USAGE;
                }
                mode = MODE_RAW;
                break;
            case 'A':
                if (mode != MODE_NONE && mode != MODE_ACCURATERIP) {
                    PRINT_ERROR("Error: Usage: '-%c' and '-A' are mutually exclusive\n", mode_flag(mode));
                    return EX_USAGE;
                }
                mode = MODE_ACCURATERIP;
                break;
            case 'C':
                if (mode != MODE_NONE && mode != MODE_CATALOG) {
                    PRINT_ERROR("Error: Usage: '-%c' and '-C' are mutually exclusive\n", mode_flag(mode));
                    return EX_USAGE;
                }
                mode = MODE_CATALOG;
                break;
            case 'F':
                if (mode != MODE_NONE && mode != MODE_FREEDB) {
                    PRINT_ERROR("Error: Usage: '-%c' and '-F' are mutually exclusive\n", mode_flag(mode));
                    return EX_USAGE;
                }
                mode = MODE_FREEDB;
                break;
            case 'I':
                if (mode != MODE_NONE && mode != MODE_ISRC) {
                    PRINT_ERROR("Error: Usage: '-%c' and '-I' are mutually exclusive\n", mode_flag(mode));
                    return EX_USAGE;
                }
                mode = MODE_ISRC;
                break;
            case 'M':
                if (mode != MODE_NONE && mode != MODE_MUSICBRAINZ) {
                    PRINT_ERROR("Error: Usage: '-%c' and '-M' are mutually exclusive\n", mode_flag(mode));
                    return EX_USAGE;
                }
                mode = MODE_MUSICBRAINZ;
                break;
            case 'a':
                if (mode != MODE_NONE && mode != MODE_ALL) {
                    PRINT_ERROR("Error: Usage: '-%c' and '-a' are mutually exclusive\n", mode_flag(mode));
                    return EX_USAGE;
                }
                mode = MODE_ALL;
                action = ACTION_ALL;
                break;
            case 'i':
                action |= ACTION_ID;
                break;
            case 't':
                action |= ACTION_TOC;
                break;
            case 'u':
                action |= ACTION_URL;
                break;
            case 'o':
                action |= ACTION_OPEN;
                break;
            case 'c':
                calculate_mode = 1;
                break;
            case 'l':
                return list_drives() == 0 ? EX_OK : EX_UNAVAILABLE;
            case 'q':
                quiet_mode = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return EX_OK;
            case 'V':
                print_version(argv[0]);
                return EX_OK;
            default:
                print_usage(argv[0]);
                return EX_USAGE;
        }
    }

    /* Apply defaults based on what was specified */
    if (mode == MODE_NONE && action == ACTION_NONE) {
        /* Nothing specified: default to -a */
        mode = MODE_ALL;
        action = ACTION_ALL;
    } else if (mode != MODE_NONE && mode != MODE_ALL && action == ACTION_NONE) {
        /* Mode specified but no action: default to -i */
        action = ACTION_ID;
    } else if (mode == MODE_NONE && action != ACTION_NONE) {
        /* Action specified but no mode: default to MusicBrainz */
        mode = MODE_MUSICBRAINZ;
    }

    /* Validate mode/action combinations */
    if (calculate_mode && (mode & (MODE_CATALOG | MODE_ISRC))) {
        if (mode == MODE_CATALOG || mode == MODE_ISRC) {
            /* User explicitly requested MCN or ISRC with -c */
            PRINT_ERROR("Error: Usage: '-C' and '-I' require a physical disc\n");
            return EX_USAGE;
        }
        /* Strip MCN/ISRC from MODE_ALL when calculating */
        mode &= ~(MODE_CATALOG | MODE_ISRC);
    }

    /* Validate argument combinations */
    if (calculate_mode) {
        /* Warn if -c is used with something that looks like a device path */
        for (int i = optind; i < argc; i++) {
            if (argv[i][0] == '/') {
                PRINT_ERROR("Error: Usage: '-c' expects TOC data, not device path '%s'\n", argv[i]);
                return EX_USAGE;
            }
        }
    } else {
        /* Without -c, allow only one argument (the device) */
        if (optind < argc && optind + 1 < argc) {
            PRINT_ERROR("Error: Usage: Too many arguments (expected one device)\n");
            return EX_USAGE;
        }
        /* Check for empty device argument */
        if (optind < argc && argv[optind][0] == '\0') {
            PRINT_ERROR("Error: Usage: Device path cannot be empty\n");
            return EX_USAGE;
        }
    }

    if ((mode & MODE_RAW) && (action & ACTION_ID)) {
        /* Raw mode has no disc ID - just TOC */
        action &= ~ACTION_ID;
        action |= ACTION_TOC;
    }

    if ((mode & MODE_CATALOG) && (action & (ACTION_TOC | ACTION_URL))) {
        /* MCN mode doesn't have TOC or URL - just ignore silently */
        action &= ~(ACTION_TOC | ACTION_URL);
    }

    if ((mode & MODE_ISRC) && (action & (ACTION_TOC | ACTION_URL))) {
        /* ISRC mode doesn't have TOC or URL - just ignore silently */
        action &= ~(ACTION_TOC | ACTION_URL);
    }

    if ((action & ACTION_URL) && !(mode & MODE_MUSICBRAINZ)) {
        if (mode != MODE_ALL) {
            PRINT_ERROR("Error: Usage: '-u' is not supported for %s mode\n", mode_name(mode));
            return EX_USAGE;
        }
    }

    if ((action & ACTION_OPEN) && !(mode & MODE_MUSICBRAINZ)) {
        if (mode != MODE_ALL) {
            PRINT_ERROR("Error: Usage: '-o' is not supported for %s mode\n", mode_name(mode));
            return EX_USAGE;
        }
    }

    /* Create disc object */
    DiscId *disc = discid_new();
    if (!disc) {
        PRINT_ERROR("Error: Could not create disc object\n");
        return EX_SOFTWARE;
    }

    char *device = NULL;
    int *offsets = NULL;
    int first_track, last_track;

    if (calculate_mode) {
        /* Determine parse mode - use specified mode or default to raw */
        disc_mode_t parse_mode = mode;
        if (parse_mode == MODE_NONE || parse_mode == MODE_ALL) {
            parse_mode = MODE_RAW;
        }

        offsets = parse_cdtoc(argc, argv, optind, parse_mode, &first_track, &last_track);
        if (!offsets) {
            discid_free(disc);
            return EX_DATAERR;
        }

        if (!discid_put(disc, first_track, last_track, offsets)) {
            PRINT_ERROR("Error: %s\n", discid_get_error_msg(disc));
            free(offsets);
            discid_free(disc);
            return EX_DATAERR;
        }
    } else {
        /* Read from device */
        if (optind >= argc) {
            PRINT_ERROR("Error: Usage: No device specified\n");
            if (!quiet_mode) print_usage(argv[0]);
            discid_free(disc);
            return EX_USAGE;
        }

        device = argv[optind];
        if (!discid_read_sparse(disc, device, DISCID_FEATURE_READ)) {
            PRINT_ERROR("Error: %s\n", discid_get_error_msg(disc));
            discid_free(disc);
            return EX_NOINPUT;
        }
    }

    int exit_code = EX_OK;
    int need_separator = 0;

    /* Output based on mode and action */
    if (mode == MODE_ALL) {
        /* All mode: MCN, ISRC, Raw, AccurateRip, FreeDB, MusicBrainz */

        /* MCN section */
        if (!calculate_mode && device) {
            char *mcn = read_mcn_with_retry(device);
            if (mcn) {
                printf("----- MCN -----\n");
                printf("%s\n", mcn);
                free(mcn);
                need_separator = 1;
            }
        }

        /* ISRC section */
        if (!calculate_mode && device) {
            int isrc_first, isrc_last;
            char **isrcs = read_isrcs_with_retry(device, &isrc_first, &isrc_last);
            if (isrcs) {
                int num_tracks = isrc_last - isrc_first + 1;
                int has_any = 0;
                for (int i = 0; i < num_tracks; i++) {
                    if (isrcs[i]) {
                        has_any = 1;
                        break;
                    }
                }
                if (has_any) {
                    if (need_separator) printf("\n");
                    printf("----- ISRC -----\n");
                    for (int i = 0; i < num_tracks; i++) {
                        if (isrcs[i]) {
                            printf("%d: %s\n", isrc_first + i, isrcs[i]);
                        }
                    }
                    need_separator = 1;
                }
                free_isrcs(isrcs, num_tracks);
            }
        }

        /* Raw section */
        if (need_separator) printf("\n");
        printf("----- Raw -----\n");
        print_raw_toc(disc);
        need_separator = 1;

        /* AccurateRip section */
        printf("\n----- AccurateRip -----\n");
        print_accuraterip_toc(disc);
        print_accuraterip_id(disc);

        /* FreeDB section */
        printf("\n----- FreeDB -----\n");
        print_freedb_toc(disc);
        print_freedb_id(disc);

        /* MusicBrainz section */
        printf("\n----- MusicBrainz -----\n");
        print_musicbrainz_toc(disc);
        print_musicbrainz_id(disc);
        print_musicbrainz_url(disc);

        /* Open browser if requested */
        if (action & ACTION_OPEN) {
            exit_code = open_url_in_browser(discid_get_submission_url(disc));
        }

    } else {
        /* Single mode output */

        if (mode & MODE_RAW) {
            if (action & ACTION_TOC)
                print_raw_toc(disc);
        }

        if (mode & MODE_CATALOG) {
            if (!calculate_mode && device) {
                char *mcn = read_mcn_with_retry(device);
                if (mcn) {
                    printf("%s\n", mcn);
                    free(mcn);
                }
            }
        }

        if (mode & MODE_ISRC) {
            if (!calculate_mode && device) {
                int isrc_first, isrc_last;
                char **isrcs = read_isrcs_with_retry(device, &isrc_first, &isrc_last);
                if (isrcs) {
                    int num_tracks = isrc_last - isrc_first + 1;
                    for (int i = 0; i < num_tracks; i++) {
                        if (isrcs[i]) {
                            printf("%d: %s\n", isrc_first + i, isrcs[i]);
                        }
                    }
                    free_isrcs(isrcs, num_tracks);
                }
            }
        }

        if (mode & MODE_ACCURATERIP) {
            if (action & ACTION_TOC)
                print_accuraterip_toc(disc);
            if (action & ACTION_ID)
                print_accuraterip_id(disc);
        }

        if (mode & MODE_FREEDB) {
            if (action & ACTION_TOC)
                print_freedb_toc(disc);
            if (action & ACTION_ID)
                print_freedb_id(disc);
        }

        if (mode & MODE_MUSICBRAINZ) {
            if (action & ACTION_TOC)
                print_musicbrainz_toc(disc);
            if (action & ACTION_ID)
                print_musicbrainz_id(disc);
            if (action & ACTION_URL)
                print_musicbrainz_url(disc);
            if (action & ACTION_OPEN) {
                exit_code = open_url_in_browser(discid_get_submission_url(disc));
            }
        }
    }

    free(offsets);
    discid_free(disc);
    return exit_code;
}
