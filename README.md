# mbdiscid

A command-line tool for calculating disc IDs and reading CD metadata.

## Why Another Disc ID Tool?

Existing tools for reading ISRC codes from CDs—including `icedax`, `cd-info`, and `cdrdao`—produce unreliable results. `mbdiscid` addresses this with consensus-based extraction using multiple read passes and strict validation.

## ISRC Extraction Reliability

Existing tools for reading ISRC codes—`icedax`, `cd-info`, and `cdrdao`—produce unreliable results due to their single-read approach to subchannel data.

| Tool | Detection Rate | Notes |
|------|----------------|-------|
| **mbdiscid** | **100%** | Consensus-based, multiple reads |
| cdrdao | 95.3% | Random false negatives |
| icedax | 92.6% | Random false negatives |
| cd-info | 31.5%* | Fails entirely on some drives |

*\* cd-info's low rate is entirely due to drive incompatibility: 100% on LG drives, 0% on Pioneer drives—not random failures.*

Testing across 16 discs with 5 consecutive reads revealed that failures on Pioneer drives are *non-deterministic*—the same tool misses different tracks on each run. On LG drives, all tools achieved 100% consistency, suggesting drive firmware differences in subchannel handling.

`mbdiscid` achieves 100% detection by avoiding track boundaries, sampling from multiple positions across each track (~500 frames total), performing CRC verification, and using consensus voting.

See [ISRC Extraction: Detailed Findings](ISRC_Extraction_Findings.md) for complete test methodology and failure analysis.

## Features

- **Disc IDs**: MusicBrainz, AccurateRip, FreeDB/CDDB
- **Metadata**: MCN (Media Catalog Number), ISRC (per-track), CD-Text
- **Media Types**: Audio CD, Enhanced CD (CD-Extra), Mixed Mode CD
- **Platforms**: Linux, macOS

## Quick Start

### Build

```bash
# Requires libdiscid
make
```

### Basic Usage

```bash
# All information from disc
mbdiscid /dev/sr0          # Linux
mbdiscid /dev/rdisk4       # macOS

# MusicBrainz disc ID only
mbdiscid -M /dev/sr0

# Calculate from TOC data (no disc needed)
mbdiscid -c "1 12 198592 150 17477 32562 ..."

# List optical drives
mbdiscid -L
```

## Modes

| Mode | Flag | Description | Requires Disc |
|------|------|-------------|---------------|
| All | `-a` | Everything (default) | Yes |
| MusicBrainz | `-M` | MusicBrainz ID/TOC/URL | No* |
| AccurateRip | `-A` | AccurateRip ID/TOC | No* |
| FreeDB | `-F` | FreeDB/CDDB ID/TOC | No* |
| Raw | `-R` | Raw device TOC | Yes |
| Type | `-T` | Media type classification | Yes |
| MCN | `-C` | Media Catalog Number | Yes |
| ISRC | `-I` | Per-track ISRCs | Yes |
| CD-Text | `-X` | Album/track text metadata | Yes |

*Can calculate from TOC data via `-c`

## Actions

| Flag | Description |
|------|-------------|
| `-i` | Display ID/value (default) |
| `-t` | Display TOC |
| `-u` | Display MusicBrainz URL |
| `-o` | Open URL in browser |

## Modifiers

| Flag | Description |
|------|-------------|
| `-c` | Calculate from TOC data instead of reading disc |
| `-q` | Quiet mode (suppress error messages) |
| `-v` | Verbose output (repeat for more: `-vv`, `-vvv`) |
| `--assume-audio` | Assume all tracks are audio (for `-Ac` with raw TOC) |

## TOC Input Formats

When using `-c`, mbdiscid accepts several TOC formats. All offset values are 0-based (raw LBA). See [Product Specification §2.11](Product_Specification.md#211-toc-format-definitions) for detailed format definitions.

**Raw TOC** is the simplest format:

```
first last offset1 ... offsetN leadout
```

Raw TOC is auto-detected and works directly with `-Mc` and `-Fc`.

**Mode-specific formats:**

| Mode | Format |
|------|--------|
| `-Mc` | `first last leadout offset1 ... offsetN` |
| `-Ac` | `track_count audio_count first_audio offset1 ... offsetN leadout` |
| `-Fc` | `track_count offset1 ... offsetN total_seconds` |

**Why AccurateRip is different:** AccurateRip IDs are calculated from both total track count and audio-only details, so the AccurateRip TOC format explicitly identifies which tracks are audio (via audio_count and first_audio). The raw TOC format doesn't distinguish between audio and data tracks.

For standard audio CDs where all tracks are audio, `--assume-audio` enables raw TOC input with `-Ac`. This produces incorrect results for Enhanced or Mixed Mode CDs.

## Platform Notes

**Linux**: Use `/dev/sr0`, `/dev/cdrom`, etc.

**macOS**: Use raw devices (`/dev/rdisk4`). The tool automatically falls back to raw if block device access fails.

## Documentation

- **[Product Specification](Product_Specification.md)**: Complete behavioral specification—modes, output formats, exit codes, error handling
- **[Implementation Specification](Implementation_Specification.md)**: Internal algorithms, platform-specific details, design rationale

## License

This project is licensed under the GNU General Public License v3.0 — see the [LICENSE](LICENSE.txt) file for details.

Copyright (C) 2025 Ian McNish
