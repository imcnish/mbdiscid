/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * main.c - Entry point
 */

#include "types.h"
#include "cli.h"
#include "device.h"
#include "toc.h"
#include "discid.h"
#include "output.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Read TOC from stdin (for -c without arguments)
 */
static char *read_stdin_toc(void)
{
    char buf[4096];
    size_t total = 0;
    char *result = NULL;

    while (fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        result = xrealloc(result, total + len + 1);
        memcpy(result + total, buf, len + 1);
        total += len;
    }

    if (result) {
        /* Trim trailing newlines */
        while (total > 0 && (result[total-1] == '\n' || result[total-1] == '\r')) {
            result[--total] = '\0';
        }
    }

    return result;
}

/*
 * Calculate and store disc IDs
 * Returns 0 on success, error code if a required ID fails to calculate
 */
static int calculate_ids(disc_info_t *disc, int mode, bool quiet)
{
    char *id;
    bool need_mb = (mode == MODE_MUSICBRAINZ || mode == MODE_ALL);
    bool need_freedb = (mode == MODE_FREEDB || mode == MODE_ALL || mode == MODE_ACCURATERIP);
    bool need_ar = (mode == MODE_ACCURATERIP || mode == MODE_ALL);

    /* MusicBrainz ID */
    id = calc_musicbrainz_id(&disc->toc);
    if (id) {
        strncpy(disc->ids.musicbrainz, id, MB_ID_LENGTH);
        disc->ids.musicbrainz[MB_ID_LENGTH] = '\0';
        free(id);
    } else if (need_mb) {
        error_quiet(quiet, "discid: cannot calculate MusicBrainz ID");
        return EX_SOFTWARE;
    }

    /* FreeDB ID */
    id = calc_freedb_id(&disc->toc);
    if (id) {
        strncpy(disc->ids.freedb, id, FREEDB_ID_LENGTH);
        disc->ids.freedb[FREEDB_ID_LENGTH] = '\0';
        free(id);
    } else if (need_freedb) {
        error_quiet(quiet, "discid: cannot calculate FreeDB ID");
        return EX_SOFTWARE;
    }

    /* AccurateRip ID */
    id = calc_accuraterip_id(&disc->toc);
    if (id) {
        strncpy(disc->ids.accuraterip, id, AR_ID_LENGTH);
        disc->ids.accuraterip[AR_ID_LENGTH] = '\0';
        free(id);
    } else if (need_ar) {
        error_quiet(quiet, "discid: cannot calculate AccurateRip ID");
        return EX_SOFTWARE;
    }

    return 0;
}

int main(int argc, char **argv)
{
    options_t opts;
    int ret;

    /* Parse command line */
    ret = cli_parse(argc, argv, &opts);
    if (ret != 0) {
        return ret;
    }

    /* Handle standalone options */
    if (opts.help) {
        cli_print_help();
        return EX_OK;
    }

    if (opts.version) {
        cli_print_version();
        return EX_OK;
    }

    if (opts.list_drives) {
        return device_list_drives();
    }

    /* Validate options */
    ret = cli_validate(&opts);
    if (ret != 0) {
        return ret;
    }

    /* Apply defaults */
    cli_apply_defaults(&opts);

    disc_info_t disc;
    memset(&disc, 0, sizeof(disc));

    if (opts.calculate) {
        /* Calculate from TOC string */
        const char *toc_str = opts.cdtoc;
        char *stdin_toc = NULL;

        if (!toc_str) {
            /* Read from stdin */
            stdin_toc = read_stdin_toc();
            if (!stdin_toc || !stdin_toc[0]) {
                error_quiet(opts.quiet, "cli: -c requires TOC data");
                free(stdin_toc);
                return EX_DATAERR;
            }
            toc_str = stdin_toc;
        }

        /* Detect TOC format */
        toc_detect_result_t detected = toc_detect_format(toc_str);

        if (detected.format == TOC_FORMAT_INVALID) {
            error_quiet(opts.quiet, "%s", detected.error);
            free(stdin_toc);
            return EX_DATAERR;
        }

        if (detected.format == TOC_FORMAT_INDETERMINATE) {
            error_quiet(opts.quiet, "%s", detected.error);
            free(stdin_toc);
            return EX_DATAERR;
        }

        verbose(1, opts.verbosity, "toc: detected format: %s", toc_format_name(detected.format));

        /* Check if format is acceptable for mode */
        if (opts.mode == MODE_ACCURATERIP && detected.format == TOC_FORMAT_RAW) {
            if (!opts.assume_audio) {
                error_quiet(opts.quiet, "accuraterip: raw TOC not supported");
                free(stdin_toc);
                return EX_USAGE;
            }
            verbose(1, opts.verbosity, "toc: assuming all tracks are audio (--assume-audio)");
        }

        ret = toc_parse(&disc.toc, toc_str, detected.format, opts.verbosity);
        free(stdin_toc);

        if (ret != 0) {
            return ret;
        }

        disc.type = toc_get_disc_type(&disc.toc);
        ret = calculate_ids(&disc, opts.mode, opts.quiet);
        if (ret != 0) {
            return ret;
        }
    } else {
        /* Read from device */
        const char *device = opts.device;

        /* Determine what to read based on mode */
        int flags = 0;
        if (opts.mode == MODE_MCN || opts.mode == MODE_ALL) {
            flags |= READ_MCN;
        }
        if (opts.mode == MODE_ISRC || opts.mode == MODE_ALL) {
            flags |= READ_ISRC;
        }
        if (opts.mode == MODE_TEXT || opts.mode == MODE_ALL) {
            flags |= READ_CDTEXT;
        }

        ret = device_read_disc(device, &disc, flags, opts.verbosity);
        if (ret != 0) {
            return ret;
        }

        ret = calculate_ids(&disc, opts.mode, opts.quiet);
        if (ret != 0) {
            cdtext_free(&disc.cdtext);
            return ret;
        }
    }

    /* Generate output based on mode */
    switch (opts.mode) {
    case MODE_TYPE:
        output_type(&disc);
        break;

    case MODE_TEXT:
        output_text(&disc);
        break;

    case MODE_MCN:
        output_mcn(&disc);
        break;

    case MODE_ISRC:
        output_isrc(&disc);
        break;

    case MODE_RAW:
        output_raw_toc(&disc.toc);
        break;

    case MODE_ACCURATERIP:
        if (opts.actions & ACTION_TOC)
            output_accuraterip_toc(&disc.toc);
        if (opts.actions & ACTION_ID)
            output_accuraterip_id(disc.ids.accuraterip);
        break;

    case MODE_FREEDB:
        if (opts.actions & ACTION_TOC)
            output_freedb_toc(&disc.toc);
        if (opts.actions & ACTION_ID)
            output_freedb_id(disc.ids.freedb);
        break;

    case MODE_MUSICBRAINZ:
        if (opts.actions & ACTION_TOC)
            output_musicbrainz_toc(&disc.toc);
        if (opts.actions & ACTION_ID)
            output_musicbrainz_id(disc.ids.musicbrainz);
        if (opts.actions & ACTION_URL) {
            char *url = get_musicbrainz_url(disc.ids.musicbrainz);
            output_musicbrainz_url(url);
            free(url);
        }
        if (opts.actions & ACTION_OPEN) {
            char *url = get_musicbrainz_url(disc.ids.musicbrainz);
            output_open_url(url);
            free(url);
        }
        break;

    case MODE_ALL:
        output_all(&disc, &opts);
        break;

    default:
        /* Should not reach here after cli_apply_defaults */
        output_musicbrainz_id(disc.ids.musicbrainz);
        break;
    }

    /* Cleanup */
    cdtext_free(&disc.cdtext);

    return EX_OK;
}
