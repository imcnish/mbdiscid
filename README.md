# mbdiscid

Calculate disc IDs from CD or CDTOC data, supporting both MusicBrainz and FreeDB/CDDB formats.

## Features

- Read directly from CD devices
- Calculate from CDTOC data
- Support for both MusicBrainz and FreeDB disc ID formats
- Cross-platform (macOS, Linux, Windows)
- Browser integration for disc submission

## Installation

### Prerequisites

- libdiscid (https://musicbrainz.org/doc/libdiscid)
- C compiler (gcc or clang)
- make

### Building

```bash
make
sudo make install
```

To install to a different prefix:
```bash
make PREFIX=/opt/local
sudo make PREFIX=/opt/local install
```

### Uninstalling

```bash
sudo make uninstall
```

## Usage

### Reading from CD

```bash
# Get MusicBrainz disc ID
mbdiscid /dev/cdrom

# Get FreeDB disc ID
mbdiscid -F /dev/cdrom

# Get ID and TOC
mbdiscid -T /dev/cdrom

# Open submission URL in browser
mbdiscid -o /dev/cdrom
```

### Calculating from CDTOC data

```bash
# From command line
mbdiscid -c 1 12 150 17477 32100 47997 67160 84650 93732 110667 127377 147860 160437 183097 198592

# From stdin
echo "1 12 150 17477 32100 47997 67160 84650 93732 110667 127377 147860 160437 183097 198592" | mbdiscid -c

# From a file
mbdiscid -c < cdtoc.txt

# Combine with other options
cat cdtoc.txt | mbdiscid -F -T -c
```

### Platform-Specific Notes

**Darwin (macOS)**:
- Block device paths (e.g., `/dev/disk16`) may not be accessible even with sudo
- Use raw device paths instead: `/dev/rdisk16`

**Linux**: Common device paths:
- `/dev/cdrom`
- `/dev/sr0`

**Windows**: Use drive letters:
- `D:`
- `E:`

## Options

### Mode Options
- `-M, --musicbrainz` - Use MusicBrainz mode (default)
- `-F, --freedb` - Use FreeDB mode

### Action Options
- `-a, --all` - Display all information
- `-r, --raw` - Display raw TOC
- `-i, --id` - Display disc ID (default)
- `-t, --toc` - Display TOC  
- `-T, --id-toc` - Display both ID and TOC
- `-u, --url` - Display submission URL
- `-o, --open` - Open URL in browser

### Other Options
- `-c, --calculate` - Calculate from CDTOC data.
- `-h, --help` - Display help
- `-v, --version` - Display version

## CDTOC Format

CDTOC data consists of:
1. First track number (usually 1)
2. Last track number
3. Track offsets in CD frames (75 frames = 1 second)
4. Leadout offset

Example: `1 5 150 23437 45102 62385 79845 98450`
- 5 tracks
- First track starts at frame 150
- Leadout at frame 98450

## TOC Output Formats

The TOC (Table of Contents) output format varies by mode:

- **Raw format** (`-r`): `first last offset1 offset2 ... offsetN leadout`
  - All values in CD frames (75 frames = 1 second)

- **MusicBrainz format** (`-t` in MusicBrainz mode): Same as raw format

- **FreeDB format** (`-t` in FreeDB mode): `track_count offset1 offset2 ... offsetN total_seconds`
  - Track count instead of first/last
  - Total playing time in seconds instead of leadout in frames

## License

MIT License - see LICENSE file for details

## Author

Ian McNish
