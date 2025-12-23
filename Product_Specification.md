# mbdiscid Product Specification

# 1. Purpose & Scope

This document specifies the **observable behavior** of the `mbdiscid` command-line tool. It defines:

* What inputs the tool accepts (devices, TOC formats, options)
* What outputs it produces (disc IDs, TOCs, metadata)
* How each mode behaves under normal and edge-case conditions
* What errors and exit codes the tool emits
* How the tool behaves across supported platforms (Linux and macOS)

This document does **not** describe internal implementation details except where they affect observable results. For implementation details, see [Implementation Specification](Implementation_Specification.md).

---

## 1.1 Tool Overview

`mbdiscid` is a **stand-alone command-line tool** that:

* Reads a **physical CD device** or **TOC-style input**
* Computes disc IDs for:
  * AccurateRip
  * FreeDB/CDDB (legacy)
  * MusicBrainz
* Reads additional metadata that **only exists on a real disc**:
  * Media type classification
  * CD-Text (album-level and track-level textual metadata)
  * MCN (Media Catalog Number)
  * ISRCs (per track)

`mbdiscid` is strictly **read-only**. It never modifies discs or devices and never requires elevated privileges.

---

## 1.2 Intended Audience

This specification is written for:

* Developers implementing or maintaining the `mbdiscid` binary
* Advanced users integrating the tool into automation workflows
* Testers validating program behavior
* Packagers distributing the tool on Linux or macOS

It assumes familiarity with CD structures (tracks, leadout, audio/data) and command-line programs.

---

## 1.3 In Scope

This specification covers:

* The command-line interface
* Option interactions, mutual exclusions, and defaults
* Mode behaviors (Type, Text, MCN, ISRC, Raw, AccurateRip, FreeDB, MusicBrainz, All)
* Exact output formats and section ordering
* ISRC validity rules and output guarantees
* CD-Text field mapping to canonical tag names
* Exit codes and error message formatting
* Device handling rules on Linux and macOS
* Verbosity behavior (`-v`, `-vv`, etc.)
* Platform portability requirements

---

## 1.4 Out of Scope

Explicitly out of scope:

* Internal algorithms (see [Implementation Specification](Implementation_Specification.md))
* Library choices, data structures, or specific API calls
* Non-Linux/non-macOS OS support
* GUI interfaces or wrappers
* Network I/O (the tool does not contact servers)

---

## 1.5 Design Principles

`mbdiscid` is designed around these principles:

1. **Stable, scriptable output**
   * Deterministic formatting
   * Parsable, section-based structures
   * Consistent exit codes from `sysexits.h`

2. **Read-only and safe**
   * No writes or destructive device operations
   * Must not require root privileges

3. **Clarity of semantics**
   * Modes define *what* information is read or computed
   * Actions define *what is displayed*
   * Modifiers affect *behavior*, not semantics

4. **Accuracy and robustness**
   * ISRC extraction uses consensus-based validation
   * TOC parsing is strict; malformed input is an error

5. **Portability**
   * Behavior is consistent across Linux and macOS
   * Minimal assumptions about device enumeration or OS APIs

---

## 1.6 Document Structure

1. [Purpose & Scope](#1-purpose--scope)
2. [Terminology & Concepts](#2-terminology--concepts)
3. [Command-Line Interface (CLI)](#3-command-line-interface-cli)
4. [Modes & Actions](#4-modes--actions)
5. [ISRC Acquisition](#5-isrc-acquisition)
6. [Output Formatting](#6-output-formatting)
7. [Exit Codes & Error Behavior](#7-exit-codes--error-behavior)
8. [Device Handling & Platform Behavior](#8-device-handling--platform-behavior)
9. [Logging & Verbosity](#9-logging--verbosity)
10. [Security & Safety Considerations](#10-security--safety-considerations)
11. [Portability & Platform Requirements](#11-portability--platform-requirements)
12. [Document Metadata](#12-document-metadata)

---

# 2. Terminology & Concepts

This section defines all normative terminology used throughout the specification.

---

## 2.1 Core Concepts

### 2.1.1 TOC (Table of Contents)

The **TOC** describes the structure of a compact disc:

* Track count
* Track start positions (LBAs)
* Lead-out position
* Track types (audio vs data)

TOC formats differ across systems and services:

* Raw TOC (MMC / device-native)
* AccurateRip TOC format
* FreeDB/CDDB TOC format
* MusicBrainz TOC format

These formats are defined in [§2.11 TOC Format Definitions](#211-toc-format-definitions).

---

### 2.1.2 TOC Input (Command-Line)

When the user specifies `-c`, TOC data is supplied via command-line arguments or stdin instead of reading from a device.

Properties:

* The expected format depends on **Mode** (AccurateRip, FreeDB, or MusicBrainz)
* TOC input **cannot** be used for Type, Text, MCN, or ISRC extraction—these require disc access

---

### 2.1.3 Disc TOC (Device TOC)

The **Disc TOC** is the TOC obtained from the physical disc using MMC/SCSI commands.

It:

* **Must** be used for Type (`-T`), Text (`-X`), MCN (`-C`), ISRC (`-I`), and Raw TOC (`-R`) modes
* **May** be used as input to compute AccurateRip, FreeDB, or MusicBrainz identifiers when `-c` is not supplied
* **Does not include** Text, MCN, ISRCs, or other subchannel-dependent fields

---

## 2.2 Disc Types

mbdiscid recognizes three canonical disc types:

| Primary Term | Technical Term | Description |
|--------------|----------------|-------------|
| **Audio CD** | CD-DA | Audio-only disc, one session, all tracks are audio |
| **Enhanced CD** | CD-Extra | Audio tracks in session 1, data track(s) in session 2 |
| **Mixed Mode CD** | Mixed Mode | Data track first (track 1), audio tracks follow |

---

## 2.3 Track Types

Each track is classified as:

* **Audio track**
* **Data track**

---

## 2.4 Disc Identifiers

mbdiscid computes or extracts the following identifiers:

**MCN (Media Catalog Number)** — A disc-level identifier encoded in subchannel data; typically encodes a UPC or EAN barcode. 13-digit numeric string. Physical disc required.

Example: `0602517484016`

**ISRC (International Standard Recording Code)** — A per-track identifier used for royalty tracking and rights management. Stored in Q-subchannel frames. 12-character alphanumeric string: 2-letter country code, 3-character registrant code, 2-digit year, 5-digit designation code. Physical disc required.

Example: `USUM70747896`

**AccurateRip Disc ID** — A checksum derived from audio track LBAs. Four hyphen-separated fields: 3-digit zero-padded audio track count, two 8-character hex checksums, and the FreeDB disc ID.

Example: `014-00209635-01652576-e211510f`

**FreeDB Disc ID** — A CDDB-style checksum derived from all track offsets. 8-character hex string.

Example: `e211510f`

**MusicBrainz Disc ID** — A hash derived from a MusicBrainz-formatted TOC. 28-character Base64-variant string.

Example: `hO3GT18x_9qBZL3vZhhpDexHnv8-`

---

## 2.5 LBA (Logical Block Addressing)

LBA values used throughout this specification:

* Are **0-based** (raw device addressing)
* Represent **75 frames per second**
* Are used directly in Raw, AccurateRip, FreeDB, and MusicBrainz TOC output formats

---

## 2.6 Subchannel Data

Subchannel data includes:

* **CD-Text**
* **MCN**
* **ISRC**

These fields:

* **Must** be read from the disc
* **Cannot** be derived from any TOC
* May contain noise or invalid frames
* Are filtered and validated before output

---

## 2.7 Modes, Actions & Modifiers

* A **Mode** determines *what type of metadata is produced*
* An **Action** determines *what representation of that metadata is output*
* A **Modifier** changes how input is obtained or how errors are handled

Details in [§3](#3-command-line-interface-cli).

---

## 2.8 Validity & Absence Rules

* A value is **valid** when it meets all rules for its type
* A value is **absent** when no valid data exists
* Absence of optional metadata (Text, MCN, ISRC) is **not** an error

Errors arise only when required or structural data is missing (e.g., no readable TOC, malformed input).

---

## 2.9 Normative Document References

* **Red Book** — Audio CD standard
* **Blue Book** — Enhanced CD / CD-Extra standard
* **MMC / SCSI Commands** — Mechanisms for reading disc TOC and subchannels
* **RFC 2119** — Requirement level definitions

---

## 2.10 Summary of Key Concepts

| Term | Meaning | Disc Required?¹ |
|------|---------|-----------------|
| TOC | Table of Contents (Raw/AccurateRip/FreeDB/MusicBrainz) | No |
| Disc TOC | Device-read TOC | Yes |
| TOC Input | User-supplied TOC (mode-dependent format) | No |
| CD-Text | Album/track textual metadata | Yes |
| MCN | Media Catalog Number | Yes |
| ISRC | Per-track ID in subchannel Q | Yes |
| Audio Track | Track with audio content | — |
| Data Track | Track without audio content | — |

¹ TOC, AccurateRip, FreeDB, and MusicBrainz IDs can be calculated from TOC input via `-c`

---

## 2.11 TOC Format Definitions

This subsection defines the exact format for each TOC type. These formats apply to both input ([§3.5](#35-toc-input)) and output ([§6.2](#62-output-sections)).

All formats include **all tracks** (audio and data) unless otherwise noted. The distinction between audio and data tracks affects disc ID calculations as described in [Implementation Specification](Implementation_Specification.md).

### 2.11.1 Raw TOC Format

```
<first> <last> <offset1> <offset2> ... <offsetN> <leadout>
```

| Field | Description |
|-------|-------------|
| `first` | First track number (always 1) |
| `last` | Last track number |
| `offset1..N` | LBA of each track's starting position (all tracks, in disc order) |
| `leadout` | LBA of the lead-out |

The Raw TOC format is auto-detected and accepted as input for MusicBrainz (`-Mc`) and FreeDB (`-Fc`) modes.

AccurateRip (`-Ac`) requires the `--assume-audio` modifier to accept raw TOC format. AccurateRip IDs are calculated from both total track count and audio-only details, but raw TOC doesn't distinguish between audio and data tracks. The `--assume-audio` modifier assumes all tracks are audio, which produces incorrect results for Enhanced or Mixed Mode CDs.

### 2.11.2 AccurateRip TOC Format

```
<track_count> <audio_count> <first_audio> <offset1> <offset2> ... <offsetN> <leadout>
```

| Field | Description |
|-------|-------------|
| `track_count` | Total track count (audio + data) |
| `audio_count` | Number of audio tracks |
| `first_audio` | First audio track number (1 for Audio CD and Enhanced CD; 2 for Mixed Mode CD where track 1 is data) |
| `offset1..N` | LBA of each track's starting position (all tracks, in disc order) |
| `leadout` | LBA of the lead-out |

Constraints:

* `audio_count` must be ≤ `track_count`
* Number of offsets must equal `track_count`
* Offsets must be in strictly ascending order
* `leadout` must be greater than all offsets

### 2.11.3 FreeDB TOC Format

```
<track_count> <offset1> <offset2> ... <offsetN> <total_seconds>
```

| Field | Description |
|-------|-------------|
| `track_count` | Number of tracks (audio + data) |
| `offset1..N` | Frame offset of each track (LBA + 150), all tracks in disc order |
| `total_seconds` | Total disc length in seconds |

Constraints:

* Number of offsets must equal `track_count`
* Offsets must be in strictly ascending order
* `total_seconds` must be consistent with the final offset

### 2.11.4 MusicBrainz TOC Format

```
<first> <last> <leadout> <offset1> <offset2> ... <offsetN>
```

| Field | Description |
|-------|-------------|
| `first` | First track number (always 1) |
| `last` | Last track number |
| `leadout` | LBA of the lead-out (audio session leadout for Enhanced CDs) |
| `offset1..N` | LBA of each track's starting position |

For Enhanced CDs, MusicBrainz format includes only audio tracks (session 1), so `last` is the last audio track number and `leadout` is the audio session leadout (start of data track).

Constraints:

* Track count must equal `last - first + 1`
* Number of offsets must equal track count
* All offsets must be in strictly ascending order
* All values must be non-negative integers
* `leadout` must be greater than all offsets

---

# 3. Command-Line Interface (CLI)

This section defines all normative rules governing command-line usage, option categories, default behaviors, and argument-processing semantics.

---

## 3.1 Invocation Forms

mbdiscid supports these invocation patterns:

```
mbdiscid [options] <DEVICE>
mbdiscid [options] -c <TOC>
mbdiscid [options] -c
mbdiscid -L
mbdiscid -h
mbdiscid -V
```

**Exactly one** of the following is required:

* `<DEVICE>` — Read from a physical CD device
* `-c <TOC>` — Calculate from TOC data supplied as argument
* `-c` (no argument) — Calculate from TOC data supplied via stdin
* `-L` — List drives (standalone)
* `-h` — Show help (standalone)
* `-V` — Show version (standalone)

Where:

* `<DEVICE>` specifies a CD device path
  * Linux: `/dev/sr0`, `/dev/cdrom`, etc.
  * macOS: `/dev/rdiskN` (raw devices recommended; see [§8](#8-device-handling--platform-behavior))
* `-c <TOC>` indicates TOC input supplied via command line (mode-dependent format)
* `-c` alone indicates TOC input from stdin

Device access is required for Type (`-T`), Text (`-X`), MCN (`-C`), and ISRC (`-I`) modes.

---

## 3.2 Option Categories

### 3.2.1 Modes (mutually exclusive)

A **Mode** selects *what kind of metadata mbdiscid operates on*:

| Mode | Long Form | Purpose |
|------|-----------|---------|
| `-T` | `--type` | Media type classification |
| `-X` | `--text` | CD-Text (album + track textual metadata) |
| `-C` | `--catalog` | MCN (Media Catalog Number) |
| `-I` | `--isrc` | ISRC reading |
| `-R` | `--raw` | Raw TOC |
| `-A` | `--accuraterip` | AccurateRip ID and TOC |
| `-F` | `--freedb` | FreeDB/CDDB ID and TOC |
| `-M` | `--musicbrainz` | MusicBrainz ID and TOC |
| `-a` | `--all` | Combined output of all supported sections |

Rules:

* Modes are mutually exclusive
* If multiple modes are supplied → error (see [§7](#7-exit-codes--error-behavior))
* `-a` counts as a mode

---

### 3.2.2 Actions (may be combined)

Actions modify *what representation* of the mode's data is output:

| Action | Long Form | Meaning |
|--------|-----------|---------|
| `-t` | `--toc` | Display TOC (mode-specific representation) |
| `-i` | `--id` | Display disc ID or value |
| `-u` | `--url` | Display submission/verification URL (MusicBrainz only) |
| `-o` | `--open` | Open the MusicBrainz URL in a browser (no output) |

Notes:

* These actions may appear in any order on the command line
* `-o` triggers a browser open and produces no output itself

---

### 3.2.3 Modifiers

Modifiers affect how input is obtained or how errors are presented:

| Modifier | Long Form | Meaning |
|----------|-----------|---------|
| `-c` | `--calculate` | Use TOC input instead of a device |
| `-q` | `--quiet` | Suppress diagnostic error messages |
| — | `--assume-audio` | Assume all tracks are audio when using raw TOC with `-Ac` |

The `--assume-audio` modifier:

* Is only valid with `-Ac` (AccurateRip mode with TOC input)
* Enables raw TOC format input for AccurateRip calculations
* Assumes all tracks in the TOC are audio tracks
* **Warning:** Produces incorrect results for Enhanced CDs or Mixed Mode CDs

---

### 3.2.4 Standalone Options

These options execute immediately and ignore all other options:

| Option | Long Form | Meaning |
|--------|-----------|---------|
| `-L` | `--list-drives` | List available optical drives |
| `-h` | `--help` | Display help and exit |
| `-V` | `--version` | Display version and exit |

#### Version Output (`-V`)

The `-V` option outputs:

1. Program name and version (one line)
2. Linked library versions (one line per library)

Example:

```
mbdiscid 1.0.0
libdiscid 0.6.4
```

Rules:

* Only libraries that provide CD access functionality are listed
* Library lines appear in alphabetical order
* No additional text (copyright, build info, etc.)

---

### 3.2.5 Verbose Mode

The `-v` flag enables verbose output. It may be repeated for increased verbosity:

| Flag | Level |
|------|-------|
| `-v` | Verbose |
| `-vv` | More verbose |
| `-vvv` | Maximum verbosity |

Verbose output goes to **stderr** and does not affect stdout content.

Details in [§9](#9-logging--verbosity).

---

## 3.3 Default Behaviors

1. **No mode + no action → default to `-a` (All Mode)**

2. **Mode only (e.g., `-M`) → default action is `-i`**

3. **Action only (e.g., `-i`) → default mode is MusicBrainz (`-M`)**

4. **`-c` only → equivalent to `-Mic`**

5. **Mode + `-c` → use that mode's input format; default action remains `-i`**

6. **`-a` with any action → all actions except `-o` are enabled by default**

7. **`-u` and `-o` are only valid in MusicBrainz mode or All mode**

---

## 3.4 Invalid & Disallowed Combinations

### 3.4.1 Missing input source

Exactly one of the following must be provided:

* `<DEVICE>` argument
* `-c` with TOC data (argument or stdin)
* `-L`, `-h`, or `-V` (standalone options)

Providing none, or providing multiple (e.g., both `<DEVICE>` and `-c`), is an error.

### 3.4.2 Disc-required modes used with `-c`

These modes require physical disc access and cannot accept TOC input:

* `-T` (Media Type)
* `-X` (Text)
* `-C` (MCN)
* `-I` (ISRC)
* `-R` (Raw TOC)
* `-a` (All Mode)

### 3.4.3 URL/Open outside applicable modes

These must error:

* `-u` with any mode except `-M` or `-a`
* `-o` with any mode except `-M` or `-a`

### 3.4.4 Multiple modes supplied

Examples that must error:

* `-M -A`
* `-C -I`
* `-A -a`

### 3.4.5 `--assume-audio` without `-Ac`

The `--assume-audio` modifier requires both AccurateRip mode (`-A`) and TOC input (`-c`). Using it with any other mode or without `-c` is an error.

---

## 3.5 TOC Input

When supplying TOC data via `-c`, the expected input format depends on the active mode (see [§2.11](#211-toc-format-definitions)):

| Mode | Input Format |
|------|--------------|
| AccurateRip (`-A`) | AccurateRip TOC (or raw TOC with `--assume-audio`) |
| FreeDB (`-F`) | FreeDB TOC (or raw TOC, auto-detected) |
| MusicBrainz (`-M`) | MusicBrainz TOC (or raw TOC, auto-detected) |

### 3.5.1 Raw TOC Auto-Detection

For MusicBrainz (`-Mc`) and FreeDB (`-Fc`) modes, raw TOC format is automatically detected and accepted. The tool distinguishes between formats based on field positions and value ranges.

### 3.5.2 AccurateRip with Raw TOC

AccurateRip mode (`-Ac`) requires the AccurateRip TOC format by default because it needs explicit audio/data track information. To use raw TOC format with AccurateRip:

1. Specify `--assume-audio`
2. All tracks will be treated as audio
3. This is correct only for standard audio CDs (CD-DA)

**Warning:** Using `--assume-audio` with Enhanced CDs or Mixed Mode CDs produces incorrect AccurateRip disc IDs.

If input fails to match the expected format, mbdiscid exits with `EX_DATAERR`.

Example invocations:

```bash
# MusicBrainz format
mbdiscid -Mc "1 12 198592 150 17477 ..."

# Raw TOC (auto-detected for -Mc)
mbdiscid -Mc "1 12 150 17477 ... 198592"

# AccurateRip format
mbdiscid -Ac "12 12 1 0 17327 ... 198442"

# Raw TOC with --assume-audio for AccurateRip
mbdiscid -Ac --assume-audio "1 12 150 17477 ... 198592"

# From stdin
echo "1 12 198592 150 17477 ..." | mbdiscid -c
```

---

## 3.6 Quiet Mode

`-q` suppresses:

* Nonfatal diagnostics
* Human-facing error text

`-q` does **not** suppress:

* Exit codes
* Mode/action validation
* Output sections required by the chosen mode/action

---

## 3.7 Device Argument

If `<DEVICE>` is supplied:

* It is interpreted as a literal path to a device node
* macOS: mbdiscid attempts raw device fallback (see [§8.4](#84-macos-behavior))
* If the device is not readable, mbdiscid returns an error

---

## 3.8 Drive Listing (`-L`)

`-L` must:

* Ignore all other options
* Output zero or more lines representing drives
* Exit successfully (`EX_OK`)

Details in [§8.7](#87-device-enumeration--l).

---

# 4. Modes & Actions

This section defines each mode's behavior, valid actions, required inputs, and constraints.

---

## 4.1 Mode and Action Availability

| Mode | Disc Required | Accepts `-c` | `-t` | `-i` | `-u` | `-o` |
|------|:-------------:|:------------:|:----:|:----:|:----:|:----:|
| Type (`-T`) | ✓ | — | — | ✓ | — | — |
| Text (`-X`) | ✓ | — | — | ✓ | — | — |
| MCN (`-C`) | ✓ | — | — | ✓ | — | — |
| ISRC (`-I`) | ✓ | — | — | ✓ | — | — |
| Raw (`-R`) | ✓ | — | ✓ | — | — | — |
| AccurateRip (`-A`) | — | ✓ | ✓ | ✓ | — | — |
| FreeDB (`-F`) | — | ✓ | ✓ | ✓ | — | — |
| MusicBrainz (`-M`) | — | ✓ | ✓ | ✓ | ✓ | ✓ |
| All (`-a`) | ✓ | — | ✓ | ✓ | ✓ | — |

Notes:

* Type, Text, MCN, ISRC modes require subchannel or session data → physical disc required
* AccurateRip, FreeDB, MusicBrainz modes may compute results from TOC input
* `-i` yields "value" for Type, Text, MCN, and ISRC modes rather than a computed hash

---

## 4.2 Media Type Mode (`-T`)

Classifies discs as Audio CD, Enhanced CD, or Mixed Mode CD (see [§2.2](#22-disc-types)).

**Inputs:** Requires disc; TOC input not accepted

**Valid actions:** `-i`

---

## 4.3 Text Mode (`-X`)

Outputs CD-Text, if present, including album-level fields and per-track textual metadata.

**Inputs:** Requires disc; TOC input not accepted

**Valid actions:** `-i`

**Output format:** See [§6.2.2](#622-text-section)

---

## 4.4 MCN Mode (`-C`)

Reads the Media Catalog Number from Q-subchannel data.

**Inputs:** Requires disc; TOC input not accepted

**Valid actions:** `-i`

---

## 4.5 ISRC Mode (`-I`)

Reads per-track ISRCs from Q-subchannel data.

**Inputs:** Requires disc; TOC input not accepted

**Valid actions:** `-i`

**Notes:** ISRC acquisition details in [§5](#5-isrc-acquisition)

---

## 4.6 Raw Mode (`-R`)

Outputs the raw device TOC in single-line textual format (see [§2.11.1](#2111-raw-toc-format)).

**Inputs:** Requires disc; TOC input not accepted

**Valid actions:** `-t`

---

## 4.7 AccurateRip Mode (`-A`)

Outputs the AccurateRip-formatted TOC and AccurateRip Disc ID.

**Inputs:** Accepts TOC input (`-c`) using AccurateRip format. Raw TOC format is accepted with `--assume-audio`. If `-c` is not used, the Disc TOC is read and converted.

**Valid actions:** `-t`, `-i`

---

## 4.8 FreeDB Mode (`-F`)

Outputs the FreeDB/CDDB style TOC and FreeDB Disc ID.

**Inputs:** Accepts TOC input (`-c`) using FreeDB format or raw TOC format (auto-detected). If `-c` is not used, the Disc TOC is read and converted.

**Valid actions:** `-t`, `-i`

**Notes:** FreeDB was discontinued in 2020; this mode is provided for legacy compatibility.

---

## 4.9 MusicBrainz Mode (`-M`)

Produces the MusicBrainz TOC, MusicBrainz Disc ID, and optional submission URL.

**Inputs:** Accepts TOC input (`-c`) using MusicBrainz format or raw TOC format (auto-detected). If `-c` is not used, the Disc TOC is read and converted.

**Valid actions:** `-t`, `-i`, `-u`, `-o`

---

## 4.10 All Mode (`-a`)

All Mode produces all supported sections in a fixed canonical order (see [§6.1.4](#614-section-ordering)).

**Inputs:** Requires disc; TOC input not accepted

**Valid actions:** `-t`, `-i`, `-u`, `-o`

---

# 5. ISRC Acquisition

This section defines the observable behavior and guarantees for ISRC extraction.

For implementation details including scanning algorithms, parameter choices, and platform-specific mechanisms, see [Implementation Specification](Implementation_Specification.md).

---

## 5.1 The Challenge

ISRC codes are stored in the Q-subchannel of audio tracks. Reading this data reliably is difficult because:

* **Subchannel noise**: Physical media defects, drive variations, and read timing cause corrupted frames
* **Sparse encoding**: ISRC data appears only in specific subchannel frames, not continuously
* **No error correction**: Unlike audio data, subchannel data has minimal error protection
* **Mastering errors**: Some discs contain conflicting or malformed ISRCs

Common tools (`icedax`, `cd-info`, `cdrdao`) often produce incomplete or incorrect results because they rely on single reads or minimal validation.

---

## 5.2 Guarantees

mbdiscid addresses these challenges with the following guarantees:

### 5.2.1 Consensus-Based Validation

An ISRC is only output when multiple independent reads produce consistent results. A single frame match is never sufficient.

### 5.2.2 Strict Format Validation

Every ISRC candidate must conform to the IFPI format:

* 2 uppercase letters (country code)
* 3 alphanumeric characters (registrant code)
* 2 digits (year)
* 5 digits (designation code)
* Total: 12 characters

Invalid formats are rejected regardless of read consistency.

### 5.2.3 CRC Validation

Subchannel frames include CRC bytes. Frames failing CRC validation are discarded before consensus evaluation.

### 5.2.4 All-Zero Rejection

ISRCs consisting entirely of zeros (`000000000000`) are treated as absent, not valid.

### 5.2.5 No False Positives

When consensus cannot be achieved (conflicting values, insufficient valid frames), no ISRC is output for that track. The tool prefers silence over incorrect data.

---

## 5.3 Scope

### 5.3.1 Audio Tracks Only

Data tracks do not contain ISRCs. Only audio tracks are scanned.

### 5.3.2 Per-Track Independence

Each track's ISRC is determined independently. A failed or indeterminate result on one track does not affect others.

### 5.3.3 Short Tracks

Very short tracks (a few seconds) may not contain enough subchannel frames for reliable consensus. These tracks are handled specially to maximize accuracy while acknowledging physical limitations.

---

## 5.4 Output Rules

For each audio track:

* A track appears in ISRC output **only if** it produced a validated ISRC via consensus
* A track is **omitted** if:
  * No valid candidates were found
  * Consensus could not be achieved
  * Only invalid or all-zero candidates were found

Tracks with ISRCs appear in ascending track-number order.

---

# 6. Output Formatting

This section defines all normative output rules across all modes.

---

## 6.1 General Output Rules

### 6.1.1 All Mode section structure

In All Mode (`-a`), output consists of multiple sections. Each section has:

* A **header line**: `----- <Section Name> -----`
* A **body**: One or more lines of content
* **Separation**: Exactly one blank line between sections
* **No trailing blank line** after the last section

Sections with no data are omitted entirely (no header, no placeholder).

### 6.1.2 Single-mode output structure

In single-mode runs (e.g., `-M`, `-I`):

* **No headers** are printed
* Only the requested content appears
* If no valid data exists, output is empty

The body content is identical to what would appear in All Mode.

### 6.1.3 Output ordering within a section

When multiple actions are specified, output appears in canonical order:

1. TOC (if `-t`)
2. ID (if `-i`)
3. URL (if `-u`)

### 6.1.4 Section ordering

In All Mode (`-a`), sections appear in this order (if present):

1. Media (Media Type)
2. Text (CD-Text)
3. MCN
4. ISRC
5. Raw (Raw TOC)
6. AccurateRip
7. FreeDB
8. MusicBrainz

### 6.1.5 Required structured data

The following must be present when applicable:

* Raw, AccurateRip, FreeDB, MusicBrainz TOCs
* AccurateRip, FreeDB, MusicBrainz disc IDs
* Media Type (always determinable from disc TOC)

Failure to compute a required identifier results in an error.

---

## 6.2 Output Sections

### 6.2.1 Media Section

Header (All Mode only):

```
----- Media -----
```

Body includes:

1. Primary disc type (one line): `Audio CD`, `Enhanced CD`, or `Mixed Mode CD`
2. Technical disc type (one line): `CD-DA`, `CD-Extra`, or `Mixed Mode`
3. Total tracks (one line): `<N> tracks`
4. Track count summary (one line, Enhanced/Mixed Mode only): `<N> audio tracks, <M> data track(s)` — listed in disc order
5. Blank line
6. Track table

Audio CDs do not print a track breakdown line.

#### Track Table Format

The track table has a header row and one row per track, plus a leadout row:

```
         ----- Start -----  ----- Length -----
S#  T#        MSF      LBA       MSF       LBA  Type   Ch  Pre
 1   1   00:02:00        0  01:38:34      7384  audio   2  no
 1   2   01:40:34     7384  05:48:00     26100  audio   2  no
...
 2  15   71:29:30   321555  02:26:23     10973  data    -  -  
 -  LO   73:55:53   332528         -     746MB  -       -  -
```

| Column | Description |
|--------|-------------|
| S# | Session number (1, 2, etc.) |
| T# | Track number, or `LO` for leadout |
| MSF (Start) | Start position in MM:SS:FF format |
| LBA (Start) | Start position as raw LBA |
| MSF (Length) | Track length in MM:SS:FF format |
| LBA (Length) | Track length in frames (leadout shows total size) |
| Type | `audio` or `data` |
| Ch | Number of audio channels (2 for stereo), `-` for data |
| Pre | Pre-emphasis flag (`yes` or `no`), `-` for data |

Example (Enhanced CD):

```
----- Media -----
Enhanced CD
CD-Extra
15 tracks
14 audio tracks, 1 data track

         ----- Start -----  ----- Length -----
S#  T#        MSF      LBA       MSF       LBA  Type   Ch  Pre
 1   1   00:02:00        0  01:38:34      7384  audio   2  no
 1   2   01:40:34     7384  05:48:00     26100  audio   2  no
 1   3   07:28:34    33484  04:00:62     18062  audio   2  no
 1   4   11:29:21    51546  04:21:47     19622  audio   2  no
 1   5   15:50:68    71168  05:27:66     24591  audio   2  no
 1   6   21:18:59    95759  04:39:07     20932  audio   2  no
 1   7   25:57:66   116691  04:24:52     19852  audio   2  no
 1   8   30:22:43   136543  04:54:05     22055  audio   2  no
 1   9   35:16:48   158598  04:58:06     22356  audio   2  no
 1  10   40:14:54   180954  04:15:74     19199  audio   2  no
 1  11   44:30:53   200153  05:01:22     22597  audio   2  no
 1  12   49:32:00   222750  05:26:21     24471  audio   2  no
 1  13   54:58:21   247221  07:28:05     33605  audio   2  no
 1  14   62:26:26   280826  09:03:04     40729  audio   2  no
 2  15   71:29:30   321555  02:26:23     10973  data    -  -  
 -  LO   73:55:53   332528         -     746MB  -       -  -
```

---

### 6.2.2 Text Section (CD-Text)

Header (All Mode only):

```
----- Text -----
```

#### CD-Text Tag Names

**Album-scope tags:**

| Tag Name | Red Book Source | Meaning |
|----------|-----------------|---------|
| `ALBUM` | `TITLE` | Album title |
| `ALBUMARTIST` | `PERFORMER` | Primary album artist |
| `LYRICIST` | `SONGWRITER` | Album lyricist |
| `COMPOSER` | `COMPOSER` | Album composer |
| `ARRANGER` | `ARRANGER` | Album arranger |
| `GENRE` | `GENRE` | Genre text field |
| `COMMENT` | `MESSAGE` | General album comment |

**Track-scope tags:**

| Tag Name | Red Book Source | Meaning |
|----------|-----------------|---------|
| `TITLE` | `TITLE` | Track title |
| `ARTIST` | `PERFORMER` | Track artist |
| `LYRICIST` | `SONGWRITER` | Track lyricist |
| `COMPOSER` | `COMPOSER` | Track composer |
| `ARRANGER` | `ARRANGER` | Track arranger |
| `COMMENT` | `MESSAGE` | Track comment |

#### Album-scope fields

If any album-scope fields are present, they appear first, one per line, in this order (skipping absent fields):

```
ALBUM: <value>
ALBUMARTIST: <value>
LYRICIST: <value>
COMPOSER: <value>
ARRANGER: <value>
GENRE: <value>
COMMENT: <value>
```

#### Track-scope fields

For each track with at least one CD-Text field:

```
<track number>:
TITLE: <value>
ARTIST: <value>
LYRICIST: <value>
COMPOSER: <value>
ARRANGER: <value>
COMMENT: <value>
```

Rules:

* No indentation
* Tracks appear in ascending track-number order
* Tags within each track appear in the order above, skipping absent tags
* One blank line between album-scope section and first track (if both present)
* One blank line between track blocks

#### CD-Text Normalization

1. Strip trailing null padding
2. Strip `\r` or mixed CRLF artifacts
3. Trim leading and trailing whitespace
4. Convert control characters (ASCII < 0x20) to spaces except `\n`
5. Convert from CD-Text encoding (typically ISO-8859-1) to UTF-8

---

### 6.2.3 MCN Section

Header (All Mode only):

```
----- MCN -----
```

Body: A single line containing the MCN value as a numeric string.

Rules:

* MCN must be exactly 13 digits (0-9 only)
* All-zero MCNs are treated as absent

---

### 6.2.4 ISRC Section

Header (All Mode only):

```
----- ISRC -----
```

Each track that passed ISRC validation appears as:

```
<track number>: <ISRC>
```

Rules:

* Only tracks with validated ISRCs appear
* Track numbers in ascending order
* All-zero ISRCs never appear

---

### 6.2.5 Raw Section

Header (All Mode only):

```
----- Raw -----
```

Body: A single line containing the raw TOC representation.

---

### 6.2.6 AccurateRip Section

Header (All Mode only):

```
----- AccurateRip -----
```

Body (two lines):

1. AccurateRip-format TOC
2. AccurateRip Disc ID

---

### 6.2.7 FreeDB Section

Header (All Mode only):

```
----- FreeDB -----
```

Body (two lines):

1. FreeDB-format TOC
2. FreeDB Disc ID

---

### 6.2.8 MusicBrainz Section

Header (All Mode only):

```
----- MusicBrainz -----
```

Body (three lines):

1. MusicBrainz TOC
2. MusicBrainz Disc ID
3. MusicBrainz URL (using `https://`)

---

## 6.3 Empty Output Cases

### 6.3.1 Optional metadata absent

For optional metadata (Text, MCN, ISRC):

* In All Mode: The entire section is omitted (no header, no placeholder)
* In single mode: Output is empty
* Exit code is `EX_OK` (not an error)

### 6.3.2 Required metadata unavailable

For required metadata (Type, Raw, AccurateRip, FreeDB, MusicBrainz):

* Failure to compute results in an error
* See [§7](#7-exit-codes--error-behavior)

---

# 7. Exit Codes & Error Behavior

mbdiscid uses standardized exit codes from `sysexits.h`.

---

## 7.1 Exit Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | `EX_OK` | Successful termination |
| 64 | `EX_USAGE` | Command line usage error |
| 65 | `EX_DATAERR` | Data format error |
| 69 | `EX_UNAVAILABLE` | Service unavailable |
| 70 | `EX_SOFTWARE` | Internal software error |
| 74 | `EX_IOERR` | Input/output error |

---

## 7.2 Non-Error Conditions (Exit = EX_OK)

The following are not errors:

1. Optional data absent (no CD-Text, no MCN, no ISRCs)
2. Single-mode requests for optional data returning nothing
3. Partial metadata (ISRCs present on some but not all tracks)
4. URL open request (`-o`) failing due to OS/browser issues (warning only)

---

## 7.3 Error Conditions

### 7.3.1 Invalid Command-Line Usage → EX_USAGE

* Mutually exclusive modes specified together
* `-a` combined with any other mode flag
* `-c` used with a mode requiring a physical disc
* `-u` or `-o` used outside MusicBrainz or All mode
* `--assume-audio` used without `-Ac`
* Missing input source (no device, no `-c`, no standalone option)
* Multiple input sources provided
* Unknown flags

### 7.3.2 Unreadable Device / I/O Failure → EX_IOERR

* CD device cannot be opened
* Drive returns a SCSI/IO error
* TOC cannot be read from the disc
* Subchannel data cannot be retrieved

### 7.3.3 Malformed TOC Input → EX_DATAERR

* Too few or too many numeric fields
* Incorrect format for the selected mode
* Non-numeric or negative values
* Impossible or inconsistent leadout/offsets
* Offsets not in ascending order

### 7.3.4 Required Metadata Unavailable → EX_DATAERR

| Mode | Required Data |
|------|---------------|
| `-T` Type | Disc type classification |
| `-R` Raw | Raw TOC |
| `-A` AccurateRip | AR TOC, AR Disc ID |
| `-F` FreeDB | FreeDB TOC, FreeDB Disc ID |
| `-M` MusicBrainz | MB TOC, MB Disc ID |

### 7.3.5 Invalid Media for Requested Operation → EX_UNAVAILABLE

* Attempt to read Text/MCN/ISRC from TOC input
* Attempt to open a device that is not a CD drive

---

## 7.4 Error Message Formatting

All error messages:

1. Single line, human-readable
2. Printed to stderr
3. Prefixed with `mbdiscid: `
4. No trailing periods
5. No stack traces or implementation details

Example:

```
mbdiscid: cannot open device /dev/sr0
```

---

## 7.5 SIGINT / Early Termination

If the user interrupts execution (CTRL-C):

* mbdiscid terminates promptly
* Exit code is `EX_SOFTWARE`
* No partial output is printed

---

# 8. Device Handling & Platform Behavior

This section defines behavior when interacting with CD/DVD hardware across platforms.

---

## 8.1 Supported Platforms

* Linux
* macOS

---

## 8.2 Device Argument Requirements

When invoked with a `<DEVICE>` argument:

* The value is treated as a literal path to a device node
* mbdiscid does not guess, alias, or auto-search for alternative names (except macOS raw device fallback)

---

## 8.3 Device Validation

When opening a device:

1. Attempt to open with minimal required permissions
2. If cannot open → `EX_IOERR`
3. If not a CD-capable device → `EX_UNAVAILABLE`
4. Never requires elevated privileges

---

## 8.4 macOS Behavior

### 8.4.1 Raw vs block devices

macOS provides both:

* Block devices: `/dev/diskN`
* Raw/character devices: `/dev/rdiskN`

Subchannel access (required for Text, MCN, ISRC) typically succeeds only on raw devices.

### 8.4.2 Fallback rule

If the user supplies a block device:

1. Attempt to open it normally
2. If block-device access fails, attempt the corresponding raw device (`/dev/diskN` → `/dev/rdiskN`)
3. If both fail → `EX_IOERR`

This fallback occurs silently.

---

## 8.5 Linux Behavior

### 8.5.1 Supported device naming

* `/dev/srN` (primary)
* `/dev/scdN` (legacy)
* `/dev/cdrom` (symlink)

### 8.5.2 No raw/block pairing logic

Linux does not require distinct raw/block device handling.

---

## 8.6 Device Capability Requirements

### 8.6.1 Required for standard TOC reading

All modes except `-c` require access to the disc TOC. Failure → `EX_IOERR`.

### 8.6.2 Required for Type, Text, MCN, ISRC

These modes require subchannel access. Failure → `EX_IOERR`.

### 8.6.3 Data tracks

Data tracks are valid for TOC and disc ID calculations but are not queried for Text, MCN, or ISRC.

---

## 8.7 Device Enumeration (`-L`)

### 8.7.1 General requirements

* Output to stdout
* One line per drive
* No sorting required
* Format may differ by platform
* If no drives present, output is empty with `EX_OK`
* Errors result in `EX_IOERR`

### 8.7.2 Linux enumeration

Invokes:

```
lsblk -dp -I 11 -o NAME,VENDOR,MODEL,REV
```

Output printed exactly as produced.

### 8.7.3 macOS enumeration

Invokes:

```
drutil status
```

Output printed exactly as produced.

---

## 8.8 Platform-Specific Notes

### 8.8.1 Device formats

Users should supply:

* `/dev/srN` (Linux)
* `/dev/rdiskN` (macOS)

### 8.8.2 No privilege escalation

mbdiscid never attempts `sudo` or privilege elevation. Insufficient permissions → `EX_IOERR`.

---

# 9. Logging & Verbosity

This section defines the behavior of mbdiscid's verbosity system.

---

## 9.1 General Rules

1. Verbose output goes to stderr only—never stdout
2. Verbose output does not change program behavior
3. Verbose output does not leak internal implementation details (memory addresses, raw buffers)
4. All verbose messages are line-oriented

---

## 9.2 Version Banner

At any verbosity level (`-v` or higher), mbdiscid begins stderr output with the same information as `-V` (program version and linked library versions).

---

## 9.3 Verbosity Levels

Verbosity is cumulative:

| Flag | Level |
|------|-------|
| `-v` | Verbose |
| `-vv` | More verbose |
| `-vvv` | Maximum verbosity |

---

## 9.4 Level 1 Verbosity (`-v`)

High-level operational status:

* Device being opened
* macOS raw-device fallback attempts
* TOC acquisition or TOC input parsing confirmation
* Track summary (count, audio vs data)
* Active mode and actions
* Absence of optional metadata
* Early ISRC exit (no ISRCs detected)
* Non-fatal warnings

---

## 9.5 Level 2 Verbosity (`-vv`)

Mid-level diagnostics:

* TOC details (per-track offsets, lengths, types)
* ISRC scanning progress per track
* Probe-track selection
* CD-Text presence/absence details

---

## 9.6 Level 3 Verbosity (`-vvv`)

Maximum diagnostic verbosity:

* ISRC candidate summaries per track
* Voting and decision explanations
* Track skipping explanations
* CD-Text block details
* Device fallback detail (macOS)

---

## 9.7 Disallowed Verbose Output

mbdiscid does not print:

* Raw binary subchannel frames
* Internal memory/struct dumps
* SCSI hex dumps
* Debug traces
* Timing statistics

---

## 9.8 Verbosity & Exit Codes

* Verbosity does not change exit codes
* Errors still emit single-line stderr messages
* Verbose messages do not appear after an error message

---

## 9.9 Interaction With Other Options

* `-v` is allowed with all modes
* Verbose output never changes stdout content or ordering

---

# 10. Security & Safety Considerations

mbdiscid is a read-only, non-destructive utility.

## 10.1 Device Safety

1. **Read-only operation** — Only read-only SCSI/MMC operations; never writes to or modifies a disc
2. **No exclusive locks** — Does not request exclusive access to the drive
3. **No elevated privileges** — Does not require root, sudo, or special capabilities

## 10.2 Input Safety

4. **Strict validation of TOC input** — Malformed input causes an error, not undefined behavior
5. **Disc-based operations require a physical disc** — Subchannel modes cannot be faked via TOC input

## 10.3 Execution Safety

6. **Separation of stdout and stderr** — Diagnostic output never appears on stdout
7. **No shell execution from user input** — Does not invoke shell interpreters with user-controlled input
8. **Browser launch safety (`-o`)** — Uses standard OS mechanisms without shell interpretation

## 10.4 Network & Privacy

9. **No unsolicited network activity** — No network requests; URLs are computed locally
10. **No persistent state** — No configuration files, caches, or logs

---

# 11. Portability & Platform Requirements

## 11.1 Supported Operating Systems

* Linux
* macOS

## 11.2 Functional Equivalence

On all supported platforms:

1. Core functionality behaves consistently
2. Output formats, error semantics, and exit codes are identical
3. Only device enumeration (`-L`) output may vary by platform

## 11.3 Required External Dependencies

* A disc-reading library capable of TOC and subchannel access (e.g., libdiscid)
* The dependency must support Linux and macOS
* The dependency must permit extraction of all metadata required by this specification
* The dependency must enable read-only operation

## 11.4 Architecture Support

* x86_64
* arm64 / aarch64 (Apple Silicon, Raspberry Pi, and other ARM64 Linux systems)

## 11.5 Environment Assumptions

mbdiscid does not require:

* Elevated privileges
* Kernel modules
* System configuration changes
* Custom device drivers
* Persistent state or configuration files

---

# 12. Document Metadata

**Title:** mbdiscid Product Specification  
**Version:** 1.3  
**Date:** 2025-06-11  
**Author:** Ian McNish  
**License:** Creative Commons Attribution 4.0 International (CC BY 4.0)

**Related Documents:**
* [README](README.md) — Quick start and overview
* [Implementation Specification](Implementation_Specification.md) — Internal algorithms and design rationale
