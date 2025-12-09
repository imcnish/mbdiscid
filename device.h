/*
 * mbdiscid - Disc ID calculator
 * device.h - Device handling and platform abstraction
 */

#ifndef MBDISCID_DEVICE_H
#define MBDISCID_DEVICE_H

#include "types.h"

/* Flags for device_read_disc */
#define READ_MCN     (1 << 0)
#define READ_ISRC    (1 << 1)
#define READ_CDTEXT  (1 << 2)
#define READ_ALL     (READ_MCN | READ_ISRC | READ_CDTEXT)

/*
 * Read disc information from device
 * flags controls what optional data to read (READ_MCN, READ_ISRC, READ_CDTEXT)
 * Returns 0 on success, exit code on error
 */
int device_read_disc(const char *device, disc_info_t *disc, int flags, int verbosity);

/*
 * Read TOC from device
 * Returns 0 on success, exit code on error
 */
int device_read_toc(const char *device, toc_t *toc, int verbosity);

/*
 * Read MCN from device
 * Returns 0 on success, exit code on error
 */
int device_read_mcn(const char *device, char *mcn, int verbosity);

/*
 * Read ISRCs from device
 * Returns 0 on success, exit code on error
 */
int device_read_isrc(const char *device, toc_t *toc, int verbosity);

/*
 * Read CD-Text from device
 * Returns 0 on success, exit code on error
 */
int device_read_cdtext(const char *device, cdtext_t *cdtext, int verbosity);

/*
 * List optical drives
 * Prints output to stdout
 * Returns 0 on success, exit code on error
 */
int device_list_drives(void);

/*
 * Get default device path for platform
 */
const char *device_get_default(void);

/*
 * Normalize device path for platform
 * On macOS: converts /dev/diskN to /dev/rdiskN
 * Returns allocated string (caller must free)
 */
char *device_normalize_path(const char *device);

/*
 * Free CD-Text structure
 */
void cdtext_free(cdtext_t *cdtext);

#endif /* MBDISCID_DEVICE_H */
