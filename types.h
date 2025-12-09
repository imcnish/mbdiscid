/*
 * mbdiscid - Disc ID calculator
 * types.h - Core types and constants
 */

#ifndef MBDISCID_TYPES_H
#define MBDISCID_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Version */
#ifndef VERSION
#define VERSION "1.1.0"
#endif

/* Exit codes from sysexits.h */
#define EX_OK           0   /* Successful termination */
#define EX_USAGE        64  /* Command line usage error */
#define EX_DATAERR      65  /* Data format error */
#define EX_UNAVAILABLE  69  /* Service unavailable */
#define EX_SOFTWARE     70  /* Internal software error */
#define EX_IOERR        74  /* Input/output error */

/* Limits */
#define MAX_TRACKS      99
#define ISRC_LENGTH     12
#define MCN_LENGTH      13
#define MB_ID_LENGTH    28
#define FREEDB_ID_LENGTH 8
#define AR_ID_LENGTH    32  /* NNN-XXXXXXXX-XXXXXXXX-XXXXXXXX */

/* CD constants */
#define FRAMES_PER_SECOND   75
#define PREGAP_FRAMES       150

/* Disc types */
typedef enum {
    DISC_TYPE_UNKNOWN = 0,
    DISC_TYPE_AUDIO,        /* Standard CD-DA */
    DISC_TYPE_ENHANCED,     /* CD-Extra (audio + data at end) */
    DISC_TYPE_MIXED         /* Mixed Mode (data first, then audio) */
} disc_type_t;

/* Track types */
typedef enum {
    TRACK_TYPE_UNKNOWN = 0,
    TRACK_TYPE_AUDIO,
    TRACK_TYPE_DATA
} track_type_t;

/* Mode flags (mutually exclusive) */
typedef enum {
    MODE_NONE       = 0,
    MODE_TYPE       = (1 << 0),   /* -T */
    MODE_TEXT       = (1 << 1),   /* -X */
    MODE_MCN        = (1 << 2),   /* -C */
    MODE_ISRC       = (1 << 3),   /* -I */
    MODE_RAW        = (1 << 4),   /* -R */
    MODE_ACCURATERIP= (1 << 5),   /* -A */
    MODE_FREEDB     = (1 << 6),   /* -F */
    MODE_MUSICBRAINZ= (1 << 7),   /* -M */
    MODE_ALL        = (1 << 8)    /* -a */
} cli_mode_t;

/* Action flags (may be combined) */
typedef enum {
    ACTION_NONE     = 0,
    ACTION_TOC      = (1 << 0),   /* -t */
    ACTION_ID       = (1 << 1),   /* -i */
    ACTION_URL      = (1 << 2),   /* -u */
    ACTION_OPEN     = (1 << 3)    /* -o */
} action_t;

/* Track information */
typedef struct {
    int number;             /* Track number (1-99) */
    track_type_t type;      /* Audio or data */
    int32_t offset;         /* Start LBA (0-based) */
    int32_t length;         /* Length in frames */
    char isrc[ISRC_LENGTH + 1];
} track_t;

/* TOC structure */
typedef struct {
    int first_track;        /* First track number */
    int last_track;         /* Last track number */
    int track_count;        /* Total tracks */
    int audio_count;        /* Audio track count */
    int data_count;         /* Data track count */
    int32_t leadout;        /* Leadout LBA */
    int32_t audio_leadout;  /* Audio session leadout (for Enhanced CDs) */
    track_t tracks[MAX_TRACKS];
} toc_t;

/* CD-Text tags (album and track level) */
typedef struct {
    char *album;
    char *albumartist;
    char *genre;
    char *lyricist;
    char *composer;
    char *arranger;
    char *comment;
} cdtext_album_t;

typedef struct {
    char *title;
    char *artist;
    char *lyricist;
    char *composer;
    char *arranger;
    char *comment;
} cdtext_track_t;

/* Full CD-Text */
typedef struct {
    cdtext_album_t album;
    cdtext_track_t tracks[MAX_TRACKS];
    int track_count;
} cdtext_t;

/* Disc identifiers */
typedef struct {
    char musicbrainz[MB_ID_LENGTH + 1];
    char freedb[FREEDB_ID_LENGTH + 1];
    char accuraterip[AR_ID_LENGTH + 1];
    char mcn[MCN_LENGTH + 1];
} disc_ids_t;

/* Complete disc information */
typedef struct {
    disc_type_t type;
    toc_t toc;
    cdtext_t cdtext;
    disc_ids_t ids;
    bool has_cdtext;
    bool has_mcn;
    bool has_isrc;
} disc_info_t;

/* Command-line options */
typedef struct {
    cli_mode_t mode;
    action_t actions;
    bool calculate;         /* -c: use CDTOC input */
    bool quiet;             /* -q: suppress errors */
    int verbosity;          /* -v count */
    bool list_drives;       /* -L */
    bool help;              /* -h */
    bool version;           /* -V */
    const char *device;     /* Device path or NULL */
    const char *cdtoc;      /* CDTOC string or NULL (stdin if -c alone) */
} options_t;

/* TOC input format (for -c) */
typedef enum {
    TOC_FORMAT_MUSICBRAINZ, /* first last leadout offset1...offsetN */
    TOC_FORMAT_ACCURATERIP, /* count audio first offset1...offsetN leadout */
    TOC_FORMAT_FREEDB       /* count offset1...offsetN total_seconds */
} toc_format_t;

#endif /* MBDISCID_TYPES_H */
