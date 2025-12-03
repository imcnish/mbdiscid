# mbdiscid

Calculate disc IDs and extract metadata from audio CDs, supporting multiple formats:
MusicBrainz, FreeDB/CDDB, AccurateRip, MCN (Media Catalog Number), and ISRCs.

## Features

- **Multiple disc ID formats**: MusicBrainz, FreeDB/CDDB, AccurateRip
- **Metadata extraction**: MCN (UPC/EAN barcode) and per-track ISRCs
- **Flexible input**: Read from CD device or calculate from TOC data
- **Cross-platform**: macOS and Linux support
- **Browser integration**: Open verification URLs directly
- **Scriptable**: Quiet mode for clean pipeline integration

## Requirements

- libdiscid (https://musicbrainz.org/doc/libdiscid)
- C compiler (gcc or clang)
- make

### Installing libdiscid

**macOS (Homebrew):**
```bash
brew install libdiscid
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt install libdiscid-dev
```

**Linux (Fedora):**
```bash
sudo dnf install libdiscid-devel
```

## Building

```bash
make
sudo make install
```

To install to a different prefix:
```bash
make PREFIX=/opt/local
sudo make PREFIX=/opt/local install
```

## Usage

### Default: All Information

```bash
mbdiscid /dev/rdisk16
```

Output:
```
----- MCN -----
0724349532625

----- ISRC -----
1: GBAYE6700012
2: GBAYE6700013
...

----- Raw -----
1 17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 263855

----- AccurateRip -----
17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 263855
017-00231e4f-01bf54d7-e00dbc11

----- FreeDB -----
17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 3518
e00dbc11

----- MusicBrainz -----
1 17 150 19745 32575 42805 54545 72047 85787 95555 117545 145010 150657 160517 178172 193610 215417 231297 244930 263855
m.wjLfLe7XrMz1c_iAL6qo06Q4w-
https://musicbrainz.org/cdtoc/attach?id=m.wjLfLe7XrMz1c_iAL6qo06Q4w-&tracks=17&toc=...
```

### Specific Modes

```bash
# MusicBrainz disc ID only
mbdiscid -M /dev/rdisk16

# AccurateRip disc ID
mbdiscid -A /dev/rdisk16

# FreeDB disc ID
mbdiscid -F /dev/rdisk16

# Media Catalog Number (UPC/EAN)
mbdiscid -C /dev/rdisk16

# ISRCs for all tracks
mbdiscid -I /dev/rdisk16
```

### Combining Actions

```bash
# MusicBrainz TOC and URL
mbdiscid -Mtu /dev/rdisk16

# MusicBrainz disc ID, TOC, URL, and open in browser
mbdiscid -Mituo /dev/rdisk16

# AccurateRip TOC only
mbdiscid -At /dev/rdisk16
```

### Calculating from TOC Data

```bash
# From command line
mbdiscid -c 1 12 150 17477 32100 47997 67160 84650 93732 110667 127377 147860 160437 183097 198592

# From stdin
echo "1 12 150 17477 32100 47997 67160 84650 93732 110667 127377 147860 160437 183097 198592" | mbdiscid -c

# From a file
mbdiscid -c < cdtoc.txt

# Specific mode with calculation
mbdiscid -Ft -c 1 5 150 23437 45102 62385 79845 98450
```

### Scripting

```bash
# Get MCN silently, empty string on failure
mcn=$(mbdiscid -Cq /dev/rdisk16) || mcn=""

# Get MusicBrainz ID for a script
mbid=$(mbdiscid -Miq /dev/rdisk16)
```

## Options

### Mode Options (mutually exclusive)

| Option | Description |
|--------|-------------|
| `-A, --accuraterip` | AccurateRip mode |
| `-C, --catalog` | Media Catalog Number (MCN/UPC/EAN) mode |
| `-F, --freedb` | FreeDB/CDDB mode |
| `-I, --isrc` | ISRC mode |
| `-M, --musicbrainz` | MusicBrainz mode (default when action specified) |
| `-a, --all` | All modes and actions (default) |

### Action Options (combinable)

| Option | Description |
|--------|-------------|
| `-i, --id` | Display disc ID (default when mode specified) |
| `-t, --toc` | Display TOC |
| `-u, --url` | Display verification URL (MusicBrainz only) |
| `-o, --open` | Open URL in browser (MusicBrainz only) |

### Other Options

| Option | Description |
|--------|-------------|
| `-c, --calculate` | Calculate from CDTOC data |
| `-q, --quiet` | Suppress error messages |
| `-h, --help` | Display help |
| `-V, --version` | Display version information |

## Disc ID Formats

| Format | Example |
|--------|---------|
| MusicBrainz | `m.wjLfLe7XrMz1c_iAL6qo06Q4w-` |
| FreeDB | `e00dbc11` |
| AccurateRip | `017-00231e4f-01bf54d7-e00dbc11` |

## TOC Formats

| Format | Structure |
|--------|-----------|
| MusicBrainz | `first last offset1 ... offsetN leadout` |
| FreeDB | `count offset1 ... offsetN total_seconds` |
| AccurateRip | `count offset1 ... offsetN leadout` |

## CDTOC Input Format

CDTOC data consists of space-separated integers:
1. First track number (usually 1)
2. Last track number
3. Track offsets in CD frames (75 frames = 1 second)
4. Leadout offset

Example: `1 5 150 23437 45102 62385 79845 98450`
- 5 tracks (tracks 1-5)
- First track starts at frame 150
- Leadout at frame 98450

## Platform Notes

### macOS
- Use raw device paths: `/dev/rdisk16` (not `/dev/disk16`)
- Block devices may not be accessible even with sudo
- ISRC fallback uses IOKit SCSI commands for reliable reading

### Linux
- Common device paths: `/dev/cdrom`, `/dev/sr0`
- May require appropriate permissions (e.g., `cdrom` group membership)
- ISRC fallback uses SCSI Generic (SG_IO) interface for reliable reading

## Notes

- **FreeDB** was discontinued in 2020
- **MCN** and **ISRC** require a physical disc (cannot use `-c`)
- Not all CDs contain MCN or ISRC data
- **ISRC reliability**: Uses direct MMC commands (READ CD + READ SUB-CHANNEL) as a
  fallback when libdiscid returns incomplete data, significantly improving reliability
  especially for track 1 which is prone to caching issues in many CD drives

## ISRC Detection Strategy

Reading ISRCs from audio CDs presents several reliability challenges that vary by drive, disc, and operating system. Testing across 6 drives, 12 discs, and 2 operating systems (Linux and macOS) revealed the following:

**Observed Issues:**

- **Track 1 failures:** Some drives exhibit ~90% first-read failure rates on track 1, while other drives work reliably. The root cause is not fully understood, but may be related to drive timing or head positioning. Reading a small amount of audio data before querying sub-channel data ("audio warmup") resolves the issue.

- **Last track failures:** Some drives show elevated (~5-10%) failure rates on the last track.

- **Intermittent failures:** Any track can occasionally fail to return ISRC data, even in the middle of repeated successful reads on the same drive and disc.

- **Hallucinated data:** Occasionally drives return properly-formatted but incorrect ISRC data. Format validation cannot detect these errors.

- **Possible caching:** Repeated queries sometimes return what appears to be stale data, which may explain why immediate retries sometimes return the same incorrect result.

**Current Approach:**

mbdiscid uses a two-stage probe to quickly detect whether a disc contains ISRCs:

1. **Quick probe:** A fast probe (5-second timeout, single attempt, no audio warmup) on a middle track to avoid the problematic track 1.

2. **Full probe fallback:** If the quick probe fails, a full probe (30-second timeout, 3 attempts, with audio warmup) on the last track. Track 1 is avoided as fallback since it has the highest failure rate.

3. **Early exit:** If both probes fail, the disc likely has no ISRCs and mbdiscid returns without further attempts.

For discs with 3 or fewer tracks, there are limited options: 2-3 track discs probe track 2; single-track discs must use track 1.

Once ISRC presence is confirmed, all tracks are read using full mode (with audio warmup and retries) to maximize reliability.

This approach reduces detection time for discs without ISRCs from 30-60 seconds to approximately 5-15 seconds (depending on whether the fallback probe is needed), while maintaining reliable detection for discs that do have ISRCs.

**Approaches Not Yet Implemented:**

- Multiple sub-channel reads per track to detect inconsistent or hallucinated data
- Probing two non-edge tracks before falling back to an edge track
- Tuning timeouts

## Changelog

### v1.0.3 (December 2025)
- Added quick probe optimization for fast detection of discs without ISRCs
- Reduced no-ISRC detection time from 30-60 seconds to ~5-15 seconds
- Two-stage probe strategy: quick probe middle track, full probe last track as fallback

### v1.0.2 (December 2025)
- Fixed ISRC reading reliability issues, particularly for track 1
- Added direct MMC command fallback for ISRC extraction on both Linux and macOS
- ISRCs now read consistently across repeated runs

### v1.0.1a (January 2025)
- Initial release with MusicBrainz, FreeDB, AccurateRip, MCN, and ISRC support

## License

MIT License - see LICENSE file for details.

## Author

Ian McNish
