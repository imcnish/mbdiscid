# 1. Purpose & Scope

This document specifies the **observable behavior** of the `mbdiscid` command-line tool (the binary itself). It defines:

* What inputs the tool accepts (devices, TOC/CDTOC formats, options)
* What outputs it produces (disc IDs, TOCs, metadata)
* How each mode behaves under normal and edge-case conditions
* What errors and exit codes the tool emits
* How the tool must behave across supported platforms (Linux and macOS)

This document does **not** describe a protocol, product line, or API — it is a behavioral specification for a single executable program.

The document is intended to be the authoritative reference for how `mbdiscid` must behave externally, without constraining internal implementation details except where they affect observable results.

---

## 1.1 Tool Overview

`mbdiscid` is a **stand-alone command-line tool** that:

* Reads a **physical CD device** or **CDTOC-style input**
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
* ISRC scanning logic and validity rules
* CD-Text field mapping to canonical tag names
* Exit codes and error message formatting
* Device handling rules on Linux and macOS
* Verbosity behavior (`-v`, `-vv`, etc.)
* Platform portability requirements

---

## 1.4 Out of Scope

Explicitly out of scope:

* Internal algorithms except where behavior is externally visible
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
   * ISRC extraction uses consensus/majority rules
   * TOC/CDTOC parsing is strict; malformed input is an error

5. **Portability**
   * Behavior is consistent across Linux and macOS
   * Minimal assumptions about device enumeration or OS APIs

---

## 1.6 Document Structure

This specification is organized into the following sections:

1. [Purpose & Scope](#1-purpose--scope)
2. [Terminology & Concepts](#2-terminology--concepts)
3. [Command-Line Interface (CLI)](#3-command-line-interface-cli)
4. [Modes & Actions](#4-modes--actions)
5. [ISRC Acquisition Logic](#5-isrc-acquisition-logic)
6. [Output Formatting](#6-output-formatting)
7. [Exit Codes & Error Behavior](#7-exit-codes--error-behavior)
8. [Device Handling & Platform Behavior](#8-device-handling--platform-behavior)
9. [Logging & Verbosity](#9-logging--verbosity)
10. [Security & Safety Considerations](#10-security--safety-considerations)
11. [Portability & Platform Requirements](#11-portability--platform-requirements)
12. [Document Metadata](#12-document-metadata)

---

# 2. Terminology & Concepts

This section defines all normative terminology used throughout the specification. Each definition applies globally unless explicitly overridden in a later section.

---

## 2.1 Core Concepts

### 2.1.1 TOC (Table of Contents)

The **TOC** describes the structure of a compact disc:

* Track count
* Track start positions (LBAs)
* Lead-out position
* Track types (audio vs data)

Important notes:

* TOC formats differ across subsystems:
  * Raw TOC (MMC / device-native)
  * AccurateRip TOC format
  * FreeDB/CDDB TOC format
  * MusicBrainz TOC format

These formats are defined in [§2.11 TOC Format Definitions](#211-toc-format-definitions).

When a TOC is supplied via `-c`, it is referred to as a **CDTOC**, and its expected format is dictated by the selected mode.

The **disc-resident TOC** (read from the device) may differ in structure from the computed TOCs used for ID formats.

---

### 2.1.2 CDTOC (Command-Line TOC Input)

A **CDTOC** is a TOC supplied via command-line arguments or stdin when the user specifies `-c`.

Properties:

* The expected CDTOC format depends on **Mode**:
  * AccurateRip (`-A`)
  * FreeDB (`-F`)
  * MusicBrainz (`-M`)
* CDTOC **MUST NOT** be used for Type, Text, MCN, or ISRC extraction. These require disc access.

---

### 2.1.3 Disc TOC (Device TOC)

The **Disc TOC** is the TOC obtained from the physical disc using MMC/SCSI commands.

It:

* **MUST** be used for Type (`-T`), Text (`-X`), MCN (`-C`), ISRC (`-I`), and Raw TOC (`-R`) modes.
* **MAY** be used as input to compute AccurateRip, FreeDB, or MusicBrainz identifiers when `-c` is not supplied.
* **Does not include** Text, MCN, ISRCs, or other subchannel-dependent fields.

---

## 2.2 Disc Types

mbdiscid recognizes three canonical disc types:

| Primary Term      | Technical Term | Governing Spec           | Description                                            |
| ----------------- | -------------- | ------------------------ | ------------------------------------------------------ |
| **Audio CD**      | CD-DA          | Red Book                 | Audio-only disc, one session, all tracks are audio.    |
| **Enhanced CD**   | CD-Extra       | Blue Book                | Audio tracks in session 1, data track(s) in session 2. |
| **Mixed Mode CD** | Mixed Mode     | N/A (standard MMC usage) | Data track first (track 1), audio tracks follow.       |

Notes:

* These classifications affect **AccurateRip** and **Media Type** logic.
* AccurateRip computes checksums from audio track data only, but incorporates the FreeDB disc ID (which uses all tracks) as a field.

---

## 2.3 Track Types

Each track is classified as:

* **Audio track**
* **Data track**

A **short track** is defined in [§5.3.3](#533-short-tracks) for the purpose of selecting ISRC probe candidates.

---

## 2.4 Disc Identifiers

mbdiscid computes or extracts the following identifiers:

**MCN (Media Catalog Number)** — A disc-level barcode encoded in subchannel data (physical disc required). 13-digit numeric string.

Example: `0602517484016`

**ISRC (International Standard Recording Code)** — A per-track identifier stored in Q-subchannel frames (physical disc required). 12-character alphanumeric string: 2-letter country code, 3-character registrant code, 2-digit year, 5-digit designation code.

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

* **Text (CD-Text)**
* **MCN**
* **ISRC**

These fields:

* **MUST** be read from the disc
* **CANNOT** be derived from any TOC or CDTOC
* May contain noise or invalid frames
* Are filtered and interpreted according to [§5 ISRC Acquisition Logic](#5-isrc-acquisition-logic) and [§6.2.2 Text Section](#622-text-section)

---

## 2.7 Modes, Actions & Modifiers

Command-line categories are defined in detail in [§3](#3-command-line-interface-cli), but conceptually:

* A **Mode** determines *what type of metadata is produced*.
* An **Action** determines *what representation of that metadata is output*.
* A **Modifier** changes how input is obtained or how errors are handled.

Combining these yields all user-visible tool behavior.

---

## 2.8 Validity & Absence Rules

Throughout this specification:

* A value is **valid** when it meets all rules for its type
* A value is **absent** when no valid data exists
* Absence of optional metadata (Text, MCN, ISRC) **MUST NOT** be treated as an error

Errors arise only when required or structural data is missing (e.g., no readable TOC, malformed CDTOC input).

---

## 2.9 Normative Document References

* **Red Book** — Audio CD standard
* **Blue Book** — Enhanced CD / CD-Extra standard
* **MMC / SCSI Commands** — Mechanisms for reading disc TOC and subchannels
* **RFC 2119** — Requirement level definitions

These standards are *not reproduced here* but inform definitions and behavior in mode- and data-specific sections.

---

## 2.10 Summary of Key Concepts

| Term        | Meaning                                                    | Disc Required? | Where Defined                               |
| ----------- | ---------------------------------------------------------- | -------------- | ------------------------------------------- |
| TOC         | Table of Contents (Raw/AccurateRip/FreeDB/MusicBrainz)     | No (via -c)    | [§2.1.1](#211-toc-table-of-contents)        |
| Disc TOC    | Device-read TOC                                            | Yes            | [§2.1.3](#213-disc-toc-device-toc)          |
| CDTOC       | User-supplied TOC (mode-dependent)                         | No             | [§2.1.2](#212-cdtoc-command-line-toc-input) |
| CD-Text     | Album/track textual metadata                               | Yes            | [§6.2.2](#622-text-section)                 |
| MCN         | Media Catalog Number                                       | Yes            | [§2.4](#24-disc-identifiers)                |
| ISRC        | Per-track ID in subchannel Q                               | Yes            | [§5](#5-isrc-acquisition-logic)             |
| Audio Track | Track with audio content                                   | No             | [§2.3](#23-track-types)                     |
| Data Track  | Track without audio content                                | No             | [§2.3](#23-track-types)                     |
| Short Track | Track below ISRC sampling threshold                        | Yes            | [§5.3.3](#533-short-tracks)                 |

---

## 2.11 TOC Format Definitions

This subsection defines the exact format for each TOC type. These formats apply to both CDTOC input ([§3.5](#35-cdtoc-input)) and output ([§6.2](#62-output-sections)).

### 2.11.1 Raw TOC Format

```
<first> <last> <offset1> <offset2> ... <offsetN> <leadout>
```

| Field        | Description                           |
| ------------ | ------------------------------------- |
| `first`      | First track number                    |
| `last`       | Last track number                     |
| `offset1..N` | LBA of each track's starting position |
| `leadout`    | LBA of the lead-out                   |

The Raw TOC is output-only and cannot be supplied via `-c`.

### 2.11.2 AccurateRip TOC Format

```
<track_count> <audio_count> <first> <offset1> <offset2> ... <offsetN> <leadout>
```

| Field         | Description                           |
| ------------- | ------------------------------------- |
| `track_count` | Total track count                     |
| `audio_count` | Number of audio tracks                |
| `first`       | First track number                    |
| `offset1..N`  | LBA of each track's starting position |
| `leadout`     | LBA of the lead-out                   |

Constraints:

* `audio_count` MUST be ≤ `track_count`
* Number of offsets MUST equal `track_count`
* Offsets MUST be in strictly ascending order
* `leadout` MUST be greater than all offsets

### 2.11.3 FreeDB TOC Format

```
<track_count> <offset1> <offset2> ... <offsetN> <total_seconds>
```

| Field           | Description                            |
| --------------- | -------------------------------------- |
| `track_count`   | Number of tracks                       |
| `offset1..N`    | Frame offset of each track (LBA + 150) |
| `total_seconds` | Total disc length in seconds           |

Constraints:

* Number of offsets MUST equal `track_count`
* Offsets MUST be in strictly ascending order
* `total_seconds` MUST be consistent with the final offset

### 2.11.4 MusicBrainz TOC Format

```
<first> <last> <leadout> <offset1> <offset2> ... <offsetN>
```

| Field        | Description                           |
| ------------ | ------------------------------------- |
| `first`      | First track number (typically 1)      |
| `last`       | Last track number                     |
| `leadout`    | LBA of the lead-out (end of disc)     |
| `offset1..N` | LBA of each track's starting position |

Constraints:

* Track count MUST equal `last - first + 1`
* Number of offsets MUST equal track count
* All offsets MUST be in strictly ascending order
* All values MUST be non-negative integers
* `leadout` MUST be greater than all offsets

---

# 3. Command-Line Interface (CLI)

This section defines all normative rules governing **command-line usage**, option categories, default behaviors, and general argument-processing semantics. Mode-specific behaviors are defined in [§4 Modes & Actions](#4-modes--actions).

---

## 3.1 Invocation Forms

mbdiscid supports three invocation patterns:

```
mbdiscid [options] <DEVICE>
mbdiscid [options] -c <CDTOC>
mbdiscid [options] -c
```

Where:

* `<DEVICE>` specifies a CD device path
  * Linux: `/dev/sr0`, `/dev/cdrom`, etc.
  * macOS: `/dev/rdiskN` (raw devices strongly recommended; see [§8 Device Handling](#8-device-handling--platform-behavior))
* `-c <CDTOC>` indicates **CDTOC input supplied via command line** (mode-dependent format)
* `-c` alone indicates **CDTOC input from stdin**

Device access is required for Type (`-T`), Text (`-X`), MCN (`-C`), and ISRC (`-I`) modes.

---

## 3.2 Option Categories

Options fall into four primary categories, which have strict interaction rules.

### 3.2.1 Modes (mutually exclusive)

A **Mode** selects *what kind of metadata mbdiscid operates on*:

| Mode | Long Form       | Purpose                                  |
| ---- | --------------- | ---------------------------------------- |
| `-T` | `--type`        | Media type classification                |
| `-X` | `--text`        | CD-Text (album + track textual metadata) |
| `-C` | `--catalog`     | MCN (Media Catalog Number)               |
| `-I` | `--isrc`        | ISRC reading                             |
| `-R` | `--raw`         | Raw TOC                                  |
| `-A` | `--accuraterip` | AccurateRip ID and TOC                   |
| `-F` | `--freedb`      | FreeDB/CDDB ID and TOC                   |
| `-M` | `--musicbrainz` | MusicBrainz ID and TOC                   |
| `-a` | `--all`         | Combined output of all supported sections|

Rules:

* **MUST** be mutually exclusive
* If multiple modes are supplied → **error** (see [§7](#7-exit-codes--error-behavior))
* `-a` counts as a mode
* When no mode is specified → **default mode resolution rules** apply (see [§3.3 Defaults](#33-default-behaviors))

---

### 3.2.2 Actions (may be combined)

Actions modify *what representation* of the mode's data is output:

| Action | Long Form | Meaning                                                |
| ------ | --------- | ------------------------------------------------------ |
| `-t`   | `--toc`   | Display TOC (mode-specific representation)             |
| `-i`   | `--id`    | Display disc ID or value                               |
| `-u`   | `--url`   | Display submission/verification URL (MusicBrainz only) |
| `-o`   | `--open`  | Open the MusicBrainz URL in a browser (no output)      |

Ordering:

* These actions may appear in any order on the command line.
* When no action is supplied, **default action rules** apply.

Notes:

* `-o` triggers a browser open and produces no output itself; it may be combined with other actions that do produce output.

---

### 3.2.3 Modifiers

Modifiers affect how input is obtained or how errors are presented:

| Modifier | Long Form     | Meaning                             |
| -------- | ------------- | ----------------------------------- |
| `-c`     | `--calculate` | Use CDTOC input instead of a device |
| `-q`     | `--quiet`     | Suppress diagnostic error messages  |

Rules:

* Modifiers may be combined with Modes and Actions.
* `-q` MUST suppress human-facing diagnostic output but MUST NOT suppress exit codes.

---

### 3.2.4 Standalone Options

These options **execute immediately** and ignore all other options:

| Option | Long Form       | Meaning                       |
| ------ | --------------- | ----------------------------- |
| `-L`   | `--list-drives` | List available optical drives |
| `-h`   | `--help`        | Display help and exit         |
| `-V`   | `--version`     | Display version and exit      |

#### Version Output (`-V`)

The `-V` option MUST output:

1. Program name and version (one line)
2. Linked library versions (one line per library)

Example:

```
mbdiscid 1.0.0
libdiscid 0.6.4
```

Rules:

* Only libraries that provide CD access functionality MUST be listed
* Library lines MUST appear in alphabetical order
* No additional text (copyright, build info, etc.) MUST appear

---

### 3.2.5 Verbose Mode

The `-v` flag enables verbose output. It may be repeated for increased verbosity:

| Flag   | Level              |
| ------ | ------------------ |
| `-v`   | Verbose            |
| `-vv`  | More verbose       |
| `-vvv` | Maximum verbosity  |

Verbose output goes to **stderr** and does not affect stdout content.

Details are defined in [§9 Logging & Verbosity](#9-logging--verbosity).

---

## 3.3 Default Behaviors

Defaulting rules govern how mbdiscid behaves when the user omits options.

1. **No mode + no action → default to `-a` (All Mode).**
   Produces the complete set of sections defined in [§6.2](#62-output-sections).

2. **Mode only (e.g., `-M`) → default action is `-i`.**

3. **Action only (e.g., `-i`) → default mode is MusicBrainz (`-M`).**

4. **`-c` only → equivalent to `-Mic`.**
   CDTOC input defaults to MusicBrainz mode with ID output.

5. **Mode + `-c` → use that mode's input format** (see [§3.5](#35-cdtoc-input)).
   Default action remains `-i`.

6. **`-a` with any action:**
   All actions except `-o` are enabled by default.

7. **`-u` and `-o` are only valid in MusicBrainz mode or All mode.**

Invalid combinations result in errors defined in [§7](#7-exit-codes--error-behavior).

---

## 3.4 Invalid & Disallowed Combinations

The following combinations MUST result in an error:

### 3.4.1 Disc-required modes used with `-c`

These modes require physical disc access and MUST NOT accept CDTOC input:

* `-T` (Media Type)
* `-X` (Text)
* `-C` (MCN)
* `-I` (ISRC)
* `-R` (Raw TOC)

`-a` also MUST NOT be combined with `-c`, because the multiple modes it implies use incompatible TOC formats in CDTOC contexts.

### 3.4.2 URL/Open outside applicable modes

The following MUST error:

* `-u` with any mode except `-M` or `-a`
* `-o` with any mode except `-M` or `-a`

### 3.4.3 Multiple modes supplied

Examples that MUST error:

* `-M -A`
* `-C -I`
* `-A -a`
* `-M -X -I`

### 3.4.4 Mode options combined with `-a`

`-a` is itself a mode and MUST NOT appear with any other mode.

---

## 3.5 CDTOC Input

When supplying a CDTOC via `-c`, the expected input format depends on the active mode. The formats are defined in [§2.11 TOC Format Definitions](#211-toc-format-definitions).

| Mode               | Input Format    |
| ------------------ | --------------- |
| AccurateRip (`-A`) | AccurateRip TOC |
| FreeDB (`-F`)      | FreeDB TOC      |
| MusicBrainz (`-M`) | MusicBrainz TOC |

Notes:

* If input fails to match the format, mbdiscid MUST exit with `EX_DATAERR`.
* Example accepted invocation forms:

```
mbdiscid -c 1 12 198592 150 17477 ...
echo "1 12 198592 150 17477 ..." | mbdiscid -c
mbdiscid -Mc "1 12 198592 150 ..."
```

---

## 3.6 Quiet Mode

`-q` suppresses:

* Nonfatal diagnostics
* Human-facing error text

`-q` MUST NOT suppress:

* Exit codes
* Mode/action validation
* Output sections required by the chosen mode/action
* Errors fatal to operation (e.g., unreadable device, malformed CDTOC)

---

## 3.7 Device Argument

If `<DEVICE>` is supplied:

* It MUST be interpreted as a direct device path
* macOS: mbdiscid MUST attempt raw device fallback (see [§8.4](#84-macos-behavior))
* If the device is not readable, mbdiscid MUST return an error (see [§7](#7-exit-codes--error-behavior))

---

## 3.8 Drive Listing (`-L`)

The rules governing drive listing are defined in [§8.7](#87-device-enumeration--l).

`-L` MUST:

* Ignore all other options
* Output zero or more lines representing drives
* Exit successfully (`EX_OK`)

---

# 4. Modes & Actions

This section defines **each mode's behavior**, **its valid actions**, **required inputs**, and **constraints**. Modes are **mutually exclusive** (see [§3.2.1](#321-modes-mutually-exclusive)). Actions may be combined unless otherwise stated.

The output formats referenced in this section are fully defined in [§6 Output Formatting](#6-output-formatting).

---

## 4.1 Mode and Action Availability

The following table summarizes what each mode **supports** and **requires**.

| Mode               | Disc Required | Accepts `-c` | `-t` | `-i` | `-u` | `-o` |
| ------------------ | :-----------: | :----------: | :--: | :--: | :--: | :--: |
| Type (`-T`)        | ✓             | —            | —    | ✓    | —    | —    |
| Text (`-X`)        | ✓             | —            | —    | ✓    | —    | —    |
| MCN (`-C`)         | ✓             | —            | —    | ✓    | —    | —    |
| ISRC (`-I`)        | ✓             | —            | —    | ✓    | —    | —    |
| Raw (`-R`)         | ✓             | —            | ✓    | —    | —    | —    |
| AccurateRip (`-A`) | —             | ✓            | ✓    | ✓    | —    | —    |
| FreeDB (`-F`)      | —             | ✓            | ✓    | ✓    | —    | —    |
| MusicBrainz (`-M`) | —             | ✓            | ✓    | ✓    | ✓    | ✓    |
| All (`-a`)         | ✓             | —            | ✓    | ✓    | ✓    | —    |

Notes:

* Type, Text, MCN, ISRC modes **require subchannel or session data** → **physical disc required**.
* AccurateRip, FreeDB, MusicBrainz modes **may compute results from CDTOC**.
* Raw mode **cannot** use `-c` because Raw TOC requires physical TOC reading.
* `-i` yields "value" for Type, Text, MCN, and ISRC modes rather than a computed hash.

---

## 4.2 Media Type Mode (`-T`)

Classifies discs as Audio CD, Enhanced CD, or Mixed Mode CD (see [§2.2](#22-disc-types)).

**Inputs:**

* Requires disc
* CDTOC not accepted

**Valid actions:** `-i`

---

## 4.3 Text Mode (`-X`)

Outputs **CD-Text**, if present, including album-level fields and per-track textual metadata.

**Inputs:**

* Requires disc
* CDTOC not accepted

**Valid actions:** `-i`

**Notes:**

* Output format is defined in [§6.2.2](#622-text-section).

---

## 4.4 MCN Mode (`-C`)

Reads the **Media Catalog Number** from Q-subchannel data.

**Inputs:**

* Requires disc
* CDTOC not accepted

**Valid actions:** `-i`

---

## 4.5 ISRC Mode (`-I`)

Reads **per-track ISRCs** from Q-subchannel data.

**Inputs:**

* Requires disc
* CDTOC not accepted

**Valid actions:** `-i`

**Notes:**

* ISRC acquisition rules are fully defined in [§5 ISRC Acquisition Logic](#5-isrc-acquisition-logic).

---

## 4.6 Raw Mode (`-R`)

Outputs the *raw device TOC* in single-line textual format (see [§2.11.1](#2111-raw-toc-format)).

**Inputs:**

* Requires disc
* CDTOC not accepted

**Valid actions:** `-t`

---

## 4.7 AccurateRip Mode (`-A`)

Outputs the AccurateRip-formatted TOC and AccurateRip Disc ID.

**Inputs:**

* Accepts CDTOC input (`-c`) using the AccurateRip TOC format (see [§2.11.2](#2112-accuraterip-toc-format)).
* If `-c` is not used, the Disc TOC is read and converted.

**Valid actions:** `-t`, `-i`

**Computation notes:**

* For Enhanced and Mixed Mode discs, AccurateRip uses **audio tracks only** for checksum fields, but incorporates the FreeDB disc ID (which uses all tracks).

---

## 4.8 FreeDB Mode (`-F`)

Outputs the FreeDB/CDDB style TOC and FreeDB Disc ID.

**Inputs:**

* Accepts CDTOC input (`-c`) using the FreeDB TOC format (see [§2.11.3](#2113-freedb-toc-format)).
* If `-c` is not used, the Disc TOC is read and converted.

**Valid actions:** `-t`, `-i`

**Notes:**

* Although FreeDB is discontinued, mbdiscid preserves the ID rules for compatibility.

---

## 4.9 MusicBrainz Mode (`-M`)

Produces the MusicBrainz TOC, MusicBrainz Disc ID, and optional submission URL.

**Inputs:**

* Accepts CDTOC (`-c`) using MusicBrainz TOC format (see [§2.11.4](#2114-musicbrainz-toc-format)).
* If `-c` is not used, the Disc TOC is read and converted.

**Valid actions:** `-t`, `-i`, `-u`, `-o`

---

## 4.10 All Mode (`-a`)

All Mode produces **all supported sections** in a fixed canonical order (see [§6.1.4](#614-section-ordering)).

Where applicable, this includes:

* Type
* Text
* MCN
* ISRC
* Raw
* AccurateRip
* FreeDB
* MusicBrainz

**Inputs:**

* Requires disc
* CDTOC not accepted

**Valid actions:** `-t`, `-i`, `-u`, `-o`

---

# 5. ISRC Acquisition Logic

This section defines the **normative**, **observable**, and **platform-independent** behavior for ISRC extraction. It specifies:

* What constitutes a **valid ISRC candidate frame**
* How a **track-level ISRC** MUST be determined
* How *probe-track selection* works
* How *short tracks* are handled
* When scanning MUST be terminated early
* What results MUST and MUST NOT be output

---

## 5.1 ISRC Fundamentals

### 5.1.1 Where ISRCs Come From

ISRCs reside exclusively in the **Q-subchannel** of audio tracks. Therefore:

* ISRCs **MUST** be read from a physical disc.
* ISRCs **CANNOT** be derived from any TOC or CDTOC.
* Access requires reading and validating Q-subchannel frames via MMC/SCSI.

---

### 5.1.2 Per-Track Nature

Each *audio* track can have:

* Zero ISRC
* One ISRC (correct case)
* One ISRC but with corrupted frames
* Multiple conflicting values (real-world mastering errors)

Data tracks **MUST NOT** be checked for ISRC.

---

### 5.1.3 Valid ISRC Candidate Frames

A frame-level ISRC candidate is **valid** if and only if:

1. **The frame passes CRC validation.**
2. **The ISRC field contains at least one non-`'0'` character.**
3. **The ISRC conforms to the IFPI character-set and length constraints:**
   * Country code: 2 uppercase A–Z
   * Registrant code: 3 alphanumeric A–Z, 0–9
   * Year: 2 digits
   * Designation code: 5 digits
   * Total length: **12 characters**

A value satisfying these conditions is a **valid candidate ISRC**.

Invalid frames (CRC failure, all zeros, malformed content) MUST NOT appear in output and MUST NOT influence majority calculations.

---

## 5.2 Disc-Level Probe Strategy

The goal of the disc-level probe is:

* To detect whether **any ISRC exists at all**,
* While avoiding unnecessary scans of all tracks (major speed improvement),
* Without reducing accuracy for common real-world cases.

Probe selection MUST follow the logic below.

---

### 5.2.1 Track Count Determination

Let **n** = number of audio tracks on the disc.

* For **n < 5**, the disc MUST be treated as **short**, and **all audio tracks MUST be scanned** (see below).
* For **n ≥ 5**, the probe-track algorithm defined in [§5.2.2](#522-probe-tracks-for-n--5) MUST be used.

---

### 5.2.2 Probe Tracks for n ≥ 5

Three probe tracks MUST be selected from the eligible audio tracks.

**(A) Exclude Short Tracks From Eligibility**

Before selecting probe tracks:

* Any track meeting the definition of a **short track** (see [§5.2.3](#523-short-tracks)) MUST be removed from the probe-candidate pool.

If removal causes fewer than 3 tracks to remain, then:

* The disc MUST be treated as a **short disc**,
* And all audio tracks MUST be scanned.

**(B) Compute Probe Positions**

Probe indices MUST be selected such that:

* They are distinct
* They fall approximately at **33%**, **50%**, and **67%** of the eligible track list
* They do **not** select track 1 or the last track unless unavoidable (tracks are least reliable at boundaries)

**(C) Evaluating Probe Tracks**

Probe tracks are scanned using the full track-level scanning procedure defined in [§5.3](#53-track-level-scanning-procedure). This means probe tracks will have ISRC results (or indeterminate status) after probing.

If **any** probe track yields a **valid ISRC** (per [§5.1.3](#513-valid-isrc-candidate-frames)):

* The disc MUST be treated as **containing ISRCs**.
* All remaining audio tracks MUST be scanned in increasing numerical order.
* Previously scanned probe tracks MUST NOT be rescanned; their results are retained.

If **none** of the probe tracks yields a valid ISRC:

* The disc MUST be treated as **ISRC-absent**.
* Scanning MUST terminate immediately.
* No track-level ISRC work occurs beyond the probes.

---

### 5.2.3 Short Tracks

A track MUST be considered a **short track** if:

* Its duration is insufficient to support:
  * Two bookend exclusion zones (start/end avoidance),
  * The minimum seek distance per tranche,
  * And the required number of tranches.

This generally applies to:

* "Hidden" tracks in index 0 areas
* Very short interstitial tracks
* Cue manipulation artifacts

**Behavior for short tracks:**

* Short tracks MUST NOT be used as probe candidates.
* When scanning short tracks:
  * The entire track MAY be scanned from start to finish.
  * All valid candidate frames MUST be aggregated.
  * Normal ISRC determination rules in [§5.4](#54-per-track-isrc-determination) apply.

---

## 5.3 Track-Level Scanning Procedure

Track-level scanning obtains **candidate ISRC frames** from arbitrary positions within a track.

The following describes **normative observable behavior**, not implementation internals.

---

### 5.3.1 Tranches

A track MUST be scanned in **up to four tranches**, unless:

* It is a short track (see §5.2.3), or
* A valid track ISRC has already been determined, or
* Disc-level logic terminates scanning after probe tracks.

Each tranche:

* Seeks into the track by a minimum offset (implementation-defined but MUST avoid the first seconds of the track).
* SHOULD read **32 Q-subchannel frames** for analysis.
* MUST exclude invalid frames (CRC failure, all zeros, malformed).
* MUST aggregate valid frames with prior tranches.

After each tranche:

* If ≥ 64 valid ISRC candidate frames have been collected, scanning MAY stop early.

---

### 5.3.2 Seek + Settle Behavior

To reduce corrupted initial reads:

* After each seek, the drive MUST be allowed a **settle period** sufficient to avoid common read instability.
* The exact mechanism is **implementation-specific** and SHOULD NOT be exposed to the user except in verbose/diagnostic mode.
* Observable behavior:
  * Fewer false negatives
  * Fewer malformed frame bursts
  * More consistent ISRC presence detection

---

## 5.4 Per-Track ISRC Determination

Once all tranches for a track have been performed (or early termination applies), mbdiscid MUST evaluate the aggregated valid ISRC candidate frames.

Let:

* **v_max** = candidate value with the highest frame count
* **count(v)** = number of times value **v** appears
* **count(other)** = sum of counts of all other distinct values

---

### 5.4.1 Strong Majority Rule

A track-level ISRC MUST be accepted when both conditions hold:

1. **count(v_max) ≥ 2 × max(count(other value))**
   (Strict 2:1 dominance over every other distinct value)

2. **v_max** satisfies all validity rules in [§5.1.3](#513-valid-isrc-candidate-frames)

In this case:

* The track's ISRC = **v_max**
* All other candidate values MUST be ignored

---

### 5.4.2 Rescue Sampling

If the strong majority rule does **not** hold after the initial tranches, mbdiscid MUST attempt rescue sampling:

* Sample **up to 2 additional tranches** (50% more than the baseline of 4).
* Re-evaluate the strong majority rule after each additional tranche.
* If a strong majority is achieved, accept the ISRC.

---

### 5.4.3 Indeterminate Case

If the strong majority rule does **not** hold after rescue sampling, the track-level ISRC MUST be treated as:

> **Indeterminate (low confidence)**

Meaning:

* No ISRC value MUST be output for that track
* The track MUST be omitted entirely from the ISRC output section (see [§6.2.4](#624-isrc-section))

Common reasons:

* Two values appear with insufficient margin
* Mastering errors produce multiple conflicting values
* Not enough candidate frames were recovered

---

## 5.5 Output Eligibility Rules

These rules coordinate with [§6 Output Formatting](#6-output-formatting).

For each audio track:

* A track MUST appear in the ISRC output only if:
  * It produced a valid ISRC candidate majority per [§5.4.1](#541-strong-majority-rule)

* A track MUST NOT appear in the ISRC output if:
  * It is indeterminate per [§5.4.3](#543-indeterminate-case)
  * It produced no valid candidates
  * It produced only invalid or all-zero candidates

Tracks meeting output criteria MUST appear in increasing track-number order.

No other information (raw frames, counts, conflicts, etc.) may appear unless verbose mode is enabled.

---

# 6. Output Formatting

This section defines all **normative output rules** for mbdiscid across all modes. It specifies:

* When sections MUST appear
* How they MUST be formatted
* When they MUST be omitted
* How All Mode output is ordered

All output formats MUST be consistent and machine-parseable.

---

## 6.1 General Output Rules

### 6.1.1 No output for missing/absent optional data

If a mode attempts to read optional metadata (Text, MCN, ISRC) and finds none:

* The relevant **section MUST NOT appear** in All Mode.
* A single-mode run (e.g., `-I`) MUST output **nothing** (empty output is allowed).
* Absence MUST NOT be treated as an error (see [§7 Exit Codes & Error Behavior](#7-exit-codes--error-behavior)).

This applies **per section**, not globally.

---

### 6.1.2 Required structured data

The following MUST be present when applicable:

* **Raw, AccurateRip, FreeDB, MusicBrainz TOCs**
* **AccurateRip, FreeDB, MusicBrainz disc IDs**
* **Media Type** (always determinable from disc TOC)

Failure to compute a required identifier MUST result in an error (see [§7](#7-exit-codes--error-behavior)).

---

### 6.1.3 Output ordering within a section

When multiple actions are specified, output MUST appear in canonical order regardless of flag order on the command line:

1. TOC (if `-t`)
2. ID (if `-i`)
3. URL (if `-u`)

This ordering applies in all modes, including All Mode.

---

### 6.1.4 Section ordering

When `-a` is active, sections MUST appear, if present, in the following canonical order:

1. **Media** (Media Type)
2. **Text** (CD-Text)
3. **MCN**
4. **ISRC**
5. **Raw** (Raw TOC)
6. **AccurateRip**
7. **FreeDB**
8. **MusicBrainz**

If a section has no data (e.g., no MCN), it MUST be omitted; the relative ordering of any present sections MUST still follow the sequence above.

---

### 6.1.5 Section header format (All Mode only)

Every section printed in **All Mode (`-a`)** MUST follow the header form:

```
----- <Section Name> -----
```

Examples:

```
----- Media -----
----- Text -----
----- MCN -----
----- ISRC -----
----- Raw -----
----- AccurateRip -----
----- FreeDB -----
----- MusicBrainz -----
```

In **single-mode** runs (e.g., `-M`, `-I`, `-T`, `-X`), **headers MUST NOT be printed**.

---

### 6.1.6 Section body layout

Each section consists of:

```
----- <Section Name> -----
<one or more data lines>
```

Rules:

* **Exactly one blank line between sections.**
* **No blank line after the last section.**
* Within a section, **no blank lines** unless explicitly defined by that section's format (Text is the only exception).

---

### 6.1.7 Single-mode output rules

When a single mode is active:

* Only the representations selected via actions (`-i`, `-t`, `-u`) MUST be printed.
* Section headers MUST NOT appear.
* If no valid data exists (e.g., no ISRC), **output MUST be empty**, not a placeholder.

---

### 6.1.8 Compact output rules

In any mode other than `-a`:

* Output MUST be one or more lines of plain text.
* No headers.
* No blank-line separation.
* No additional labeling.
* No non-selected representations.

Examples:

```
mbdiscid -M       → prints MB disc ID only
mbdiscid -Mt      → prints MB TOC only
mbdiscid -Mi      → prints MB ID only
mbdiscid -Mtiu    → prints MB TOC, MB ID, MB URL (3 lines)
```

---

## 6.2 Output Sections

This subsection defines each section's required output format.

The **subsection order matches the All Mode section order** defined in [§6.1.4](#614-section-ordering).

---

### 6.2.1 Media Section

Header (All Mode only):

```
----- Media -----
```

Body includes:

1. **Primary disc type** (one line):
   * `Audio CD`
   * `Enhanced CD`
   * `Mixed Mode CD`

2. **Technical disc type** (one line):
   * `CD-DA`
   * `CD-Extra`
   * `Mixed Mode`

3. **Total tracks** (one line):
   * `<N> tracks`

4. **Track count summary** (one line) — **only for Enhanced and Mixed Mode discs**:
   * `<N> audio tracks, <M> data track(s)`

Rules:

* Audio CDs MUST NOT print a track breakdown line.
* Enhanced and Mixed Mode discs MUST print a single-line breakdown.

Example:

```
----- Media -----
Enhanced CD
CD-Extra
12 tracks
11 audio tracks, 1 data track
```

---

### 6.2.2 Text Section (CD-Text)

Header (All Mode only):

```
----- Text -----
```

Text output MUST use the **canonical, ALLCAPS, no-space tag names** defined below.

#### CD-Text Tag Names

**Album-scope tags:**

| Tag Name      | Red Book Source            | Meaning               |
| ------------- | -------------------------- | --------------------- |
| `ALBUM`       | `TITLE` (album-level)      | Album title           |
| `ALBUMARTIST` | `PERFORMER` (album-level)  | Primary album artist  |
| `LYRICIST`    | `SONGWRITER` (album-level) | Album lyricist        |
| `COMPOSER`    | `COMPOSER` (album-level)   | Album composer        |
| `ARRANGER`    | `ARRANGER` (album-level)   | Album arranger        |
| `GENRE`       | `GENRE`                    | Genre text field      |
| `COMMENT`     | `MESSAGE` (album-level)    | General album comment |

**Track-scope tags:**

| Tag Name    | Red Book Source           | Meaning        |
| ----------- | ------------------------- | -------------- |
| `TITLE`     | `TITLE` (track-level)     | Track title    |
| `ARTIST`    | `PERFORMER` (track-level) | Track artist   |
| `LYRICIST`  | `SONGWRITER` (track-level)| Track lyricist |
| `COMPOSER`  | `COMPOSER` (track-level)  | Track composer |
| `ARRANGER`  | `ARRANGER` (track-level)  | Track arranger |
| `COMMENT`   | `MESSAGE` (track-level)   | Track comment  |

Note: LYRICIST, COMPOSER, ARRANGER, and COMMENT exist at both album and track scope per Red Book. GENRE is album-scope only. Scope is determined by the track index in the CD-Text data (track 0 = album, track 1+ = track).

#### Album-scope fields

If any album-scope fields are present, they MUST appear first, one per line, in the following order (skipping absent fields):

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

For each track that has at least one CD-Text field, mbdiscid MUST output:

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

* **No indentation** before any of the lines (track number or tags).
* Tracks MUST appear in ascending track-number order.
* Within each track block, tag lines MUST appear in the order above, skipping absent tags.
* If at least one track block exists and at least one album-scope field was printed, there MUST be **exactly one blank line** between the last album-scope line and the first `<track>:` line.
* There MUST be **exactly one blank line** between track blocks.

#### CD-Text Normalization Rules

mbdiscid MUST normalize CD-Text field contents as follows:

1. Strip trailing null padding.
2. Strip `\r` or mixed CRLF artifacts.
3. Trim leading and trailing whitespace.
4. Convert control characters (ASCII < 0x20) to spaces except `\n`.
5. Preserve the original encoding **as UTF-8** after conversion from CD-Text's encoding (typically ISO-8859-1).
6. The tag name MUST appear exactly as defined (e.g., `ALBUMARTIST`, not `AlbumArtist`).
7. Values MUST NOT be quoted.

#### Omission rules

If no CD-Text is present at all:

* The Text section MUST be omitted in All Mode.
* `-X` alone MUST produce **empty output**.

Example:

```
----- Text -----
ALBUM: Dark Side of the Moon
ALBUMARTIST: Pink Floyd
GENRE: Rock

1:
TITLE: Speak to Me
ARTIST: Pink Floyd

2:
TITLE: Breathe
ARTIST: Pink Floyd
COMPOSER: Roger Waters
```

---

### 6.2.3 MCN Section

Header (All Mode only):

```
----- MCN -----
```

Body:

* A single line containing the MCN value, as a numeric string.

Example:

```
----- MCN -----
0602517484016
```

Rules:

* MUST NOT appear if MCN is absent or invalid.
* MCN MUST be validated as exactly 13 digits (characters 0-9 only).
* All-zero MCNs MUST be treated as absent and MUST NOT be printed.

In MCN-only mode (`-C` without `-a`):

* Only the bare MCN value MUST be printed (no header).

---

### 6.2.4 ISRC Section

Header (All Mode only):

```
----- ISRC -----
```

Each track that passed ISRC determination (see [§5.4](#54-per-track-isrc-determination)) appears as:

```
<track number>: <ISRC>
```

Example:

```
----- ISRC -----
1: USUM70747896
2: USUM70747897
5: USUM70747900
```

Rules:

* Only tracks with **accepted** ISRCs may appear.
* Tracks with indeterminate or absent ISRC MUST NOT appear.
* Track numbers MUST appear in ascending order.
* All-zero ISRCs MUST never be printed.

In ISRC-only mode (`-I` without `-a`):

* Only the `<track>: <ISRC>` lines MUST be printed (no header).

---

### 6.2.5 Raw Section

Header (All Mode only):

```
----- Raw -----
```

Body:

* A single line containing the raw TOC representation (see [§2.11.1](#2111-raw-toc-format)).

Example:

```
----- Raw -----
1 12 150 17477 32562 ... 198592
```

In Raw-only mode (`-R` without `-a`):

* Only the selected representation(s) MUST be printed, with no header.

---

### 6.2.6 AccurateRip Section

Header (All Mode only):

```
----- AccurateRip -----
```

Body (two lines):

1. AccurateRip-format TOC (see [§2.11.2](#2112-accuraterip-toc-format))
2. AccurateRip Disc ID

Example:

```
----- AccurateRip -----
12 12 1 0 17327 32412 ... 198442
014-00209635-01652576-e211510f
```

In AccurateRip-only mode (`-A` without `-a`):

* Only the selected representation(s) MUST be printed, with no header.

---

### 6.2.7 FreeDB Section

Header (All Mode only):

```
----- FreeDB -----
```

Body (two lines):

1. FreeDB-format TOC (see [§2.11.3](#2113-freedb-toc-format))
2. FreeDB Disc ID

Example:

```
----- FreeDB -----
12 150 17477 32562 ... 2648
e211510f
```

In FreeDB-only mode (`-F` without `-a`):

* Only the selected representation(s) MUST be printed, with no header.

---

### 6.2.8 MusicBrainz Section

Header (All Mode only):

```
----- MusicBrainz -----
```

Body:

1. MusicBrainz TOC (one line, see [§2.11.4](#2114-musicbrainz-toc-format))
2. MusicBrainz Disc ID (one line)
3. MusicBrainz URL (one line)

Example:

```
----- MusicBrainz -----
1 12 198592 150 17477 32562 ...
hO3GT18x_9qBZL3vZhhpDexHnv8-
https://musicbrainz.org/cdtoc/hO3GT18x_9qBZL3vZhhpDexHnv8-
```

Rules:

* URLs MUST use `https://`.

In MusicBrainz-only mode (`-M` without `-a`):

* Only the selected representation(s) MUST be printed, with no header.

---

## 6.3 Empty Output Cases

In any mode **other than All Mode**:

* If the chosen mode/action produces no valid data for **optional** metadata (Text, MCN, ISRC), **output MUST be empty**, and the exit code MUST be `EX_OK`.
* If the chosen mode/action produces no valid data for **required** metadata (Type, Raw, AccurateRip, FreeDB, MusicBrainz), this is an error condition (see [§7](#7-exit-codes--error-behavior)).

Examples of empty output (EX_OK):

* `mbdiscid -X` on a disc with no CD-Text
* `mbdiscid -C` on a disc with no MCN
* `mbdiscid -I` on a disc with no ISRCs

---

# 7. Exit Codes & Error Behavior

mbdiscid MUST use the standardized exit codes defined in **sysexits.h**. This section specifies **which conditions map to which exit codes**, what constitutes an error, and how error messages MUST be formatted.

mbdiscid MUST:

* Print all error messages to **stderr**
* Format errors consistently as **single-line messages**
* Prefix errors with `mbdiscid: `

---

## 7.1 Exit Codes

The following exit codes are used:

| Code | Name           | Description                                    |
| ---- | -------------- | ---------------------------------------------- |
| 0    | `EX_OK`        | Successful termination                         |
| 64   | `EX_USAGE`     | Command line usage error                       |
| 65   | `EX_DATAERR`   | Data format error                              |
| 69   | `EX_UNAVAILABLE` | Service unavailable                          |
| 70   | `EX_SOFTWARE`  | Internal software error                        |
| 74   | `EX_IOERR`     | Input/output error                             |

---

## 7.2 Non-Error Conditions (Exit = EX_OK)

The following conditions MUST NOT be treated as errors:

1. **Optional data absent**, including:
   * No CD-Text
   * No MCN
   * No ISRCs
   * No applicable text for a specific tag

2. Single-mode requests for optional data returning "nothing to report". Examples:
   * `mbdiscid -X` on a disc with no CD-Text → empty stdout, exit EX_OK
   * `mbdiscid -C` on a disc with no MCN → empty stdout, exit EX_OK
   * `mbdiscid -I` on a disc with no ISRCs → empty stdout, exit EX_OK

3. Partial metadata scenarios (e.g., ISRCs present on some but not all tracks).

4. URL open request (`-o`) failing because of OS/browser issues—this SHOULD emit a warning but MUST NOT cause a non-zero exit.

---

## 7.3 Error Conditions (Non-Zero Exit Codes)

These conditions MUST produce a **non-zero exit**, with the exit code chosen from sysexits.h as defined below.

### 7.3.1 Invalid Command-Line Usage → EX_USAGE

Examples:

* Mutually exclusive modes specified together
* `-a` combined with any other mode flag
* `-c` used with a mode requiring a physical disc (`-T`, `-X`, `-C`, `-I`, `-R`)
* `-u` or `-o` used outside MusicBrainz or All mode
* Wrong number of arguments
* Unknown flags

---

### 7.3.2 Unreadable Device / I/O Failure → EX_IOERR

This error applies when:

* The CD device cannot be opened
* The drive returns a SCSI/IO error
* The TOC cannot be read from the disc
* Subchannel data cannot be retrieved (for Text, MCN, or ISRC modes)

---

### 7.3.3 Malformed CDTOC Input (Calculate Mode) → EX_DATAERR

This applies to:

* Too few or too many numeric fields
* Incorrect format for the selected mode (AccurateRip, FreeDB, or MusicBrainz)
* Non-numeric or negative values
* Leadout or offsets impossible or inconsistent
* Offsets not in ascending order

Notes:

* This applies *only* to `-c` input.
* Missing fields are also considered malformed.
* A malformed input MUST NOT fall back to disc reading.

---

### 7.3.4 Required Metadata Unavailable → EX_DATAERR

Certain identifiers are mandatory when their associated mode is invoked:

| Requested Mode   | Required Data                | Failure Condition → EX_DATAERR   |
| ---------------- | ---------------------------- | -------------------------------- |
| `-T` Type        | Disc type classification     | Cannot read TOC or classify      |
| `-R` Raw         | Raw TOC                      | Cannot read TOC                  |
| `-A` AccurateRip | AR TOC, AR Disc ID           | Cannot read TOC or compute ID    |
| `-F` FreeDB      | FreeDB TOC, FreeDB Disc ID   | Cannot read TOC or compute ID    |
| `-M` MusicBrainz | MB TOC, MB Disc ID           | Cannot read TOC or compute ID    |

---

### 7.3.5 Invalid Media for Requested Operation → EX_UNAVAILABLE

Examples:

* Attempt to read Text from CDTOC input
* Attempt to read MCN from CDTOC input
* Attempt to read ISRC from CDTOC input
* Attempt to open a device that is not a CD drive

---

## 7.4 Error Message Formatting

All error messages MUST adhere to the following rules:

1. **Single line**, human-readable
2. Printed to **stderr**
3. Prefixed with `mbdiscid: `
4. No trailing periods
5. No extra whitespace or blank lines before/after
6. MUST NOT output stack traces, debug logs, or implementation details

Example:

```
mbdiscid: cannot open device /dev/sr0
```

---

## 7.5 SIGINT / Early Termination

If the user interrupts execution (CTRL-C):

* mbdiscid MUST terminate promptly
* Exit code MUST be **EX_SOFTWARE**
* No partial output SHOULD be printed

---

# 8. Device Handling & Platform Behavior

This section defines all normative behavior for mbdiscid when interacting with CD/DVD hardware devices across platforms. It covers device resolution, fallback rules, permissions expectations, and platform-specific differences.

---

## 8.1 Supported Platforms

mbdiscid MUST support:

* **Linux**
* **macOS**

No other platforms are required.

---

## 8.2 Device Argument Requirements

When invoked with a `<DEVICE>` argument:

* The value MUST be treated as a **literal path** to a device node.
* mbdiscid MUST NOT guess, alias, or auto-search for alternative names (except where explicitly specified for macOS in [§8.4](#84-macos-behavior)).
* The user is responsible for supplying the correct device path.

---

## 8.3 Device Validation

When opening a device:

1. mbdiscid MUST attempt to open the device with the minimal required permissions.
2. If the device cannot be opened, mbdiscid MUST return **EX_IOERR**.
3. If the device cannot be probed as a CD-capable device, mbdiscid MUST return **EX_UNAVAILABLE**.
4. No operation MUST ever require elevated privileges such as `sudo`.

---

## 8.4 macOS Behavior

### 8.4.1 Raw vs block devices

macOS provides both:

* Block devices: `/dev/diskN`
* Raw/character devices: `/dev/rdiskN`

Subchannel access (required for Text, MCN, ISRC) typically succeeds **only** on raw devices.

### 8.4.2 Fallback rule

If the user supplies a block device:

1. mbdiscid MUST attempt to open it normally.
2. If block-device access fails for TOC or subchannel operations, mbdiscid MUST attempt the corresponding raw device automatically by converting:

```
/dev/diskN  →  /dev/rdiskN
```

3. If both attempts fail, mbdiscid MUST return **EX_IOERR**.

This fallback MUST occur silently and MUST NOT require user interaction.

### 8.4.3 No additional probing

mbdiscid MUST NOT attempt any other device name transformations.

---

## 8.5 Linux Behavior

### 8.5.1 Supported device naming

Linux supports several canonical CD device patterns:

* `/dev/srN` (primary)
* `/dev/scdN` (legacy)
* `/dev/cdrom` (symlink)

mbdiscid MUST accept any of these as valid device paths.

### 8.5.2 No raw/block pairing logic

Unlike macOS, Linux does not require distinct raw/block device handling. mbdiscid MUST NOT attempt to derive alternative names.

---

## 8.6 Device Capability Requirements

### 8.6.1 Required for standard TOC reading

All modes except `-c` require access to the disc TOC. Failure to read the TOC MUST produce **EX_IOERR**.

### 8.6.2 Required for Type, Text, MCN, ISRC

These modes require **subchannel access** and MUST therefore require:

* A physical disc
* A drive that supports reading Q-subchannel frames

If subchannel data cannot be read:

* The mode MUST NOT succeed
* mbdiscid MUST return **EX_IOERR**

### 8.6.3 Data tracks

Data tracks MUST NOT be queried for Text, MCN, or ISRC. They remain valid for TOC and disc ID calculations.

---

## 8.7 Device Enumeration (`-L`)

The `-L` option lists optical drives available on the system.

### 8.7.1 General requirements

* Output MUST go to **stdout**.
* Output MUST be **one line per drive**.
* No sorting is required.
* Format **may differ by platform**; mbdiscid MUST NOT post-process platform tool output.
* If no drives are present, output MUST be empty with exit code **EX_OK**.
* Errors (e.g., the underlying system command fails) MUST result in **EX_IOERR**.

mbdiscid MUST NOT require elevated privileges to list drives.

### 8.7.2 Linux enumeration

On Linux, mbdiscid MUST invoke:

```
lsblk -dp -I 11 -o NAME,VENDOR,MODEL,REV
```

mbdiscid MUST print the output **exactly as produced**, with no modification or filtering.

### 8.7.3 macOS enumeration

On macOS, mbdiscid MUST invoke:

```
drutil status
```

mbdiscid MUST print the output **exactly as produced**, with no modification.

### 8.7.4 No cross-platform harmonization

mbdiscid MUST NOT attempt to normalize or combine the behaviors of `drutil` and `lsblk`. Their output formats are inherently different and SHOULD remain so.

---

## 8.8 Platform-Specific Notes

### 8.8.1 Device formats

Users SHOULD supply:

* `/dev/srN` (Linux)
* `/dev/rdiskN` (macOS)

But mbdiscid MUST accept any path the OS will allow to open.

### 8.8.2 No privilege escalation

mbdiscid MUST NOT attempt:

* `sudo`
* Privilege elevation APIs
* Modifying device permissions

If permissions are insufficient, mbdiscid MUST exit with **EX_IOERR**.

---

# 9. Logging & Verbosity

This section defines the behavior of mbdiscid's verbosity system, which controls **diagnostic output to stderr**. Verbose output is intended for debugging, inspection, and development; it MUST NOT affect normal operation or the structure of stdout.

mbdiscid uses the `-v` flag, repeated as needed, to increase verbosity.

---

## 9.1 General Rules for Verbose Output

1. **Verbose output MUST go to stderr only.** It MUST NEVER appear on stdout, which is reserved exclusively for machine-readable data.

2. **Verbose output MUST NOT change program behavior.** Execution, scanning strategy, ISRC logic, TOC parsing, and exit codes MUST be identical with or without verbosity.

3. **Verbose output MUST NOT leak internal implementation details** (e.g., memory addresses, struct layouts, raw SCSI buffers).

4. **Verbose output MUST remain human-readable**, short, and structured.

5. **All verbose messages MUST be line-oriented** and MUST NOT contain blank lines unless explicitly allowed.

6. **All verbosity levels MUST preserve stderr/stdout separation** to guarantee pipeline safety.

---

## 9.2 Version Banner

At **any verbosity level (`-v` or higher)**, mbdiscid MUST begin stderr output with the same information as `-V` (program version and linked library versions).

---

## 9.3 Verbosity Levels

Verbosity is cumulative:

```
-v     → Verbose
-vv    → More verbose
-vvv   → Maximum verbosity
```

---

## 9.4 Level 1 Verbosity (`-v`)

Level 1 MUST print **high-level operational status** messages, including:

* Device being opened
* macOS raw-device fallback attempts
* Confirmation of TOC acquisition or CDTOC parsing
* Active mode and actions
* Absence of optional metadata (Type, Text, MCN, ISRC)
* Early ISRC disc-level exit (probe tracks show no ISRCs)
* Non-fatal warnings

---

## 9.5 Level 2 Verbosity (`-vv`)

Level 2 provides **mid-level diagnostics**, including:

* ISRC scanning progress per track
* Frame rejection reasons per tranche
* Short-track classification
* Probe-track selection and results
* CD-Text presence/absence details

---

## 9.6 Level 3 Verbosity (`-vvv`)

Level 3 is **maximum diagnostic verbosity**, including:

* ISRC frame value summaries (not raw bytes)
* Voting and decision explanations for ISRC determination
* Track skipping explanations
* CD-Text block details (summaries, not raw bytes)
* Device fallback detail (macOS)

---

## 9.7 Disallowed Verbose Output

mbdiscid MUST NOT print:

* Raw binary subchannel frames
* Internal memory/struct dumps
* SCSI hex dumps
* Debug traces
* Timing statistics
* Sector-level offsets beyond what is relevant to ISRC selection
* Multi-line or noisy logs disruptive to stderr consumers

---

## 9.8 Verbosity & Exit Codes

* Verbosity MUST NOT change exit codes.
* Errors MUST continue to emit single-line stderr messages, even when verbose mode is active.
* Verbose messages MUST NOT appear *after* an error message.

---

## 9.9 Interaction With Other Options

* `-v` MUST be allowed with all modes (including `-a`, `-c`, and `-L`).
* Verbose output MUST NEVER change the content or ordering of stdout.

---

# 10. Security & Safety Considerations

mbdiscid is a **read-only**, non-destructive utility. It MUST conform to the following minimal safety requirements:

## 10.1 Device Safety

1. **Read-only operation** — mbdiscid MUST perform only read-only SCSI/MMC operations. It MUST NOT write to, modify, or otherwise alter a disc or device under any circumstances.

2. **No exclusive locks** — mbdiscid MUST NOT request or require exclusive access to the drive.

3. **No elevated privileges required** — mbdiscid MUST NOT require root, sudo, privilege escalation, or special capabilities. If the OS denies access to the device, mbdiscid MUST fail with an appropriate error.

---

## 10.2 Input Safety

4. **Strict validation of CDTOC input** — When using `-c`, mbdiscid MUST validate CDTOC input before use. Malformed or inconsistent input MUST cause an error rather than undefined behavior.

5. **Disc-based operations MUST require a physical disc** — Subchannel-based modes (Type, Text, MCN, ISRC) MUST NOT be attempted on CDTOC input.

---

## 10.3 Execution Safety

6. **Separation of stdout and stderr** — Diagnostic or verbose output MUST NOT appear on stdout under any circumstances.

7. **No shell execution from user input** — mbdiscid MUST NOT invoke shell interpreters or construct command strings containing user-controlled input.

8. **Browser launch safety (`-o`)** — The `-o` option MUST open the URL using standard OS mechanisms without shell interpretation or injection risk. Failure to open a browser MUST NOT be treated as a fatal error.

---

## 10.4 Network & Privacy

9. **No unsolicited network activity** — mbdiscid MUST NOT perform network requests. MusicBrainz URLs MUST be computed locally; only `-o` MAY trigger a browser to open a URL.

10. **No persistent state** — mbdiscid MUST NOT create configuration files, caches, or logs, and MUST NOT store user data.

---

# 11. Portability & Platform Requirements

This section defines the **minimum portability expectations** for mbdiscid.

## 11.1 Supported Operating Systems

mbdiscid MUST support:

* **Linux**
* **macOS**

No other platforms are required by this specification.

## 11.2 Functional Equivalence

On all supported platforms:

1. Core functionality (TOC reading, disc ID calculation, Type, Text, MCN, ISRC) MUST behave consistently.
2. Output formats, error semantics, and exit codes MUST be identical.
3. Differences in underlying OS tooling (e.g., `lsblk`, `drutil`) MUST NOT affect stdout format for any mode except device enumeration (`-L`), which is permitted to vary by platform.

## 11.3 Required External Dependencies

As a functional requirement:

* mbdiscid **requires a disc-reading library capable of TOC and subchannel access**, such as **libdiscid**.
* The exact build mechanism or dependency management system is **not prescribed** by this specification.

The only normative requirements are:

1. The dependency MUST support Linux and macOS.
2. The dependency MUST permit extraction of all metadata required by this specification.
3. The dependency MUST enable read-only operation.

## 11.4 Architecture Support

mbdiscid MUST run on:

* x86_64
* arm64 (Apple Silicon)

No additional architectures are guaranteed.

## 11.5 Environment Assumptions

mbdiscid MUST NOT require:

* Elevated privileges
* Kernel modules
* System configuration changes
* Custom device drivers
* Persistent state or configuration files

---

# 12. Document Metadata

**Title:** *mbdiscid Product Specification*<br>
**Version:** 1.1<br>
**Date:** 2025-06-08<br>
**Author:** Ian McNish<br>
**License:** Creative Commons Attribution 4.0 International (CC BY 4.0)
