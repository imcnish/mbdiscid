# mbdiscid Message Style Guide

This document defines conventions for mbdiscid warning, error, and diagnostic messages, and usage text.

Standard output (disc IDs, TOCs, metadata) is defined in the Product Specification and is not covered here.


## General Principles

1. **Lowercase** — Messages must begin with lowercase (the prefix or first word)
2. **Terse** — Messages must state only what's wrong or what's happening; no tutorials
3. **Consistent** — Similar situations must use identical phrasing patterns
4. **Prefixed** — All messages must begin with their subsystem prefix
5. **Concise** — Messages should be concise
6. **Single line** — Messages should be single line


## Subsystem Prefixes

| Prefix    | Source         | Usage                            |
|-----------|----------------|----------------------------------|
| `cli`     | cli.c, main.c  | Argument parsing and validation  |
| `toc`     | toc.c, main.c  | TOC parsing and format detection |
| `discid`  | discid.c       | Disc ID calculation              |
| `device`  | device.c       | Device access and reading        |
| `cdtext`  | cdtext.c       | CD-Text parsing                  |
| `isrc`    | isrc.c         | ISRC acquisition                 |
| `mcn`     | device.c       | MCN reading                      |
| `scsi`    | scsi_*.c       | Low-level SCSI operations        |


## Error Messages

Errors go to stderr via `error()` or `error_quiet()`.

Format: `prefix: message`

Errors should use descriptions from `sysexits.h` and `errno.h` where applicable. For situations without a matching standard description, errors should use similar language.


### Argument Errors (cli)

Patterns use printf-style `%s` placeholders for option names.

**Mutually exclusive options:**

    cli: %s and %s are mutually exclusive

**Option requires another option:**

    cli: %s requires %s

**Option requires non-option input:**

    cli: %s requires <what>

**Examples:**

    cli: -M and -F are mutually exclusive
    cli: -c and -R are mutually exclusive
    cli: --assume-audio requires -Ac
    cli: -c requires TOC data
    cli: -T requires a disc
    cli: too many arguments


### TOC Errors (toc)

**Format/parsing errors:**

    toc: <what is wrong>

**Examples:**

    toc: non-numeric value
    toc: too few values
    toc: value cannot be negative
    toc: value exceeds CD capacity
    toc: format not recognized
    toc: format is ambiguous
    toc: track count out of range
    toc: offsets not in ascending order
    toc: leadout before last track


### Disc ID Errors (discid)

    discid: cannot calculate <type> ID

**Examples:**

    discid: cannot calculate MusicBrainz ID
    discid: cannot calculate FreeDB ID
    discid: cannot calculate AccurateRip ID


### Device Errors (device)

    device: cannot <action>: <reason>

**Examples:**

    device: cannot read disc: no medium
    device: cannot open: permission denied


### Mode-specific Errors

    <mode>: <what is wrong>

**Example:**

    accuraterip: raw TOC not supported


## Diagnostic Messages

Diagnostic messages go to stderr via `verbose()`. They must use the same prefixes as error messages.

Verbosity levels are defined in the Product Specification §9.


### Format

    prefix: <description>
    prefix: <noun>: <details>

**Examples:**

    device: opening /dev/rdisk2
    toc: detected format: raw
    toc: 12 tracks
    isrc: starting scan
    isrc: scan complete, 10 found
    toc: track 1: offset 0, length 15000, audio
    isrc: track 3: candidates: USRC17607839×5, GBAYE0200012×2


## Help and Usage

Help text (via `cli_print_help`) uses different conventions:

- Section headers must use title case
- Option descriptions should be sentence fragments
- Help text must not include a subsystem prefix (this is documentation, not runtime messages)

**Example:**

    Usage: mbdiscid [options] <DEVICE>
           mbdiscid [options] -c <TOC>

    Mode options (mutually exclusive):
      -M, --musicbrainz   MusicBrainz ID and TOC
      -F, --freedb        FreeDB/CDDB ID and TOC


## Summary

**Messages must:**

- State the problem only
- Use consistent structure for similar errors
- Include the subsystem prefix
- Use lowercase
- Use active voice (`cannot open` not `could not be opened`)
- Use present tense (`requires` not `required`)
- Use declarative mood
- Use positive framing (`value cannot be negative` not `value must be non-negative`)
- Be impersonal (`too many arguments` not `you provided too many arguments`)
- Be direct (`format not recognized` not `there appears to be an issue with the format`)

**Messages must not:**

- Include hints or solutions
- Hedge or soften (`appears to`, `seems like`)


## Document Metadata

**Title:** mbdiscid Message Style Guide  
**Version:** 1.0  
**Date:** 2025-06-11  
**Author:** Ian McNish  
**License:** Creative Commons Attribution 4.0 International (CC BY 4.0)

**Related Documents:**

- [README](README.md) — Quick start and overview
- [Product Specification](Product_Specification.md) — Observable behavior and output formats
- [Implementation Specification](Implementation_Specification.md) — Internal algorithms and design rationale
