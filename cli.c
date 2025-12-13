/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * cli.c - Command-line interface handling
 */

#include "cli.h"
#include "discid.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static const char *short_opts = "TXCIRAFMatiuocqLhVv";

static struct option long_opts[] = {
    /* Modes */
    {"type",        no_argument, NULL, 'T'},
    {"text",        no_argument, NULL, 'X'},
    {"catalog",     no_argument, NULL, 'C'},
    {"isrc",        no_argument, NULL, 'I'},
    {"raw",         no_argument, NULL, 'R'},
    {"accuraterip", no_argument, NULL, 'A'},
    {"freedb",      no_argument, NULL, 'F'},
    {"musicbrainz", no_argument, NULL, 'M'},
    {"all",         no_argument, NULL, 'a'},

    /* Actions */
    {"toc",         no_argument, NULL, 't'},
    {"id",          no_argument, NULL, 'i'},
    {"url",         no_argument, NULL, 'u'},
    {"open",        no_argument, NULL, 'o'},

    /* Modifiers */
    {"calculate",   no_argument, NULL, 'c'},
    {"quiet",       no_argument, NULL, 'q'},

    /* Standalone */
    {"list-drives", no_argument, NULL, 'L'},
    {"help",        no_argument, NULL, 'h'},
    {"version",     no_argument, NULL, 'V'},

    /* Verbose */
    {"verbose",     no_argument, NULL, 'v'},

    {NULL, 0, NULL, 0}
};

/*
 * Convert option character to mode flag
 */
static cli_mode_t char_to_mode(int c)
{
    switch (c) {
    case 'T': return MODE_TYPE;
    case 'X': return MODE_TEXT;
    case 'C': return MODE_MCN;
    case 'I': return MODE_ISRC;
    case 'R': return MODE_RAW;
    case 'A': return MODE_ACCURATERIP;
    case 'F': return MODE_FREEDB;
    case 'M': return MODE_MUSICBRAINZ;
    case 'a': return MODE_ALL;
    default:  return MODE_NONE;
    }
}

/*
 * Parse command-line arguments
 */
int cli_parse(int argc, char **argv, options_t *opts)
{
    memset(opts, 0, sizeof(*opts));

    int c;
    int mode_count = 0;

    /* Reset getopt */
    optind = 1;

    while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (c) {
        /* Modes */
        case 'T':
        case 'X':
        case 'C':
        case 'I':
        case 'R':
        case 'A':
        case 'F':
        case 'M':
        case 'a':
            if (opts->mode != MODE_NONE) {
                mode_count++;
            }
            opts->mode = char_to_mode(c);
            mode_count++;
            break;

        /* Actions */
        case 't':
            opts->actions |= ACTION_TOC;
            break;
        case 'i':
            opts->actions |= ACTION_ID;
            break;
        case 'u':
            opts->actions |= ACTION_URL;
            break;
        case 'o':
            opts->actions |= ACTION_OPEN;
            break;

        /* Modifiers */
        case 'c':
            opts->calculate = true;
            break;
        case 'q':
            opts->quiet = true;
            break;

        /* Standalone */
        case 'L':
            opts->list_drives = true;
            break;
        case 'h':
            opts->help = true;
            break;
        case 'V':
            opts->version = true;
            break;

        /* Verbose */
        case 'v':
            opts->verbosity++;
            break;

        case '?':
        default:
            return EX_USAGE;
        }
    }

    /* Check for multiple modes */
    if (mode_count > 1) {
        error_quiet(opts->quiet, "modes are mutually exclusive");
        return EX_USAGE;
    }

    /* Handle remaining arguments */
    if (optind < argc) {
        if (opts->calculate) {
            /* Collect remaining args as CDTOC */
            size_t total_len = 0;
            for (int i = optind; i < argc; i++) {
                total_len += strlen(argv[i]) + 1;
            }

            char *cdtoc = xmalloc(total_len + 1);
            cdtoc[0] = '\0';

            for (int i = optind; i < argc; i++) {
                if (i > optind)
                    strcat(cdtoc, " ");
                strcat(cdtoc, argv[i]);
            }

            /* Check if it looks like a device path */
            if (cdtoc[0] == '/') {
                error_quiet(opts->quiet, "-c expects TOC data, not a device path");
                free(cdtoc);
                return EX_USAGE;
            }

            opts->cdtoc = cdtoc;
        } else {
            /* Single device argument */
            if (argc - optind > 1) {
                error_quiet(opts->quiet, "too many arguments");
                return EX_USAGE;
            }
            opts->device = argv[optind];
        }
    }

    return 0;
}

/*
 * Validate option combinations
 */
int cli_validate(const options_t *opts)
{
    /* Standalone options skip other validation */
    if (opts->help || opts->version || opts->list_drives)
        return 0;

    /* Must have device or -c */
    if (!opts->calculate && !opts->device) {
        cli_print_help();
        return EX_USAGE;
    }

    /* -c with disc-required modes */
    if (opts->calculate) {
        if (opts->mode == MODE_TYPE || opts->mode == MODE_TEXT ||
            opts->mode == MODE_MCN || opts->mode == MODE_ISRC ||
            opts->mode == MODE_RAW || opts->mode == MODE_ALL) {

            if (opts->mode == MODE_RAW || opts->mode == MODE_ALL) {
                error_quiet(opts->quiet, "-c is mutually exclusive with -%c",
                           opts->mode == MODE_RAW ? 'R' : 'a');
            } else {
                error_quiet(opts->quiet, "-%c modes require a physical disc",
                           opts->mode == MODE_TYPE ? 'T' :
                           opts->mode == MODE_TEXT ? 'X' :
                           opts->mode == MODE_MCN ? 'C' : 'I');
            }
            return EX_USAGE;
        }
    }

    /* -u and -o only valid for MusicBrainz or All mode */
    if ((opts->actions & ACTION_URL) || (opts->actions & ACTION_OPEN)) {
        cli_mode_t effective_mode = opts->mode;
        if (effective_mode == MODE_NONE)
            effective_mode = MODE_MUSICBRAINZ;  /* Default */

        if (effective_mode != MODE_MUSICBRAINZ && effective_mode != MODE_ALL) {
            error_quiet(opts->quiet, "-u/-o not supported for this mode");
            return EX_USAGE;
        }
    }

    return 0;
}

/*
 * Apply default behaviors
 */
void cli_apply_defaults(options_t *opts)
{
    /* Standalone options - no defaults needed */
    if (opts->help || opts->version || opts->list_drives)
        return;

    /* Default mode resolution */
    if (opts->mode == MODE_NONE) {
        if (opts->calculate) {
            /* -c alone → MusicBrainz */
            opts->mode = MODE_MUSICBRAINZ;
        } else if (opts->actions != ACTION_NONE) {
            /* Action only → MusicBrainz */
            opts->mode = MODE_MUSICBRAINZ;
        } else {
            /* Nothing specified → All mode */
            opts->mode = MODE_ALL;
        }
    }

    /* Default action resolution */
    if (opts->actions == ACTION_NONE) {
        switch (opts->mode) {
        case MODE_RAW:
            /* Raw mode defaults to TOC */
            opts->actions = ACTION_TOC;
            break;
        case MODE_ALL:
            /* All mode enables all actions except open */
            opts->actions = ACTION_TOC | ACTION_ID | ACTION_URL;
            break;
        default:
            /* Other modes default to ID */
            opts->actions = ACTION_ID;
            break;
        }
    }

    /* Special handling for Raw mode with -i */
    if (opts->mode == MODE_RAW && (opts->actions & ACTION_ID)) {
        /* Raw mode has no ID, convert -i to -t */
        opts->actions &= ~ACTION_ID;
        opts->actions |= ACTION_TOC;
    }
}

/*
 * Print help message
 */
void cli_print_help(void)
{
    printf("Usage: mbdiscid [options] <DEVICE>\n");
    printf("       mbdiscid [options] -c <TOC>\n");
    printf("       mbdiscid -L\n");
    printf("\n");
    printf("Calculate disc IDs and TOC from CD or CDTOC data.\n");
    printf("\n");
    printf("Mode options (mutually exclusive):\n");
    printf("  -T, --type          Media type classification\n");
    printf("  -X, --text          CD-Text metadata\n");
    printf("  -C, --catalog       Media Catalog Number (MCN/barcode)\n");
    printf("  -I, --isrc          ISRC codes\n");
    printf("  -R, --raw           Raw TOC\n");
    printf("  -A, --accuraterip   AccurateRip ID and TOC\n");
    printf("  -F, --freedb        FreeDB/CDDB ID and TOC\n");
    printf("  -M, --musicbrainz   MusicBrainz ID and TOC\n");
    printf("  -a, --all           All modes (default)\n");
    printf("\n");
    printf("Action options (combinable):\n");
    printf("  -t, --toc           Display TOC\n");
    printf("  -i, --id            Display disc ID\n");
    printf("  -u, --url           Display MusicBrainz URL\n");
    printf("  -o, --open          Open MusicBrainz URL in browser\n");
    printf("\n");
    printf("Modifier options:\n");
    printf("  -c, --calculate     Calculate from TOC data instead of disc\n");
    printf("  -q, --quiet         Suppress error messages\n");
    printf("  -v, --verbose       Increase verbosity (repeat for more)\n");
    printf("\n");
    printf("Standalone options:\n");
    printf("  -L, --list-drives   List available optical drives\n");
    printf("  -h, --help          Display this help\n");
    printf("  -V, --version       Display version information\n");
    printf("\n");
    printf("TOC formats for -c:\n");
    printf("  -Mc: first last leadout offset1...offsetN\n");
    printf("  -Ac: count audio first offset1...offsetN leadout\n");
    printf("  -Fc: count offset1...offsetN total_seconds\n");
}

/*
 * Print version information
 */
 void cli_print_version(void)
 {
    printf("mbdiscid %s, %s\n", MBDISCID_VERSION, get_libdiscid_version());
 }

/*
 * Check if mode requires physical disc
 */
bool cli_mode_requires_disc(cli_mode_t mode)
{
    switch (mode) {
    case MODE_TYPE:
    case MODE_TEXT:
    case MODE_MCN:
    case MODE_ISRC:
    case MODE_RAW:
    case MODE_ALL:
        return true;
    default:
        return false;
    }
}

/*
 * Check if action is valid for mode
 */
bool cli_action_valid_for_mode(action_t action, cli_mode_t mode)
{
    switch (mode) {
    case MODE_RAW:
        /* Raw only supports TOC */
        return action == ACTION_TOC;

    case MODE_TYPE:
    case MODE_TEXT:
    case MODE_MCN:
    case MODE_ISRC:
        /* These only support ID (value) */
        return action == ACTION_ID;

    case MODE_ACCURATERIP:
    case MODE_FREEDB:
        /* Support TOC and ID */
        return action == ACTION_TOC || action == ACTION_ID;

    case MODE_MUSICBRAINZ:
    case MODE_ALL:
        /* Support all actions */
        return true;

    default:
        return false;
    }
}

/*
 * Get TOC format for current mode
 */
toc_format_t cli_get_toc_format(cli_mode_t mode)
{
    switch (mode) {
    case MODE_ACCURATERIP:
        return TOC_FORMAT_ACCURATERIP;
    case MODE_FREEDB:
        return TOC_FORMAT_FREEDB;
    case MODE_MUSICBRAINZ:
    default:
        return TOC_FORMAT_MUSICBRAINZ;
    }
}
