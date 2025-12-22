# mbdiscid Implementation Specification

This document describes the internal algorithms, platform-specific mechanisms, and design rationale for `mbdiscid`. It is intended for developers maintaining or extending the codebase.

For observable behavior and output formats, see [Product Specification](Product_Specification.md).

---

## Table of Contents

1. [Overview](#1-overview)
2. [TOC Handling](#2-toc-handling)
3. [Disc ID Calculations](#3-disc-id-calculations)
4. [ISRC Scanning](#4-isrc-scanning)
5. [MCN Reading](#5-mcn-reading)
6. [Platform-Specific SCSI Implementation](#6-platform-specific-scsi-implementation)
7. [Verbose Output Architecture](#7-verbose-output-architecture)
8. [Error Handling Strategy](#8-error-handling-strategy)
9. [Testing Considerations](#9-testing-considerations)

---

# 1. Overview

## 1.1 Architecture

mbdiscid is structured in layers:

```
┌─────────────────────────────────────────┐
│               main.c                    │  CLI parsing, mode dispatch
├─────────────────────────────────────────┤
│     discid.c     │    output.c          │  ID calculation, formatting
├─────────────────────────────────────────┤
│      toc.c       │     isrc.c           │  TOC parsing, ISRC scanning
├─────────────────────────────────────────┤
│              device.c                   │  Device abstraction
├──────────────────┬──────────────────────┤
│  scsi_linux.c    │    scsi_macos.c      │  Platform SCSI layer
└──────────────────┴──────────────────────┘
```

Key design decisions:

- **libdiscid** provides MusicBrainz disc ID calculation and basic TOC reading
- **Raw SCSI** commands are used for full TOC (session info), subchannel data (ISRC, MCN), and CD-Text
- **Platform abstraction** isolates Linux vs macOS differences in the SCSI layer

## 1.2 Dependencies

| Library | Purpose |
|---------|---------|
| libdiscid | MusicBrainz disc ID, basic TOC, device enumeration |
| IOKit (macOS) | SCSI pass-through, device arbitration |
| SG_IO (Linux) | SCSI generic interface |

---

# 2. TOC Handling

## 2.1 TOC Sources

mbdiscid obtains TOC data from two sources:

1. **libdiscid** — Provides track count, offsets, leadout for audio tracks
2. **Full TOC via SCSI** — Provides session information, track types (audio/data), all tracks including data

libdiscid alone is insufficient for Enhanced CDs and Mixed Mode CDs because it only reports audio tracks visible to the OS audio subsystem.

## 2.2 Full TOC Reading

The full TOC is read using MMC command `READ TOC/PMA/ATIP` (opcode 0x43) with format 0x02 (Full TOC).

Response structure:

```
Byte 0-1:  TOC data length
Byte 2:    First session number
Byte 3:    Last session number
Byte 4+:   TOC track descriptors (11 bytes each)
```

Each track descriptor:

```
Byte 0:    Session number
Byte 1:    ADR/Control (control nibble indicates audio vs data)
Byte 2:    TNO (track number, 0xA0-0xA2 for lead-in info, 0xAA for lead-out)
Byte 3:    POINT
Byte 4:    Min (of running time)
Byte 5:    Sec
Byte 6:    Frame
Byte 7:    Zero
Byte 8:    PMIN (of POINT)
Byte 9:    PSEC
Byte 10:   PFRAME
```

Control nibble interpretation:

- Bit 2 set (0x04): Data track
- Bit 2 clear: Audio track

## 2.3 Multi-Session Handling

For Enhanced CDs (CD-Extra):

1. Session 1 contains audio tracks
2. Session 2 contains data track(s)
3. The "audio leadout" is the start of the first data track (end of session 1)

This distinction matters for AccurateRip calculations, which use the audio session leadout, not the disc leadout.

## 2.4 TOC Parsing (Input Mode)

When parsing TOC input via `-c`, each format has specific validation:

**MusicBrainz format:** `first last leadout offset1 ... offsetN`
- Verify `last - first + 1` equals number of offsets
- Verify all offsets ascending
- Verify leadout > last offset

**AccurateRip format:** `total audio first offset1 ... offsetN leadout`
- Verify `audio <= total`
- Verify offset count equals `total`
- Infer track types: first `audio` tracks from position `first` are audio, remainder are data

**FreeDB format:** `count offset1 ... offsetN total_seconds`
- Offsets are +150 adjusted (convert to raw LBA by subtracting 150)
- `total_seconds` used to derive leadout: `total_seconds * 75`

---

# 3. Disc ID Calculations

## 3.1 MusicBrainz Disc ID

Calculated by libdiscid using SHA-1 over a specific TOC representation. We use libdiscid directly via `discid_put()` and `discid_get_id()`.

**Standard Audio CDs:**
- Include all tracks
- Use disc leadout

**Enhanced CDs (trailing data track):**
- Exclude trailing data track(s)
- Use audio session leadout (`audio_leadout`)

**Mixed Mode CDs (leading data track):**
- Include all tracks (including the data track at position 1)
- Use disc leadout

## 3.2 FreeDB Disc ID

We calculate this ourselves rather than using libdiscid because libdiscid doesn't provide a FreeDB calculation function—it only calculates MusicBrainz IDs.

Algorithm:

```
n = sum of digit_sum(offset_seconds) for each track
    where offset_seconds = (offset + 150) / 75

t = leadout_seconds - first_track_seconds
    where each is independently floor-divided by 75

disc_id = ((n % 255) << 24) | (t << 8) | track_count
```

Important: The `t` calculation uses independent truncation:
```c
int t = (leadout_frames / 75) - (first_offset_frames / 75);  // Correct
// NOT: int t = (leadout_frames - first_offset_frames) / 75;  // Wrong
```

These produce different results due to truncation timing.

The FreeDB disc ID includes **all tracks** (audio and data) in `track_count`, even for Enhanced/Mixed Mode CDs.

## 3.3 AccurateRip Disc ID

Format: `NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX`

Fields:
1. Audio track count (3 digits, zero-padded)
2. Disc ID 1 (8 hex chars)
3. Disc ID 2 (8 hex chars)
4. FreeDB disc ID (8 hex chars)

**Disc ID 1:**
```
sum of all audio track start LBAs + leadout LBA
```

**Disc ID 2:**
```
sum of (max(offset, 1) × track_number) for audio tracks
+ (leadout × (last_audio_track + 1))
```

The `max(offset, 1)` is critical: if an audio track starts at LBA 0, it contributes 1 × track_number, not 0. This ensures the first track always contributes to the checksum.

**Leadout selection:**
- Standard Audio CD: disc leadout
- Enhanced CD: audio session leadout (start of data track)
- Mixed Mode CD: disc leadout (audio tracks follow the data track and extend to disc end)

**FreeDB field:**
The fourth field is the standard FreeDB disc ID, which uses **all tracks** including data. This allows AccurateRip to match discs in the CDDB database while using audio-only checksums for verification.

---

# 4. ISRC Scanning

## 4.1 The Problem

ISRC codes are embedded in Q-subchannel frames. The challenges:

1. **Sparse data**: ISRC appears in Q-mode-3 frames, which occur only ~1 in 100 frames
2. **No random access**: Cannot seek directly to ISRC frames; must read sequentially and filter
3. **Noise**: Physical defects, drive timing, and read errors corrupt subchannel data
4. **CRC failures**: Subchannel CRC catches some errors but not all
5. **Drive variation**: Different drives have different subchannel read quality
6. **Speed**: Reading the entirety of every track would be prohibitively slow
7. **Mastering errors**: Real-world discs exhibit:
   - ISRC bleed from one track to the next (wrong ISRC appears in adjacent track's area)
   - Random frames containing incorrect data even on otherwise good discs
   - Track 1 and track N being notoriously difficult to read reliably (lead-in/lead-out proximity)

Common tools fail because they:
- Read only a small portion of the track
- Accept single-frame matches without validation
- Don't handle CRC failures gracefully

## 4.2 Scanning Strategy

### 4.2.1 Probe-First Approach

Before scanning all tracks, mbdiscid probes a subset to detect whether the disc has ISRCs at all. This is based on the observation that discs generally either have ISRCs for all tracks or have none—partial ISRC encoding is rare.

**For discs with ≥5 audio tracks:**

1. Exclude short tracks from eligibility
2. Select 3 probe tracks at approximately 33%, 50%, 67% positions in the eligible list
3. Avoid track 1 and last track when possible (boundary tracks are less reliable)

If any probe track yields a valid ISRC, scan all tracks. If none do, stop immediately—the disc likely has no ISRCs.

**For discs with <5 audio tracks:**

Skip probing entirely and perform a full scan of all audio tracks. With so few tracks, probing provides little benefit.

### 4.2.2 Tranche-Based Reading

Each track is scanned in multiple **tranches** (stride reads at different positions within the track):

**Configuration:**
- `INITIAL_TRANCHES = 3` — baseline read passes
- `RESCUE_TRANCHES = 1` — additional pass if consensus not achieved
- `FRAMES_PER_TRANCHE = 192` — frames read per tranche (~2.56 seconds)
- `BOOKEND_FRAMES = 150` — frames to avoid at track start and end (2 seconds)

**Rationale for these values:**

The goal is to collect 5-6 valid ISRC frames before voting, providing enough samples for confident consensus.

**ISRC frame density:**
- Red Book specifies ISRC in Q-mode-3 frames, occurring roughly once per 100 frames
- Observed rates: ~1.04-1.30% (approximately 1 per 77-96 frames)
- At 75 frames/second, this means ~0.75-1 ISRC frames per second of audio

**Trade-off analysis:**
- More smaller tranches = more seek overhead, higher chance of landing between ISRC frames
- Fewer larger tranches = less seek overhead, higher probability of capturing ISRC frames
- But more tranches = better chance of sampling non-clustered regions (avoiding localized damage or mismastering)
- Seek time is not trivial—each seek can cost 100-300ms on optical drives, so minimizing tranche count has real performance impact

**Sizing math:**
With ~1.17% ISRC frame rate (observed average):

| Tranches × Size | Total | Expected ISRC frames |
|-----------------|-------|---------------------|
| 3 × 128         | 384   | ~4.5                |
| 3 × 160         | 480   | ~5.6                |
| 3 × 192         | 576   | ~6.7                |
| 3 × 200         | 600   | ~7.0                |

We chose **3 × 192 = 576 frames** as a balance:
- Expected ~6-7 ISRC frames (comfortably above minimum 5)
- 3 tranches provides spatial diversity without excessive seek overhead
- ~2.56 seconds per tranche is long enough to reliably capture ISRC frames even with stride variance

**150-frame bookends (2 seconds):**
- Drives may be unstable immediately after seeking to a new position
- Track boundaries (especially near lead-in/lead-out) have higher error rates
- Avoiding the first and last 2 seconds keeps reads in the stable middle region

**Rescue tranche:**
- If initial 3 tranches produce candidates but no consensus, 1 additional tranche is read
- A single extra sample is usually sufficient to break ties
- More than 1 rescue tranche shows diminishing returns

**Tranche positioning:**

```
Track: [====|=====|=====|=====|====]
       ^                           ^
       |<-- bookend     bookend -->|

       Usable area:
            [=====|=====|=====]
               ^     ^     ^
               T1    T2    T3
```

Tranches are evenly spaced within the usable area (excluding bookends):
```
position[i] = usable_start + (usable_length / (num_tranches + 1)) * (i + 1)
```

### 4.2.3 Short Track Handling

A track is considered "short" if it doesn't have enough length to support:
- Two bookend exclusion zones
- The minimum tranche spacing
- The required number of tranches

The threshold is calculated dynamically:
```c
SHORT_TRACK_THRESHOLD = (2 * BOOKEND_FRAMES) +
                        ((INITIAL_TRANCHES + RESCUE_TRANCHES + 1) * FRAMES_PER_TRANCHE)
```

**For short tracks:**
- Skip tranche-based reading
- Read the entire track from start to finish
- Apply the same consensus logic to all collected frames
- This ensures we don't overrun track boundaries or miss data

### 4.2.4 Consensus Determination

After collecting candidate ISRCs across all tranches:

**Majority rule:**
```c
Accept ISRC if:
  max_count >= 2                              // At least 2 votes for winner
  AND (second_max == 0 OR max_count >= 2 * second_max)  // 2:1 margin over any competitor
```

Both conditions must be met:
1. The winning candidate must appear at least twice (single-frame matches are never accepted)
2. The winner must have at least double the count of any other candidate

**Early termination:**

If ≥64 valid ISRC frames have been collected and consensus is achieved, stop reading additional tranches.

**Rescue sampling:**

If initial tranches produce candidates but no consensus:
1. Recalculate positions for `INITIAL_TRANCHES + RESCUE_TRANCHES` tranches
2. Read only the new rescue tranche position
3. Re-evaluate consensus
4. If still no consensus, mark track as indeterminate (output nothing)

## 4.3 Subchannel Reading

### 4.3.1 Read Modes

Two approaches for reading Q-subchannel:

1. **Batch mode**: Read multiple frames in one SCSI command (preferred)
2. **Single-frame mode**: Read one frame at a time (fallback)

### 4.3.2 Batch Mode Detection (macOS)

On macOS, batch subchannel reading requires exclusive SCSI access. At scan start, mbdiscid tests batch mode:

1. Read 10 frames from a known position (first audio track + 100 frames)
2. Check CRC validity of each frame
3. If any frame has valid CRC, use batch mode with CRC validation
4. Otherwise, fall back to `DKIOCCDREADISRC` ioctl

The ioctl fallback is less reliable because it:
- Performs only a single read (no multiple samples)
- Has no CRC validation
- Cannot perform majority voting
- Returns whatever the drive reports, errors and all

### 4.3.3 CRC Validation

Q-subchannel frames include a 16-bit CRC. Frames failing CRC are discarded before consensus evaluation.

CRC polynomial: x¹⁶ + x¹² + x⁵ + 1 (CRC-16-CCITT)

## 4.4 ISRC Validation

Each candidate ISRC must pass:

1. **CRC check** (frame level)
2. **Format check**: 12 characters, pattern `[A-Z]{2}[A-Z0-9]{3}[0-9]{7}`
3. **Non-zero check**: Not all zeros (`000000000000`)

---

# 5. MCN Reading

## 5.1 MCN Location

The Media Catalog Number (MCN/UPC/EAN) is stored in Q-subchannel mode 2 frames in the lead-in area and may repeat throughout the disc.

## 5.2 Reading Strategy

MCN is read via libdiscid using `discid_read_sparse()` with `DISCID_FEATURE_MCN`. This delegates the reading to libdiscid's implementation, which typically uses the READ SUB-CHANNEL command (0x42) with data format 0x02.

We use libdiscid rather than our own SCSI implementation because:
- MCN reading is straightforward compared to ISRC
- libdiscid handles platform differences
- The drive returns a single MCN value (unlike ISRC which varies per-track)

## 5.3 MCN Validation

MCN must pass:
1. **Length check**: Exactly 13 characters
2. **Format check**: All digits (0-9)
3. **Non-zero check**: Not all zeros (`0000000000000`)

---

# 6. Platform-Specific SCSI Implementation

## 6.1 Linux Implementation

Uses the SG_IO ioctl interface:

```c
struct sg_io_hdr io_hdr;
io_hdr.interface_id = 'S';
io_hdr.cmd_len = cdb_length;
io_hdr.cmdp = cdb;
io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
io_hdr.dxfer_len = buffer_size;
io_hdr.dxferp = buffer;
io_hdr.timeout = timeout_ms;

ioctl(fd, SG_IO, &io_hdr);
```

No special considerations—Linux SCSI is straightforward.

## 6.2 macOS Implementation

macOS requires significantly more complexity:

### 6.2.1 Device Access

1. Convert `/dev/diskN` to `/dev/rdiskN` (raw device) for subchannel access
2. Use IOKit to find the corresponding IOService
3. Use DADiskClaim() for exclusive access (DiskArbitration framework)

### 6.2.2 SCSI Pass-Through

macOS has an ioctl interface (`DKIOCCDREADISRC`) for reading ISRCs, but testing showed it to be unreliable—it performs only a single read with no CRC validation, making it unsuitable for our consensus-based approach.

Instead, we use IOKit for SCSI pass-through:

1. Get the SCSITaskDeviceInterface from IOKit
2. Create a SCSI task
3. Set CDB, transfer direction, buffer
4. Execute and wait for completion
5. Release the task

The `DKIOCCDREADISRC` ioctl is retained only as a fallback when batch subchannel reading fails entirely.

### 6.2.3 Device Release Polling

**Problem:** After releasing exclusive access via DADiskUnclaim(), macOS remounts and indexes the disc. During this window (~1-2 seconds), the device is inaccessible.

**Solution:** After releasing exclusive access, poll the device for accessibility before returning. This involves repeatedly attempting to open the device until either it succeeds (indicating the system has finished remounting) or a timeout is reached (typically 10 seconds). Polling interval is 100ms to avoid busy-waiting.

This allows back-to-back invocations of mbdiscid to succeed without the user having to wait manually.

---

# 7. Verbose Output Architecture

## 7.1 Verbosity Levels

| Level | Flag | Content |
|-------|------|---------|
| 0 | (none) | No diagnostic output |
| 1 | `-v` | High-level operations (device open, mode selected, summary results) |
| 2 | `-vv` | Per-component details (per-track ISRC results, TOC entries, CD-Text blocks) |
| 3 | `-vvv` | Diagnostic data (frame counts, CRC stats, ADR distributions, voting details) |

## 7.2 Output Assignment Guidelines

**Level 1** — High-level operations:
- Device opened successfully
- Mode selected (all, specific mode)
- TOC read summary (track count, audio vs data)
- ISRC scan summary (count found)
- Optional metadata absent
- macOS device release timing

**Level 2** — Per-component details:
- Per-track ISRC results
- TOC entries (offset, length, type, session)
- CD-Text blocks parsed
- Probe track selection

**Level 3** — Diagnostic data:
- Frame counts per tranche
- CRC valid/invalid statistics
- ADR distribution (mode 1/2/3 counts)
- Voting details (candidates and counts)
- Consensus decisions

## 7.3 Prefix Convention

All verbose output uses a prefix indicating the subsystem:

```
device: opening /dev/rdisk19
toc: libdiscid reports tracks 1-14, leadout 310155
toc: full TOC reports tracks 1-15, 2 session(s)
isrc: starting scan
isrc: track 5: USUM70747900 (6/6)
scsi: waited 1.6s for device release
```

## 7.4 Consistency Between Paths

Verbose output should be consistent whether reading from device or parsing TOC input:

**Device read:**
```
toc: libdiscid reports tracks 1-14, leadout 310155
toc: full TOC reports tracks 1-15, 2 session(s)
toc: 15 tracks (14 audio, 1 data)
toc: track 1: session 1, offset 0, length 7384, audio
```

**TOC input (MusicBrainz format):**
```
toc: user reports tracks 1-13, leadout 183470
toc: 13 tracks
toc: track 1: offset 0, length 23153
```

Note: TOC input doesn't include audio/data distinction (except AccurateRip format) or session info, so those fields are omitted.

---

# 8. Error Handling Strategy

## 8.1 Fail-Fast Principle

Validate early, fail immediately:

1. CLI parsing errors → exit before any device access
2. Device open failure → exit before reading
3. TOC read failure → exit before ID calculation

## 8.2 Graceful Degradation for Optional Data

For Text, MCN, ISRC:

- Failure to read → empty output, success exit code
- Partial data → output what we have
- Never error on missing optional metadata

## 8.3 SCSI Error Handling

SCSI commands can fail for many reasons:

- Device busy → retry with backoff
- Media error → report as I/O error
- Invalid command → fall back to alternative approach if available
- Timeout → retry once, then fail

## 8.4 Error Messages

Format: `mbdiscid: <message>`

Messages should be:
- Specific enough to diagnose
- Not implementation-revealing
- Consistent across platforms

---

# 9. Testing Considerations

## 9.1 Test Categories

1. **TOC parsing**: All three input formats, edge cases (single track, 99 tracks, data-only)
2. **Disc ID calculation**: Known-good discs with verified IDs from AccurateRip/MusicBrainz databases
3. **Enhanced CD handling**: Verify audio leadout calculation, track type detection
4. **Mixed Mode handling**: Verify first-track-data detection, audio track numbering
5. **ISRC accuracy**: Compare against other tools, manual verification on test discs
6. **Platform parity**: Same disc should produce identical output on Linux vs macOS

## 9.2 Test Disc Types

Minimum coverage:

| Type | Description | Key Tests |
|------|-------------|-----------|
| Audio CD | Standard CD-DA | Basic functionality |
| Enhanced CD | CD-Extra with data session | Audio leadout, session handling |
| Mixed Mode CD | Data track 1, audio tracks 2+ | Track type detection, first audio track |
| CD with MCN | Has Media Catalog Number | MCN reading |
| CD with ISRCs | Has per-track ISRCs | ISRC scanning accuracy |
| CD without ISRCs | No ISRCs encoded | Graceful empty output |
| Short tracks | <15 second tracks | Short track handling |

## 9.3 Regression Testing

The test harness (`test.sh`) should:

1. Define expected outputs for known discs
2. Test all modes and action combinations
3. Verify exit codes for error conditions
4. Compare verbose output structure (not exact content)

---

# Document Metadata

**Title:** mbdiscid Implementation Specification  
**Version:** 1.0  
**Date:** 2025-06-11  
**Author:** Ian McNish  
**License:** Creative Commons Attribution 4.0 International (CC BY 4.0)

**Related Documents:**
* [README](README.md) — Quick start and overview
* [Product Specification](Product_Specification.md) — Observable behavior and output formats
