/*
 * mbdiscid - Disc ID calculator
 * scsi_macos.c - macOS implementation using SCSITaskDeviceInterface
 *
 * This uses IOKit's SCSITaskDeviceInterface to send SCSI commands directly
 * to CD/DVD drives. Requires Disk Arbitration to unmount and claim the disc
 * before obtaining exclusive access.
 *
 * Supports:
 * - READ CD (0xBE) with formatted Q subchannel (mode 0x02)
 * - READ SUB-CHANNEL (0x42) for ISRC/MCN queries
 * - READ TOC (0x43) for CD-Text
 */

#ifdef PLATFORM_MACOS

#include "scsi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/storage/IOCDTypes.h>
#include <IOKit/storage/IOCDMediaBSDClient.h>
#include <DiskArbitration/DiskArbitration.h>

/* SCSI commands */
#define READ_CD         0xBE
#define READ_SUBCHANNEL 0x42
#define READ_TOC        0x43

/* Timeout in milliseconds */
#define SCSI_TIMEOUT 30000

struct scsi_device {
    /* IOKit interfaces */
    io_object_t media_service;
    io_object_t authoring_service;
    IOCFPlugInInterface **plugIn;
    MMCDeviceInterface **mmc;
    SCSITaskDeviceInterface **taskIf;

    /* Disk Arbitration */
    DASessionRef da_session;
    DADiskRef da_disk;
    bool da_claimed;
    bool exclusive_access;

    /* BSD device for fallback ioctls */
    int fd;
    char bsd_name[64];  /* For polling on close */

    char error[256];
};

/* Disk Arbitration callback results */
static volatile int da_unmount_result = 0;
static volatile int da_claim_result = 0;

static void unmount_callback(DADiskRef disk, DADissenterRef dissenter, void *context)
{
    (void)disk; (void)context;
    da_unmount_result = dissenter ? -1 : 1;
}

static DADissenterRef claim_release_callback(DADiskRef disk, void *context)
{
    (void)disk; (void)context;
    return NULL;  /* Allow release */
}

static void claim_callback(DADiskRef disk, DADissenterRef dissenter, void *context)
{
    (void)disk; (void)context;
    da_claim_result = dissenter ? -1 : 1;
}

/*
 * Find authoring service (IODVDServices, IOCompactDiscServices, IOBDServices)
 * by walking up the IORegistry tree from the media service.
 */
static io_object_t find_authoring_service(io_object_t start)
{
    io_object_t service = start;
    io_object_t parent;
    kern_return_t kr;

    IOObjectRetain(service);

    while (service != IO_OBJECT_NULL) {
        if (IOObjectConformsTo(service, "IODVDServices") ||
            IOObjectConformsTo(service, "IOCompactDiscServices") ||
            IOObjectConformsTo(service, "IOBDServices")) {
            return service;
        }

        kr = IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent);
        IOObjectRelease(service);

        if (kr != KERN_SUCCESS) {
            return IO_OBJECT_NULL;
        }
        service = parent;
    }

    return IO_OBJECT_NULL;
}

/*
 * Extract BSD name from device path (e.g., "/dev/disk2" -> "disk2")
 */
static const char *get_bsd_name(const char *device)
{
    const char *name = device;
    if (strncmp(name, "/dev/r", 6) == 0) {
        name += 6;  /* Skip "/dev/r" */
    } else if (strncmp(name, "/dev/", 5) == 0) {
        name += 5;  /* Skip "/dev/" */
    }
    return name;
}

scsi_device_t *scsi_open(const char *device)
{
    scsi_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return NULL;
    }
    dev->fd = -1;

    const char *bsd_name = get_bsd_name(device);
    snprintf(dev->bsd_name, sizeof(dev->bsd_name), "/dev/%s", bsd_name);

    /* Find IOKit media service - may need retry after DA release */
    int retries = 20;
    while (retries-- > 0) {
        CFMutableDictionaryRef matching = IOBSDNameMatching(kIOMainPortDefault, 0, bsd_name);
        if (!matching) {
            snprintf(dev->error, sizeof(dev->error), "IOBSDNameMatching failed for %s", bsd_name);
            fprintf(stderr, "scsi_open: %s\n", dev->error);
            free(dev);
            return NULL;
        }

        /* Note: IOServiceGetMatchingService consumes the matching dictionary */
        dev->media_service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
        if (dev->media_service != IO_OBJECT_NULL) {
            break;
        }

        /* Service not found - might be re-registering after previous DA release */
        if (retries > 0) {
            usleep(500000);  /* 500ms */
        }
    }

    if (dev->media_service == IO_OBJECT_NULL) {
        snprintf(dev->error, sizeof(dev->error), "no IOKit service for %s", bsd_name);
        fprintf(stderr, "scsi_open: %s\n", dev->error);
        free(dev);
        return NULL;
    }

    /* Find authoring service */
    dev->authoring_service = find_authoring_service(dev->media_service);
    if (dev->authoring_service == IO_OBJECT_NULL) {
        snprintf(dev->error, sizeof(dev->error), "no authoring service found (drive may be read-only)");
        fprintf(stderr, "scsi_open: %s\n", dev->error);
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    /* Create plugin interface */
    SInt32 score;
    kern_return_t kr = IOCreatePlugInInterfaceForService(
        dev->authoring_service,
        kIOMMCDeviceUserClientTypeID,
        kIOCFPlugInInterfaceID,
        &dev->plugIn,
        &score);

    if (kr != KERN_SUCCESS || !dev->plugIn) {
        snprintf(dev->error, sizeof(dev->error), "IOCreatePlugInInterfaceForService failed: 0x%x", kr);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    /* Get MMC device interface */
    HRESULT hr = (*dev->plugIn)->QueryInterface(
        dev->plugIn,
        CFUUIDGetUUIDBytes(kIOMMCDeviceInterfaceID),
        (LPVOID *)&dev->mmc);

    if (hr != S_OK || !dev->mmc) {
        snprintf(dev->error, sizeof(dev->error), "QueryInterface for MMC failed: 0x%x", (unsigned)hr);
        IODestroyPlugInInterface(dev->plugIn);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    /* Get SCSI task interface */
    dev->taskIf = (*dev->mmc)->GetSCSITaskDeviceInterface(dev->mmc);
    if (!dev->taskIf) {
        snprintf(dev->error, sizeof(dev->error), "GetSCSITaskDeviceInterface failed");
        (*dev->mmc)->Release(dev->mmc);
        IODestroyPlugInInterface(dev->plugIn);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    /* Try to get exclusive access without DA first - retry if drive is busy */
    IOReturn ioret;
    int initial_retries = 10;  /* Up to 5 seconds before trying DA */
    while (initial_retries-- > 0) {
        ioret = (*dev->taskIf)->ObtainExclusiveAccess(dev->taskIf);
        if (ioret == kIOReturnSuccess) {
            dev->exclusive_access = true;
            return dev;
        }
        /* If drive is busy but not due to mounting, wait and retry */
        if (ioret == kIOReturnBusy || ioret == kIOReturnNotReady) {
            usleep(500000);  /* 500ms */
            continue;
        }
        /* Other errors (like kIOReturnExclusiveAccess) mean we need DA */
        break;
    }

    /* Need Disk Arbitration to unmount and claim */
    dev->da_session = DASessionCreate(kCFAllocatorDefault);
    if (!dev->da_session) {
        snprintf(dev->error, sizeof(dev->error), "DASessionCreate failed");
        (*dev->taskIf)->Release(dev->taskIf);
        (*dev->mmc)->Release(dev->mmc);
        IODestroyPlugInInterface(dev->plugIn);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    DASessionScheduleWithRunLoop(dev->da_session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    dev->da_disk = DADiskCreateFromBSDName(kCFAllocatorDefault, dev->da_session, bsd_name);
    if (!dev->da_disk) {
        snprintf(dev->error, sizeof(dev->error), "DADiskCreateFromBSDName failed");
        DASessionUnscheduleFromRunLoop(dev->da_session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFRelease(dev->da_session);
        (*dev->taskIf)->Release(dev->taskIf);
        (*dev->mmc)->Release(dev->mmc);
        IODestroyPlugInInterface(dev->plugIn);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    /* Unmount */
    da_unmount_result = 0;
    DADiskUnmount(dev->da_disk, kDADiskUnmountOptionWhole | kDADiskUnmountOptionForce,
                  unmount_callback, NULL);

    int timeout = 100;  /* 10 seconds */
    while (da_unmount_result == 0 && timeout-- > 0) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    }

    if (da_unmount_result != 1) {
        snprintf(dev->error, sizeof(dev->error), "DADiskUnmount failed (result=%d)", da_unmount_result);
        fprintf(stderr, "scsi_open: %s\n", dev->error);
        CFRelease(dev->da_disk);
        DASessionUnscheduleFromRunLoop(dev->da_session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFRelease(dev->da_session);
        (*dev->taskIf)->Release(dev->taskIf);
        (*dev->mmc)->Release(dev->mmc);
        IODestroyPlugInInterface(dev->plugIn);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    /* Claim */
    da_claim_result = 0;
    DADiskClaim(dev->da_disk, kDADiskClaimOptionDefault,
                claim_release_callback, NULL,
                claim_callback, NULL);

    timeout = 100;
    while (da_claim_result == 0 && timeout-- > 0) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    }

    if (da_claim_result != 1) {
        snprintf(dev->error, sizeof(dev->error), "DADiskClaim failed (result=%d)", da_claim_result);
        fprintf(stderr, "scsi_open: %s\n", dev->error);
        CFRelease(dev->da_disk);
        DASessionUnscheduleFromRunLoop(dev->da_session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFRelease(dev->da_session);
        (*dev->taskIf)->Release(dev->taskIf);
        (*dev->mmc)->Release(dev->mmc);
        IODestroyPlugInInterface(dev->plugIn);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    dev->da_claimed = true;

    /* Try exclusive access again - retry with backoff if drive is busy */
    int exclusive_retries = 20;  /* Up to 10 seconds */
    while (exclusive_retries-- > 0) {
        ioret = (*dev->taskIf)->ObtainExclusiveAccess(dev->taskIf);
        if (ioret == kIOReturnSuccess) {
            break;
        }
        if (ioret != kIOReturnBusy && ioret != kIOReturnNotReady) {
            /* Fatal error, don't retry */
            break;
        }
        /* Drive is busy (still settling from previous operation), wait and retry */
        usleep(500000);  /* 500ms */
    }

    if (ioret != kIOReturnSuccess) {
        snprintf(dev->error, sizeof(dev->error), "ObtainExclusiveAccess failed: 0x%x", ioret);
        fprintf(stderr, "scsi_open: %s\n", dev->error);
        DADiskUnclaim(dev->da_disk);
        CFRelease(dev->da_disk);
        DASessionUnscheduleFromRunLoop(dev->da_session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFRelease(dev->da_session);
        (*dev->taskIf)->Release(dev->taskIf);
        (*dev->mmc)->Release(dev->mmc);
        IODestroyPlugInInterface(dev->plugIn);
        if (dev->authoring_service != dev->media_service) {
            IOObjectRelease(dev->authoring_service);
        }
        IOObjectRelease(dev->media_service);

        free(dev);
        return NULL;
    }

    dev->exclusive_access = true;
    return dev;
}

void scsi_close(scsi_device_t *dev)
{
    if (!dev) return;

    if (dev->exclusive_access && dev->taskIf) {
        (*dev->taskIf)->ReleaseExclusiveAccess(dev->taskIf);
    }

    if (dev->da_claimed && dev->da_disk) {
        DADiskUnclaim(dev->da_disk);
    }

    if (dev->da_disk) {
        CFRelease(dev->da_disk);
    }

    if (dev->da_session) {
        DASessionUnscheduleFromRunLoop(dev->da_session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFRelease(dev->da_session);
    }

    if (dev->taskIf) {
        (*dev->taskIf)->Release(dev->taskIf);
    }

    if (dev->mmc) {
        (*dev->mmc)->Release(dev->mmc);
    }

    if (dev->plugIn) {
        IODestroyPlugInInterface(dev->plugIn);
    }

    if (dev->authoring_service != dev->media_service && dev->authoring_service != IO_OBJECT_NULL) {
        IOObjectRelease(dev->authoring_service);
    }

    if (dev->media_service != IO_OBJECT_NULL) {
        IOObjectRelease(dev->media_service);
    }

    /*
     * After releasing DA claim, macOS remounts the disc which can trigger
     * Spotlight indexing, AV scanners, etc. Poll until the device is
     * accessible again before returning, so the next scsi_open() can succeed.
     */
    if (dev->da_claimed && dev->bsd_name[0]) {
        for (int i = 0; i < 100; i++) {  /* Up to 10 seconds */
            int test_fd = open(dev->bsd_name, O_RDONLY | O_NONBLOCK);
            if (test_fd >= 0) {
                close(test_fd);
                break;
            }
            usleep(100000);  /* 100ms */
        }
    }

    free(dev);
}

const char *scsi_error(scsi_device_t *dev)
{
    return dev ? dev->error : "no device";
}

/*
 * Execute a SCSI command
 */
static int scsi_cmd(scsi_device_t *dev,
                    unsigned char *cdb, int cdb_len,
                    unsigned char *buf, int buf_len)
{
    if (!dev || !dev->taskIf || !dev->exclusive_access) {
        return -1;
    }

    SCSITaskInterface **task = (*dev->taskIf)->CreateSCSITask(dev->taskIf);
    if (!task) {
        snprintf(dev->error, sizeof(dev->error), "CreateSCSITask failed");
        return -1;
    }

    SCSICommandDescriptorBlock scsi_cdb = {0};
    memcpy(scsi_cdb, cdb, cdb_len < 16 ? cdb_len : 16);

    kern_return_t kr = (*task)->SetCommandDescriptorBlock(task, scsi_cdb, cdb_len);
    if (kr != KERN_SUCCESS) {
        (*task)->Release(task);
        snprintf(dev->error, sizeof(dev->error), "SetCommandDescriptorBlock failed: %d", kr);
        return -1;
    }

    IOVirtualRange range;
    range.address = (IOVirtualAddress)buf;
    range.length = buf_len;

    kr = (*task)->SetScatterGatherEntries(task, &range, 1, buf_len,
                                           kSCSIDataTransfer_FromTargetToInitiator);
    if (kr != KERN_SUCCESS) {
        (*task)->Release(task);
        snprintf(dev->error, sizeof(dev->error), "SetScatterGatherEntries failed: %d", kr);
        return -1;
    }

    kr = (*task)->SetTimeoutDuration(task, SCSI_TIMEOUT);
    if (kr != KERN_SUCCESS) {
        (*task)->Release(task);
        snprintf(dev->error, sizeof(dev->error), "SetTimeoutDuration failed: %d", kr);
        return -1;
    }

    SCSITaskStatus taskStatus;
    SCSI_Sense_Data senseData;
    UInt64 bytesTransferred = 0;

    kr = (*task)->ExecuteTaskSync(task, &senseData, &taskStatus, &bytesTransferred);

    (*task)->Release(task);

    if (kr != KERN_SUCCESS) {
        snprintf(dev->error, sizeof(dev->error), "ExecuteTaskSync failed: %d", kr);
        return -1;
    }

    if (taskStatus != kSCSITaskStatus_GOOD) {
        snprintf(dev->error, sizeof(dev->error), "SCSI command failed: status=%d sense=%02X/%02X/%02X",
                 taskStatus, senseData.SENSE_KEY & 0x0F,
                 senseData.ADDITIONAL_SENSE_CODE,
                 senseData.ADDITIONAL_SENSE_CODE_QUALIFIER);
        return -1;
    }

    return (int)bytesTransferred;
}

/* Forward declaration */
static void parse_q_subchannel(const unsigned char *buf, q_subchannel_t *q);

/*
 * Read Q subchannel at specific LBA using READ CD with formatted Q (mode 0x02)
 */
bool scsi_read_q_subchannel(scsi_device_t *dev, int32_t lba, q_subchannel_t *q)
{
    unsigned char cdb[12];
    unsigned char buf[16];  /* Formatted Q is 16 bytes */

    memset(q, 0, sizeof(*q));

    if (!dev) {
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

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf)) < 0) {
        return false;
    }

    parse_q_subchannel(buf, q);
    return true;
}

/*
 * Parse a single Q subchannel frame (16 bytes) into q_subchannel_t structure
 */
static void parse_q_subchannel(const unsigned char *buf, q_subchannel_t *q)
{
    memset(q, 0, sizeof(*q));

    /* Extract control and ADR */
    q->control = (buf[0] >> 4) & 0x0F;
    q->adr = buf[0] & 0x0F;

    /* For formatted Q, drive validates CRC - assume valid if we got data */
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
        /* Mode 3: ISRC - first 5 chars are 6-bit packed, rest is BCD */
        {
            /* Bytes 1-4 contain 5 characters in 6-bit encoding (30 bits) */
            uint8_t c1 = (buf[1] >> 2) & 0x3F;
            uint8_t c2 = ((buf[1] & 0x03) << 4) | ((buf[2] >> 4) & 0x0F);
            uint8_t c3 = ((buf[2] & 0x0F) << 2) | ((buf[3] >> 6) & 0x03);
            uint8_t c4 = buf[3] & 0x3F;
            uint8_t c5 = (buf[4] >> 2) & 0x3F;

            /* Decode 6-bit chars: 0='0', 1-9='1'-'9', 17-42='A'-'Z' */
            #define DECODE6(c) ((c) == 0 ? '0' : ((c) <= 9 ? '0' + (c) : ((c) >= 17 && (c) <= 42 ? 'A' + (c) - 17 : '?')))
            q->isrc[0] = DECODE6(c1);
            q->isrc[1] = DECODE6(c2);
            q->isrc[2] = DECODE6(c3);
            q->isrc[3] = DECODE6(c4);
            q->isrc[4] = DECODE6(c5);
            #undef DECODE6

            /* Remaining 7 digits are BCD */
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
        }
        break;

    default:
        q->has_isrc = false;
        q->has_mcn = false;
        break;
    }
}

/*
 * Read multiple Q subchannels in a TRUE batch using READ CD
 * This reads multiple sectors in a single SCSI command for efficiency
 */
#define MAX_BATCH_SECTORS 75  /* ~1 second of audio, 1200 bytes of Q data */

int scsi_read_q_subchannel_batch(scsi_device_t *dev, int32_t start_lba,
                                  int count, q_subchannel_t *q_array)
{
    if (!dev || count <= 0 || !q_array) {
        return 0;
    }

    int total_success = 0;
    int remaining = count;
    int32_t current_lba = start_lba;
    int array_offset = 0;

    while (remaining > 0) {
        int batch_count = (remaining > MAX_BATCH_SECTORS) ? MAX_BATCH_SECTORS : remaining;
        int buf_size = batch_count * 16;  /* 16 bytes per sector for formatted Q */

        unsigned char *buf = malloc(buf_size);
        if (!buf) {
            return total_success;
        }

        unsigned char cdb[12];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = READ_CD;
        cdb[1] = 0x00;            /* Any sector type */
        cdb[2] = (current_lba >> 24) & 0xFF;
        cdb[3] = (current_lba >> 16) & 0xFF;
        cdb[4] = (current_lba >> 8) & 0xFF;
        cdb[5] = current_lba & 0xFF;
        cdb[6] = (batch_count >> 16) & 0xFF;  /* Transfer length MSB */
        cdb[7] = (batch_count >> 8) & 0xFF;
        cdb[8] = batch_count & 0xFF;          /* Transfer length LSB */
        cdb[9] = 0x00;            /* No main channel data */
        cdb[10] = 0x02;           /* Subchannel = 2 (formatted Q, 16 bytes) */

        memset(buf, 0, buf_size);

        int result = scsi_cmd(dev, cdb, sizeof(cdb), buf, buf_size);
        if (result < 0) {
            free(buf);
            /* Try to continue with smaller batches or single reads */
            if (batch_count > 1) {
                /* Fall back to single-sector reads for this batch */
                for (int i = 0; i < batch_count && remaining > 0; i++) {
                    if (scsi_read_q_subchannel(dev, current_lba + i, &q_array[array_offset + i])) {
                        total_success++;
                    }
                    remaining--;
                }
                current_lba += batch_count;
                array_offset += batch_count;
                continue;
            }
            return total_success;
        }

        /* Parse the batch results */
        int frames_returned = result / 16;
        for (int i = 0; i < frames_returned && i < batch_count; i++) {
            parse_q_subchannel(&buf[i * 16], &q_array[array_offset + i]);
            if (q_array[array_offset + i].crc_valid) {
                total_success++;
            }
        }

        free(buf);

        current_lba += batch_count;
        array_offset += batch_count;
        remaining -= batch_count;
    }

    return total_success;
}

/*
 * Read ISRC using READ SUB-CHANNEL command (0x42)
 */
bool scsi_read_isrc(scsi_device_t *dev, int track, char *isrc)
{
    unsigned char cdb[10];
    unsigned char buf[24];

    if (!dev || !isrc) {
        return false;
    }

    isrc[0] = '\0';

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_SUBCHANNEL;
    cdb[1] = 0x00;
    cdb[2] = 0x40;            /* SubQ = 1 */
    cdb[3] = 0x03;            /* Data format = ISRC */
    cdb[6] = track;           /* Track number */
    cdb[7] = 0;               /* Allocation length MSB */
    cdb[8] = 24;              /* Allocation length LSB */

    memset(buf, 0, sizeof(buf));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf)) < 0) {
        return false;
    }

    /* Check TCVAL (bit 7 of byte 8) */
    if (!(buf[8] & 0x80)) {
        return false;  /* No valid ISRC */
    }

    /* Copy ISRC (bytes 9-20, 12 characters) */
    memcpy(isrc, &buf[9], 12);
    isrc[12] = '\0';

    return true;
}

/*
 * Read MCN using READ SUB-CHANNEL command (0x42)
 */
bool scsi_read_mcn(scsi_device_t *dev, char *mcn)
{
    unsigned char cdb[10];
    unsigned char buf[24];

    if (!dev || !mcn) {
        return false;
    }

    mcn[0] = '\0';

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_SUBCHANNEL;
    cdb[1] = 0x00;
    cdb[2] = 0x40;            /* SubQ = 1 */
    cdb[3] = 0x02;            /* Data format = MCN */
    cdb[7] = 0;               /* Allocation length MSB */
    cdb[8] = 24;              /* Allocation length LSB */

    memset(buf, 0, sizeof(buf));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf)) < 0) {
        return false;
    }

    /* Check MCVAL (bit 7 of byte 8) */
    if (!(buf[8] & 0x80)) {
        return false;  /* No valid MCN */
    }

    /* Copy MCN (bytes 9-21, 13 characters) */
    memcpy(mcn, &buf[9], 13);
    mcn[13] = '\0';

    return true;
}

/*
 * Read CD-Text using READ TOC command (format 5)
 * Allocates memory for the raw CD-Text data.
 */
bool scsi_read_cdtext_raw(scsi_device_t *dev, uint8_t **data, size_t *len)
{
    unsigned char cdb[10];
    unsigned char header[4];

    *data = NULL;
    *len = 0;

    if (!dev) {
        return false;
    }

    /* First, get the header to find total length */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_TOC;
    cdb[1] = 0x00;
    cdb[2] = 0x05;            /* Format = CD-Text */
    cdb[7] = 0;               /* Allocation length MSB */
    cdb[8] = 4;               /* Allocation length LSB */

    memset(header, 0, sizeof(header));

    if (scsi_cmd(dev, cdb, sizeof(cdb), header, sizeof(header)) < 0) {
        return false;
    }

    /* Parse data length from header (big-endian) */
    uint16_t data_len = ((uint16_t)header[0] << 8) | header[1];

    if (data_len < 2) {
        return false;  /* No CD-Text */
    }

    size_t total_len = data_len + 2;
    size_t pack_data_len = data_len - 2;

    /* Sanity checks */
    if (pack_data_len % 18 != 0 || total_len > 8192) {
        return false;
    }

    /* Allocate buffer */
    uint8_t *buf = malloc(total_len);
    if (!buf) {
        return false;
    }

    /* Read full CD-Text data */
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_TOC;
    cdb[1] = 0x00;
    cdb[2] = 0x05;
    cdb[7] = (total_len >> 8) & 0xFF;
    cdb[8] = total_len & 0xFF;

    memset(buf, 0, total_len);

    int ret = scsi_cmd(dev, cdb, sizeof(cdb), buf, (int)total_len);
    if (ret < 0) {
        free(buf);
        return false;
    }

    /* Return just the pack data (skip 4-byte header) */
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

/*
 * Read TOC with control bytes using READ TOC command (format 0)
 */
bool scsi_read_toc_control(scsi_device_t *dev, int *first_track, int *last_track,
                           uint8_t *control)
{
    unsigned char cdb[10];
    unsigned char buf[804];  /* Max: 4 header + 100 tracks * 8 bytes */

    if (!dev) {
        return false;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_TOC;
    cdb[1] = 0x00;            /* LBA format */
    cdb[2] = 0x00;            /* Format 0 = TOC */
    cdb[6] = 0;               /* Starting track */
    cdb[7] = (sizeof(buf) >> 8) & 0xFF;
    cdb[8] = sizeof(buf) & 0xFF;

    memset(buf, 0, sizeof(buf));

    if (scsi_cmd(dev, cdb, sizeof(cdb), buf, sizeof(buf)) < 0) {
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

#endif /* PLATFORM_MACOS */
