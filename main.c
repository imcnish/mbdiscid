/*
 * mbdiscid - Disc ID calculator
 * main.c - Main entry point
 */

#include "types.h"
#include "cli.h"
#include "toc.h"
#include "discid.h"
#include "device.h"
#include "output.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Read CDTOC from stdin
 */
static char *read_stdin(void)
{
    size_t bufsize = 4096;
    size_t len = 0;
    char *buf = xmalloc(bufsize);
    
    int c;
    while ((c = getchar()) != EOF) {
        if (len + 1 >= bufsize) {
            bufsize *= 2;
            buf = xrealloc(buf, bufsize);
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    
    return buf;
}

/*
 * Calculate disc IDs from TOC
 */
static int calculate_ids(disc_info_t *disc)
{
    /* AccurateRip ID */
    char *ar_id = calc_accuraterip_id(&disc->toc);
    if (ar_id) {
        strncpy(disc->ids.accuraterip, ar_id, AR_ID_LENGTH);
        disc->ids.accuraterip[AR_ID_LENGTH] = '\0';
        free(ar_id);
    }
    
    /* FreeDB ID */
    char *fb_id = calc_freedb_id(&disc->toc);
    if (fb_id) {
        strncpy(disc->ids.freedb, fb_id, FREEDB_ID_LENGTH);
        disc->ids.freedb[FREEDB_ID_LENGTH] = '\0';
        free(fb_id);
    }
    
    /* MusicBrainz ID */
    char *mb_id = calc_musicbrainz_id(&disc->toc);
    if (mb_id) {
        strncpy(disc->ids.musicbrainz, mb_id, MB_ID_LENGTH);
        disc->ids.musicbrainz[MB_ID_LENGTH] = '\0';
        free(mb_id);
    }
    
    return 0;
}

/*
 * Process CDTOC input mode
 */
static int process_cdtoc(const options_t *opts)
{
    const char *input = opts->cdtoc;
    char *stdin_buf = NULL;
    
    /* Read from stdin if no CDTOC provided */
    if (!input) {
        stdin_buf = read_stdin();
        input = stdin_buf;
    }
    
    /* Duplicate and trim whitespace */
    char *buf = xstrdup(input);
    char *trimmed = trim(buf);  /* trim returns pointer into buf */
    
    if (strlen(trimmed) == 0) {
        error_quiet(opts->quiet, "empty TOC input");
        free(buf);       /* free the original allocation */
        free(stdin_buf);
        return EX_DATAERR;
    }
    
    /* Parse TOC according to mode's format */
    toc_format_t format = cli_get_toc_format(opts->mode);
    
    disc_info_t disc;
    memset(&disc, 0, sizeof(disc));
    
    int ret = toc_parse(&disc.toc, trimmed, format);
    free(buf);           /* free the original allocation */
    free(stdin_buf);
    
    if (ret != 0) {
        error_quiet(opts->quiet, "invalid TOC format");
        return ret;
    }
    
    /* Determine disc type */
    disc.type = toc_get_disc_type(&disc.toc);
    
    /* Calculate IDs */
    calculate_ids(&disc);
    
    /* Output based on mode and actions */
    switch (opts->mode) {
    case MODE_ACCURATERIP:
        if (opts->actions & ACTION_TOC)
            output_accuraterip_toc(&disc.toc);
        if (opts->actions & ACTION_ID)
            output_accuraterip_id(disc.ids.accuraterip);
        break;
    
    case MODE_FREEDB:
        if (opts->actions & ACTION_TOC)
            output_freedb_toc(&disc.toc);
        if (opts->actions & ACTION_ID)
            output_freedb_id(disc.ids.freedb);
        break;
    
    case MODE_MUSICBRAINZ:
        if (opts->actions & ACTION_TOC)
            output_musicbrainz_toc(&disc.toc);
        if (opts->actions & ACTION_ID)
            output_musicbrainz_id(disc.ids.musicbrainz);
        if (opts->actions & ACTION_URL) {
            char *url = get_musicbrainz_url(disc.ids.musicbrainz);
            output_musicbrainz_url(url);
            if (opts->actions & ACTION_OPEN) {
                output_open_url(url);
            }
            free(url);
        } else if (opts->actions & ACTION_OPEN) {
            char *url = get_musicbrainz_url(disc.ids.musicbrainz);
            output_open_url(url);
            free(url);
        }
        break;
    
    default:
        /* Should not happen - validation should catch this */
        return EX_SOFTWARE;
    }
    
    return EX_OK;
}

/*
 * Process disc reading mode
 */
static int process_disc(const options_t *opts)
{
    const char *device = opts->device;
    if (!device) {
        device = device_get_default();
    }
    
    if (!device) {
        error_quiet(opts->quiet, "no device specified and no default available");
        return EX_USAGE;
    }
    
    verbose(1, opts->verbosity, "Reading disc from %s", device);
    
    disc_info_t disc;
    memset(&disc, 0, sizeof(disc));
    
    /* Determine what data to read based on mode */
    int read_flags = 0;
    switch (opts->mode) {
    case MODE_MCN:
        read_flags = READ_MCN;
        break;
    case MODE_ISRC:
        read_flags = READ_ISRC;
        break;
    case MODE_TEXT:
        read_flags = READ_CDTEXT;
        break;
    case MODE_ALL:
        read_flags = READ_ALL;
        break;
    default:
        /* TOC-only modes: TYPE, RAW, ACCURATERIP, FREEDB, MUSICBRAINZ */
        read_flags = 0;
        break;
    }
    
    int ret = device_read_disc(device, &disc, read_flags, opts->verbosity);
    if (ret != 0) {
        return ret;
    }
    
    /* Calculate IDs */
    calculate_ids(&disc);
    
    /* Output based on mode */
    switch (opts->mode) {
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
        if (opts->actions & ACTION_TOC)
            output_accuraterip_toc(&disc.toc);
        if (opts->actions & ACTION_ID)
            output_accuraterip_id(disc.ids.accuraterip);
        break;
    
    case MODE_FREEDB:
        if (opts->actions & ACTION_TOC)
            output_freedb_toc(&disc.toc);
        if (opts->actions & ACTION_ID)
            output_freedb_id(disc.ids.freedb);
        break;
    
    case MODE_MUSICBRAINZ:
        if (opts->actions & ACTION_TOC)
            output_musicbrainz_toc(&disc.toc);
        if (opts->actions & ACTION_ID)
            output_musicbrainz_id(disc.ids.musicbrainz);
        if (opts->actions & ACTION_URL) {
            char *url = get_musicbrainz_url(disc.ids.musicbrainz);
            output_musicbrainz_url(url);
            if (opts->actions & ACTION_OPEN) {
                output_open_url(url);
            }
            free(url);
        } else if (opts->actions & ACTION_OPEN) {
            char *url = get_musicbrainz_url(disc.ids.musicbrainz);
            output_open_url(url);
            free(url);
        }
        break;
    
    case MODE_ALL:
        output_all(&disc, opts);
        if (opts->actions & ACTION_OPEN) {
            char *url = get_musicbrainz_url(disc.ids.musicbrainz);
            output_open_url(url);
            free(url);
        }
        break;
    
    default:
        return EX_SOFTWARE;
    }
    
    cdtext_free(&disc.cdtext);
    return EX_OK;
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
    
    /* Print version banner in verbose mode */
    if (opts.verbosity > 0) {
        fprintf(stderr, "mbdiscid %s\n", VERSION);
        fprintf(stderr, "libdiscid %s\n", get_libdiscid_version());
    }
    
    /* Apply defaults */
    cli_apply_defaults(&opts);
    
    /* Validate options */
    ret = cli_validate(&opts);
    if (ret != 0) {
        return ret;
    }
    
    /* Process based on mode */
    if (opts.calculate) {
        return process_cdtoc(&opts);
    } else {
        return process_disc(&opts);
    }
}
