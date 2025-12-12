## ISRC Extraction Reliability

Testing across 16 diverse CDs revealed significant reliability gaps in existing tools. These findings motivated the development of `mbdiscid` as a best-of-breed solution for ISRC extraction.

### Test Corpus

| Disc ID | Tracks | Has ISRCs | Type | Drive |
|---------|--------|-----------|------|-------|
| m.wjLfLe7XrMz1c_iAL6qo06Q4w- | 17 | Yes | CD-DA | LG GP65NB60 |
| eafSQC0kDG0EPmE15c7vmMp6PNs- | 13 | Yes | CD-DA | LG GP65NB60 |
| ED06vnWi7Emm8fFP1Cm7R0hBHvc- | 13 | Yes | CD-DA | LG GP65NS60 |
| 4PtT2zz5BmntI7XfmT2dTpEsZ0E- | 13 | Yes | CD-DA | LG GP65NS60 |
| eoknU.IyXXaywKSXdaNZgbqkGZw- | 11 | Yes | CD-Extra | Pioneer BDR-XD08U |
| xYH60C0oTAOYn7y3CWYvrD7RMH4- | 8 audio | No | Mixed Mode | Pioneer BDR-XD08U |
| eu.FPhvxgQ78cpBNSeKdyXTp85s- | 12 | No | CD-DA | Pioneer BDR-XD08U |
| EbaBnJokyGEpgZ_1CN_RAhcLqRw- | 12 | No | CD-DA | Pioneer BDR-XD08U |
| 7qEtDcKmMSVjJ33Rc2NilSQ.j2Y- | 13 | Yes | CD-Extra | Pioneer BDR-XD08U |
| Kcogxi3TcGcQt0Ph3oRPLCotkv8- | 11 | Yes | CD-DA | Pioneer BDR-XD08U |
| TLgZb09osKy6AE9r2Nhlh5k8qOI- | 13 | Yes | CD-Extra | Pioneer BDR-XD08U |
| trirUzx2c8ohg_cxg_ho2lJTyeM- | 9 | Yes | CD-Extra | Pioneer BDR-XD08U |
| kZR8g_7vXdiFrXpGv4dNYprK7e4- | 11 | Yes | CD-DA | LG GP65NB60 |
| hO3GT18x_9qBZL3vZhhpDexHnv8- | 14 | Yes | CD-Extra | LG GP65NS60 |
| 3V0gKX.6sEHW9aXXkXg48imDLFw- | 11 | Yes | CD-DA | LG GP65NB60 |
| G_NP1b4lL0W8YMOhaEFMWzPNvjA- | 11 | No | CD-DA | LG GP65NB60 |

*16 discs total: 12 with ISRCs (149 ISRC-bearing tracks), 4 without ISRCs*

---

### Phase 1: Single-Read Detection Rates

| Tool | Tracks Detected | Missed | Success Rate |
|------|-----------------|--------|--------------|
| **mbdiscid** | 149/149 | 0 | **100.0%** |
| cdrdao | 142/149 | 7 | 95.3% |
| icedax | 138/149 | 11 | 92.6% |
| cd-info | 47/149 | 102 | 31.5% |

---

### Phase 2: Run-to-Run Consistency

To test whether tool failures are deterministic or random, each disc was read 5 consecutive times (1-second intervals) with each tool. Results revealed a stark drive-dependent pattern:

#### LG Drives (GP65NB60, GP65NS60): Perfect Consistency

| Disc | Tracks | mbdiscid | cdrdao | icedax | cd-info |
|------|--------|----------|--------|--------|---------|
| sr4 (13 tracks) | 13 | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ |
| sr5 (17 tracks) | 17 | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ |
| sr6 (13 tracks) | 13 | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ |
| sr7 (13 tracks) | 13 | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ |
| Disc 5 (11 tracks) | 11 | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ |
| Disc 7 (11 tracks) | 11 | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ |
| Disc 8 (14 tracks) | 14 | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ | 5/5 ✓ |

**All 7 ISRC-bearing discs on LG drives: 100% consistency across all tools, all runs.**

#### Pioneer Drives (BDR-XD08U): Non-Deterministic Failures

| Disc | Tracks | mbdiscid | cdrdao | icedax | cd-info |
|------|--------|----------|--------|--------|---------|
| Disc 1 (9 tracks) | 9 | 5/5 ✓ | 3/5 | 2/5 | 0/5 |
| Disc 2 (11 tracks) | 11 | 5/5 ✓ | 1/5 | 0/5 | 0/5 |
| Disc 4 (9 tracks) | 9 | 5/5 ✓ | 4/5 | 3/5 | 0/5 |
| Disc 10 (11 tracks) | 11 | 5/5 ✓ | 1/5 | 0/5 | 0/5 |

*"5/5 ✓" = all tracks detected in all 5 runs; "3/5" = perfect detection in only 3 of 5 runs*

**Key findings on Pioneer drives:**
- **mbdiscid**: 100% consistent (only tool achieving this)
- **cdrdao**: 20-80% perfect runs (varies by disc)
- **icedax**: 0-60% perfect runs (highly variable)
- **cd-info**: 0% (completely incompatible with Pioneer drives)

---

### Failure Modes

ISRC extraction failures fall into four categories, distinguished by whether the tool or the disc is at fault:

| Category | Mechanism | Tool Error? | Detected In Test Data |
|----------|-----------|-------------|----------------------|
| **False Negative** | Tool fails to read existing ISRC | Yes | Common (cdrdao, icedax, cd-info) |
| **Mastering Defect** | Disc has wrong ISRC encoded; tools read it faithfully | No | 1 case (Sublime Track 6) |
| **Track Boundary Bleed-Over** | Subchannel timing ambiguity at track transitions | No | 1 case (sr5 Track 6) |
| **Transient Bit Corruption** | Corrupted read produces valid-format but wrong ISRC | Unclear | 1 case (development testing) |

#### False Negatives

The dominant failure mode. Tools fail to return an ISRC that exists on the disc.

**cd-info is drive-dependent.** cd-info uses the IOCTL interface for ISRC retrieval, which behaves differently across drive hardware:

| Drive | cd-info ISRC Detection |
|-------|------------------------|
| HL-DT-ST DVDRAM GP65NS60/NB60 | Works (100% when ISRCs exist) |
| PIONEER BD-RW BDR-XD08U | Never returns ISRCs (0%) |

cd-info's 31.5% overall detection rate is entirely due to this drive incompatibility—not random failures. This makes cd-info unsuitable for general-purpose ISRC extraction.

**cdrdao and icedax fail on different tracks.** A critical finding: these tools' failures rarely overlap. On the same disc, cdrdao might miss tracks 6, 11, 12 while icedax misses tracks 1, 5, 11—suggesting different timing or seek behaviors trigger different read errors.

| Disc | Track | cdrdao | icedax | mbdiscid |
|------|-------|--------|--------|----------|
| m.wjLfLe7XrMz1c... | 15 | ✓ | ✗ | ✓ |
| m.wjLfLe7XrMz1c... | 16 | ✓ | ✗ | ✓ |
| eafSQC0kDG0EPm... | 9 | ✗ | ✓ | ✓ |
| 4PtT2zz5BmntI7... | 4 | ✓ | ✗ | ✓ |
| 4PtT2zz5BmntI7... | 12 | ✗ | ✓ | ✓ |
| TLgZb09osKy6AE... | 1 | ✓ | ✗ | ✓ |
| TLgZb09osKy6AE... | 5 | ✓ | ✗ | ✓ |
| TLgZb09osKy6AE... | 6 | ✗ | ✓ | ✓ |
| TLgZb09osKy6AE... | 11 | ✗ | ✗ | ✓ |
| TLgZb09osKy6AE... | 12 | ✗ | ✓ | ✓ |
| trirUzx2c8ohg_... | 1 | ✓ | ✗ | ✓ |
| trirUzx2c8ohg_... | 9 | ✗ | ✓ | ✓ |

Key observations:
- **Only 2 tracks** were missed by both cdrdao and icedax
- **mbdiscid detected all 149 tracks**, including those missed by all other tools
- Combined cdrdao+icedax union coverage: ~98.7% (147/149)

#### Mastering Defect

The physical disc contains incorrect ISRC data. Tools faithfully read what's there, but "what's there" is wrong.

**Sublime Track 6:** Three tools (cdrdao, icedax, mbdiscid) reported `USGA19648331` for Track 6—which is Track 5's ISRC ("April 29, 1992"). cd-info reported `USGA19649253`. Verification against the IFPI database confirmed cd-info's value is correct for "Santeria." The disc itself has corrupted subcode for Track 6; most tools accurately read the bad data. This demonstrates that **majority agreement doesn't guarantee correctness** when the disc has defective mastering.

#### Track Boundary Bleed-Over

A subchannel timing ambiguity at track transitions where tools sample different (valid) frames and get different results.

**Sublime disc (sr5):** Two instances of bleed-over were identified:

| Track | Reported ISRC | Correct ISRC | Source Track |
|-------|---------------|--------------|--------------|
| Track 6 | USGA19648331 | USGA19649253 | Track 5 |
| Track 15 | USGA19649261 | USGA19649256 | Track 14 |

For Track 6, cdrdao and icedax reported Track 5's ISRC, while mbdiscid and cd-info reported the correct Track 6 ISRC (verified against IFPI database). Each tool was 100% internally consistent across all 5 runs.

**Independent confirmation via drutil:** macOS's native `drutil subchannel` command (macOS Sequoia 15.7.1, Apple SuperDrive, CoreAudio-based — different OS, different drive, different codebase from all Linux testing) confirms both bleed-over cases are encoded on the disc itself:

```
Track  5 ISRC: USGA19648331  (from block 25)
Track  6 ISRC: USGA19648331  (from block 42)
...
Track 14 ISRC: USGA19649261  (from block 40)
Track 15 ISRC: USGA19649261  (from block 12)
```

The block offsets are informative: Track 6's ISRC was found at block 42 (~0.5 seconds into the track), confirming that the wrong ISRC persists into the early portion of the track. Tools sampling from this region read the lingering previous-track ISRC; tools sampling later read the intended value.

This demonstrates that **100% tool consistency doesn't guarantee tool agreement** when subchannel data has timing edge cases. For practical ISRC extraction, when tools disagree on a specific track, checking whether the "wrong" value matches an adjacent track's ISRC can help distinguish bleed-over from other failure modes.

#### Transient Bit Corruption

A valid-format but incorrect ISRC produced by corrupted subchannel data.

**USWB19800782 → VTXC19Q00782:** During development testing (same drives, same disc corpus, prior to formal test protocol), one read returned `VTXC19Q00782` instead of the correct `USWB19800782`. The corrupted value passed format validation but does not exist in any ISRC database. This failure was not reproduced in the formal 16-disc test corpus. The Q subchannel lacks error correction, making such corruption theoretically possible; this observation motivated implementing CRC-16 validation in mbdiscid.

#### Q Subchannel Technical Background

ISRC data is encoded in Mode 3 of the Q-channel subcode. Per the Red Book specification, ISRC frames must appear at least once per 100 Q-channel frames (~1.3 seconds at 75 frames/second). In practice, observed rates are roughly 1 per 77–96 frames. The Q subchannel includes a CRC-16 checksum (bytes 10–11) that can detect corrupted frames before they produce incorrect ISRCs.

#### Track Position Analysis

| Track Position | Failures (cdrdao+icedax combined) |
|----------------|-----------------------------------|
| Track 1 | 2 |
| Track 4 | 1 |
| Track 5 | 1 |
| Track 6 | 1 |
| Track 9 | 2 |
| Track 10 | 2 |
| **Track 11** | **4** |
| Track 12 | 2 |
| Tracks 15–16 | 2 |

Track 11 shows the highest failure count in this corpus. However, with only 12 ISRC-bearing discs and 18 total failures, the sample size is too small to establish a universal pattern. No strong first-track or last-track bias was observed in the formal test data.

---

### Tool Reliability Summary by Drive Type

| Drive | mbdiscid | cdrdao | icedax | cd-info |
|-------|----------|--------|--------|---------|
| LG | ★★★★★ | ★★★★★ | ★★★★★ | ★★★★★ |
| Pioneer | ★★★★★ | ★★★☆☆ | ★★☆☆☆ | ☆☆☆☆☆ |

**Recommendation:** For consistent ISRC extraction, use **LG drives** (eliminates false negatives for all tools) or **mbdiscid** (works reliably on both drive types and mitigates disc-level issues—see "Why mbdiscid Succeeds").

---

### Why mbdiscid Succeeds

Standard tools perform a single subchannel read per track. ISRC data is encoded in Q-channel subcode and appears approximately once per 100 frames—a single read that lands on noise or a gap will fail silently.

`mbdiscid` uses a different approach:

1. **Boundary avoidance**: Skips the first and last ~2 seconds of each track, avoiding the regions most susceptible to bleed-over and seek instability
2. **Distributed sampling**: Reads from multiple positions across the track (tranches), not just one location—reduces exposure to localized defects or mastering errors
3. **Frame accumulation**: Collects 4–7 ISRC frames per track from ~500 total frames read
4. **CRC verification**: Validates each Q-channel frame's CRC-16 checksum before considering it, rejecting corrupted reads
5. **Consensus voting**: Determines correct value from verified frames—a single bad frame cannot produce a wrong result

This eliminates the random failures inherent in single-read strategies and mitigates most disc-level issues (bleed-over, localized mastering defects) without requiring full track scans or external database lookups.

---

### Test Methodology

- **Discs**: 16 CDs selected for diversity (CD-DA, CD-Extra, Mixed Mode; various labels and pressings from 1996–2007)
- **Drives**: PIONEER BD-RW BDR-XD08U (×4), HL-DT-ST DVDRAM GP65NS60/NB60 (×4)
- **Tools**: cdrdao 1.2.4, icedax 1.1.11, cd-info 2.1.0, mbdiscid (this tool)
- **Phase 1**: Single read per disc per tool
- **Phase 2**: 5 consecutive reads per disc per tool (1-second intervals)
- **Ground truth**: Consensus across all tools plus verification against MusicBrainz database entries where available

Raw test data is available in [`test-data/isrc-comparison/`](test-data/isrc-comparison/).
