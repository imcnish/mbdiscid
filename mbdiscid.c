/*
 * mbdiscid - Calculate disc IDs from CD or CDTOC data
 * Copyright (c) 2025 Ian McNish
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sysexits.h>
#include <unistd.h>
#include <limits.h>
#include <discid/discid.h>

#define VERSION "1.0.0"

// Platform-specific browser opening
#ifdef __APPLE__
    #define OPEN_CMD "open"
#elif __linux__
    #define OPEN_CMD "xdg-open"
#elif _WIN32
    #define OPEN_CMD "start"
#else
    #define OPEN_CMD ""
#endif

// Mode flags
typedef enum {
    MODE_MUSICBRAINZ = 0,
    MODE_FREEDB = 1
} disc_mode_t;

// Action flags - each must be a unique power of 2 for bit operations
typedef enum {
    ACTION_ID    = 1,   // 0b0000001
    ACTION_TOC   = 2,   // 0b0000010
    ACTION_IDTOC = 4,   // 0b0000100
    ACTION_RAW   = 8,   // 0b0001000
    ACTION_URL   = 16,  // 0b0010000
    ACTION_OPEN  = 32,  // 0b0100000
    ACTION_ALL   = 64   // 0b1000000
} action_t;

// Calculate CDDB/FreeDB disc ID
unsigned int calculate_cddb_id(int num_tracks, int *offsets) {
    int n = 0;

    // Sum of digits in track start times (in seconds)
    for (int i = 1; i <= num_tracks; i++) {
        int time_seconds = offsets[i] / 75;  // Convert frames to seconds
        while (time_seconds > 0) {
            n += time_seconds % 10;
            time_seconds /= 10;
        }
    }

    // Total playing time in seconds
    int total_seconds = (offsets[0] - offsets[1]) / 75;

    // CDDB ID formula
    return ((n % 0xff) << 24) | (total_seconds << 8) | num_tracks;
}

// Calculate MusicBrainz disc ID
char* calculate_musicbrainz_id(DiscId *disc) {
    return (char*)discid_get_id(disc);
}

struct option_help {
    const char *shortopt;
    const char *longopt;
    const char *description;
};

struct argument_help {
    const char *name;
    const char *description;
};

static const struct option_help option_help[] = {
    { "-F", "--freedb",       "FreeDB/CDDB mode" },
    { "-M", "--musicbrainz",  "MusicBrainz mode (default)" },
    { "-a", "--all",          "All information" },
    { "-r", "--raw",          "Raw TOC" },
    { "-i", "--id",           "Disc ID" },
    { "-t", "--toc",          "TOC" },
    { "-T", "--id-toc",       "Disc ID and TOC" },
    { "-u", "--url",          "Submission URL" },
    { "-o", "--open",         "Open URL in browser" },
    { "-c", "--calculate",    "Calculate from CDTOC data" },
    { "-h", "--help",         "Display this help" },
    { "-v", "--version",      "Display version information" },
    { NULL, NULL, NULL }
};

static const struct argument_help argument_help[] = {
    { "DEVICE",  "CD device (e.g., /dev/disk16 or /dev/rdisk16)" },
    { "FIRST",   "First track number (usually 1)" },
    { "LAST",    "Last track number" },
    { "OFFSET",  "Track offset, one per track" },
    { "LEADOUT", "Leadout offset (last value)" },
    { NULL, NULL }
};

static void
print_options(const struct option_help *opts)
{
    fprintf(stderr, "Options\n");
    for (; opts->shortopt; opts++) {
        if (opts->longopt)
            fprintf(stderr, "  %-3s %-15s %s\n", opts->shortopt, opts->longopt, opts->description);
        else
            fprintf(stderr, "  %-3s %-15s %s\n", opts->shortopt, "", opts->description);
    }
    fprintf(stderr, "\n");
}

static void
print_arguments(const struct argument_help *args)
{
    fprintf(stderr, "Arguments\n");
    for (; args->name; args++) {
        fprintf(stderr, "  %-20s %s\n", args->name, args->description);
    }
    fprintf(stderr, "\n");
}

void print_usage(const char *argv0)
{
    const char *prog;

    prog = strrchr(argv0, '/');
    prog = prog ? prog + 1 : argv0;

    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [-a|r] [-o] <DEVICE>\n", prog);
    fprintf(stderr, "  %s [-i|t|T|u] [-F|M] [-o] <DEVICE>\n", prog);
    fprintf(stderr, "  %s [-i|t|T|u] [-F|M] [-o] -c <FIRST> <LAST> <OFFSET>... <LEADOUT>\n", prog);
    fprintf(stderr, "  %s [-i|t|T|u] [-F|M] [-o] -c\n\n", prog);

    fprintf(stderr, "Calculate disc IDs from CD or CDTOC data.\n\n");

    print_options(option_help);
    print_arguments(argument_help);

    fprintf(stderr, "Examples\n");
    fprintf(stderr, "  %s /dev/rdisk16\n", prog);
    fprintf(stderr, "  %s -F /dev/rdisk16\n", prog);
    fprintf(stderr, "  %s -tF /dev/rdisk16\n", prog);
    fprintf(stderr, "  %s -co 1 12 150 17477 32100 47997 67160 84650 93732 110667 127377 147860 160437 183097 198592\n", prog);
    fprintf(stderr, "  echo \"1 12 150 17477 32100 47997 67160 84650 93732 110667 127377 147860 160437 183097 198592\" | %s -co\n\n", prog);


    fprintf(stderr, "Notes\n");
    fprintf(stderr, "  - FreeDB was discontinued in 2020 and is no longer accepting submissions.\n");
    fprintf(stderr, "  - URL operations only supported for use with MusicBrainz.\n");
    fprintf(stderr, "\n");
}

void print_freedb_id_toc(DiscId *disc) {
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);
    int num_tracks = last_track - first_track + 1;

    // Allocate exact size needed: tracks + leadout
    int *offsets = calloc(last_track + 2, sizeof(int));
    if (!offsets) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return;
    }

    // Get track offsets
    for (int i = first_track; i <= last_track; i++) {
        offsets[i] = discid_get_track_offset(disc, i);
    }
    // Leadout at index 0
    offsets[0] = discid_get_sectors(disc);

    unsigned int cddb_id = calculate_cddb_id(num_tracks, offsets);
    printf("%08x %d", cddb_id, last_track);

    // Output offsets
    for (int i = first_track; i <= last_track; i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    // cd-discid outputs total time in seconds, not sectors
    printf(" %d\n", discid_get_sectors(disc) / 75);

    free(offsets);
}

void print_freedb_id(DiscId *disc) {
    int first_track = discid_get_first_track_num(disc);
    int last_track = discid_get_last_track_num(disc);
    int num_tracks = last_track - first_track + 1;

    // Allocate exact size needed
    int *offsets = calloc(last_track + 2, sizeof(int));
    if (!offsets) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return;
    }

    // Get track offsets
    for (int i = first_track; i <= last_track; i++) {
        offsets[i] = discid_get_track_offset(disc, i);
    }
    // Leadout at index 0
    offsets[0] = discid_get_sectors(disc);

    unsigned int cddb_id = calculate_cddb_id(num_tracks, offsets);
    printf("%08x\n", cddb_id);

    free(offsets);
}

void print_freedb_toc(DiscId *disc) {
    printf("%d", discid_get_last_track_num(disc));

    // Output offsets
    for (int i = discid_get_first_track_num(disc); i <= discid_get_last_track_num(disc); i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    // cd-discid outputs total time in seconds, not sectors
    printf(" %d\n", discid_get_sectors(disc) / 75);
}

void print_musicbrainz_toc(DiscId *disc) {
    printf("%d %d",
           discid_get_first_track_num(disc),
           discid_get_last_track_num(disc));
    for (int i = discid_get_first_track_num(disc); i <= discid_get_last_track_num(disc); i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    printf(" %d\n", discid_get_sectors(disc));
}

void print_raw_toc(DiscId *disc) {
    printf("%d %d",
           discid_get_first_track_num(disc),
           discid_get_last_track_num(disc));
    for (int i = discid_get_first_track_num(disc); i <= discid_get_last_track_num(disc); i++) {
        printf(" %d", discid_get_track_offset(disc, i));
    }
    printf(" %d\n", discid_get_sectors(disc));
}

// In main(), replace the variable declarations section with:
int main(int argc, char *argv[]) {
    disc_mode_t mode = MODE_MUSICBRAINZ;
    action_t action = 0;  // Start with no action
    int calculate_mode = 0;
    int opt;
    int mode_explicitly_set = 0;  // Track if user set mode
    const char *prog;

    static struct option long_options[] = {
        {"musicbrainz", no_argument, 0, 'M'},
        {"freedb", no_argument, 0, 'F'},
        {"id", no_argument, 0, 'i'},
        {"toc", no_argument, 0, 't'},
        {"id-toc", no_argument, 0, 'T'},
        {"raw", no_argument, 0, 'r'},
        {"url", no_argument, 0, 'u'},
        {"open", no_argument, 0, 'o'},
        {"all", no_argument, 0, 'a'},
        {"calculate", no_argument, 0, 'c'},
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "MFitTruoachv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'M':
                if (mode_explicitly_set && mode != MODE_MUSICBRAINZ) {
                    fprintf(stderr, "Error: -M/--musicbrainz and -F/--freedb are mutually exclusive\n");
                    return EX_USAGE;
                }
                mode = MODE_MUSICBRAINZ;
                mode_explicitly_set = 1;
                break;
            case 'F':
                if (mode_explicitly_set && mode != MODE_FREEDB) {
                    fprintf(stderr, "Error: -M/--musicbrainz and -F/--freedb are mutually exclusive\n");
                    return EX_USAGE;
                }
                mode = MODE_FREEDB;
                mode_explicitly_set = 1;
                break;
            case 'i':
                action |= ACTION_ID;
                break;
            case 't':
                action |= ACTION_TOC;
                break;
            case 'T':
                action |= ACTION_IDTOC;
                break;
            case 'r':
                action |= ACTION_RAW;
                break;
            case 'u':
                action |= ACTION_URL;
                break;
            case 'o':
                action |= ACTION_OPEN;
                break;
            case 'a':
                action |= ACTION_ALL;
                break;
            case 'c':
                calculate_mode = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return EX_OK;
            case 'v':
                prog = strrchr(argv[0], '/');
                prog = prog ? prog + 1 : argv[0];
                printf("%s %s\n", prog, VERSION);
                return EX_OK;
            default:
                print_usage(argv[0]);
                return EX_USAGE;
        }
    }

    // Default action if none specified
    if (action == 0) {
        action = ACTION_ID;
    }

    // Check for mode flags with -r or -a
    if (mode_explicitly_set && (action & (ACTION_RAW | ACTION_ALL))) {
        if (action & ACTION_RAW) {
            fprintf(stderr, "Error: Mode flags (-M/-F) cannot be used with -r/--raw\n");
        } else {
            fprintf(stderr, "Error: Mode flags (-M/-F) cannot be used with -a/--all\n");
        }
        return EX_USAGE;
    }

    // Check for multiple non-OPEN actions
    int non_open_actions = action & ~ACTION_OPEN;

    // Check if more than one bit is set (excluding OPEN)
    if (non_open_actions & (non_open_actions - 1)) {
        fprintf(stderr, "Error: Multiple action options specified. Actions are mutually exclusive:\n");
        fprintf(stderr, "  Choose only one of: -i, -t, -T, -r, -u, -a (can be combined with -o)\n");
        return EX_USAGE;
    }

    // Check for URL/open actions with FreeDB mode
    if (mode == MODE_FREEDB && (action & (ACTION_URL | ACTION_OPEN))) {
        fprintf(stderr, "Error: URL operations (-u, -o) are only supported for MusicBrainz mode\n");
        return EX_USAGE;
    }

    DiscId *disc = discid_new();
    if (!disc) {
        fprintf(stderr, "Error: Could not create disc object\n");
        return EX_SOFTWARE;
    }

    // Add this include at the top with the others
    #include <unistd.h>

    // In main(), in the calculate_mode section, replace the existing check with:
    if (calculate_mode) {
        // Calculate from CDTOC data
        int first_track, last_track, num_tracks;
        int *offsets = NULL;

        // Check if we have command line args or should read from stdin
        if (optind + 3 > argc) {
            // Try to read from stdin
            if (!isatty(STDIN_FILENO)) {
                char line[4096];  // Large enough for 99 tracks
                if (fgets(line, sizeof(line), stdin) == NULL) {
                    fprintf(stderr, "Error: Failed to read CDTOC data from stdin\n");
                    discid_free(disc);
                    return EX_NOINPUT;
                }

                // Parse the line into values
                int values[102];  // Max 99 tracks + first + last + leadout
                int count = 0;
                char *token = strtok(line, " \t\n");

                while (token != NULL && count < 102) {
                    char *endptr;
                    long val = strtol(token, &endptr, 10);
                    if (*endptr != '\0' || val < 0 || val > INT_MAX) {
                        fprintf(stderr, "Error: Invalid number in CDTOC data: %s\n", token);
                        discid_free(disc);
                        return EX_DATAERR;
                    }
                    values[count++] = (int)val;
                    token = strtok(NULL, " \t\n");
                }

                if (count < 3) {
                    fprintf(stderr, "Error: Insufficient CDTOC data (need at least FIRST LAST LEADOUT)\n");
                    discid_free(disc);
                    return EX_DATAERR;
                }

                first_track = values[0];
                last_track = values[1];
                num_tracks = last_track - first_track + 1;

                if (count != num_tracks + 3) {
                    fprintf(stderr, "Error: Expected %d values (first last %d_offsets leadout), got %d\n",
                            num_tracks + 3, num_tracks, count);
                    discid_free(disc);
                    return EX_DATAERR;
                }

                // Validate track range
                if (first_track < 1 || last_track > 99 || first_track > last_track) {
                    fprintf(stderr, "Error: Invalid track range (first=%d, last=%d)\n",
                            first_track, last_track);
                    fprintf(stderr, "Tracks must be between 1-99 and first <= last\n");
                    discid_free(disc);
                    return EX_DATAERR;
                }

                // Allocate exact size needed
                offsets = calloc(last_track + 2, sizeof(int));
                if (!offsets) {
                    fprintf(stderr, "Error: Memory allocation failed\n");
                    discid_free(disc);
                    return EX_OSERR;
                }

                // Copy track offsets with validation
                int prev_offset = 0;
                for (int i = 0; i < num_tracks; i++) {
                    int offset = values[2 + i];
                    if (offset < prev_offset) {
                        fprintf(stderr, "Error: Offsets must be monotonically increasing\n");
                        fprintf(stderr, "Track %d offset (%d) < previous offset (%d)\n",
                                first_track + i, offset, prev_offset);
                        free(offsets);
                        discid_free(disc);
                        return EX_DATAERR;
                    }
                    offsets[first_track + i] = offset;
                    prev_offset = offset;
                }

                // Add leadout at index 0 with validation
                int leadout = values[count - 1];
                if (leadout <= prev_offset) {
                    fprintf(stderr, "Error: Leadout (%d) must be greater than last track offset (%d)\n",
                            leadout, prev_offset);
                    free(offsets);
                    discid_free(disc);
                    return EX_DATAERR;
                }
                offsets[0] = leadout;
            } else {
                fprintf(stderr, "Error: -c requires CDTOC data\n");
                print_usage(argv[0]);
                discid_free(disc);
                return EX_USAGE;
            }
        } else {
            // Parse from command line arguments (existing code)
            first_track = atoi(argv[optind]);
            last_track = atoi(argv[optind + 1]);

            // Validate track range
            if (first_track < 1 || last_track > 99 || first_track > last_track) {
                fprintf(stderr, "Error: Invalid track range (first=%d, last=%d)\n",
                        first_track, last_track);
                fprintf(stderr, "Tracks must be between 1-99 and first <= last\n");
                discid_free(disc);
                return EX_DATAERR;
            }

            num_tracks = last_track - first_track + 1;
            int expected_args = optind + 2 + num_tracks + 1;

            if (argc != expected_args) {
                fprintf(stderr, "Error: Expected %d track offsets plus leadout, got %d values\n",
                        num_tracks, argc - optind - 2);
                discid_free(disc);
                return EX_USAGE;
            }

            // Allocate exact size needed
            offsets = calloc(last_track + 2, sizeof(int));
            if (!offsets) {
                fprintf(stderr, "Error: Memory allocation failed\n");
                discid_free(disc);
                return EX_OSERR;
            }

            // Parse track offsets with validation
            int prev_offset = 0;
            for (int i = 0; i < num_tracks; i++) {
                int offset = atoi(argv[optind + 2 + i]);
                if (offset < 0) {
                    fprintf(stderr, "Error: Negative offset %d for track %d\n",
                            offset, first_track + i);
                    free(offsets);
                    discid_free(disc);
                    return EX_DATAERR;
                }
                if (offset < prev_offset) {
                    fprintf(stderr, "Error: Offsets must be monotonically increasing\n");
                    fprintf(stderr, "Track %d offset (%d) < previous offset (%d)\n",
                            first_track + i, offset, prev_offset);
                    free(offsets);
                    discid_free(disc);
                    return EX_DATAERR;
                }
                offsets[first_track + i] = offset;
                prev_offset = offset;
            }

            // Add leadout at index 0 with validation
            int leadout = atoi(argv[optind + 2 + num_tracks]);
            if (leadout <= prev_offset) {
                fprintf(stderr, "Error: Leadout (%d) must be greater than last track offset (%d)\n",
                        leadout, prev_offset);
                free(offsets);
                discid_free(disc);
                return EX_DATAERR;
            }
            offsets[0] = leadout;
        }

        // Populate disc with TOC data
        if (!discid_put(disc, first_track, last_track, offsets)) {
            fprintf(stderr, "Error: %s\n", discid_get_error_msg(disc));
            discid_free(disc);
            free(offsets);
            return EX_DATAERR;
        }
        free(offsets);
    } else {
        // Read from device
        if (optind >= argc) {
            fprintf(stderr, "Error: No device specified\n");
            print_usage(argv[0]);
            discid_free(disc);
            return EX_USAGE;
        }

        char *device = argv[optind];
        if (!discid_read_sparse(disc, device, DISCID_FEATURE_READ)) {
            fprintf(stderr, "Error: %s\n", discid_get_error_msg(disc));
            discid_free(disc);
            return EX_NOINPUT;
        }
    }

    // Perform the requested action(s)
    // Strip OPEN bit to get the primary action
    int primary_action = action & ~ACTION_OPEN;

    switch (primary_action) {
        case ACTION_ID:
            if (mode == MODE_FREEDB) {
                print_freedb_id(disc);
            } else {
                printf("%s\n", discid_get_id(disc));
            }
            break;

        case ACTION_TOC:
            if (mode == MODE_FREEDB) {
                print_freedb_toc(disc);
            } else {
                print_musicbrainz_toc(disc);
            }
            break;

        case ACTION_IDTOC:
            if (mode == MODE_FREEDB) {
                print_freedb_id_toc(disc);
            } else {
                printf("%s ", discid_get_id(disc));
                print_musicbrainz_toc(disc);
            }
            break;

        case ACTION_RAW:
            print_raw_toc(disc);
            break;

        case ACTION_URL:
            printf("%s\n", discid_get_submission_url(disc));
            break;

        case ACTION_ALL:
            printf("=== FreeDB ===\n");
            print_freedb_id_toc(disc);
            printf("=== MusicBrainz ===\n");
            printf("%s ", discid_get_id(disc));
            print_musicbrainz_toc(disc);
            printf("%s\n", discid_get_submission_url(disc));
            break;

        case 0:  // Just ACTION_OPEN by itself
            // Do nothing here, will open browser below
            break;
    }

    // Handle browser opening if requested
    if (action & ACTION_OPEN) {
        #ifdef OPEN_CMD
        if (strlen(OPEN_CMD) > 0) {
            char cmd[2048];
            snprintf(cmd, sizeof(cmd), "%s \"%s\" >/dev/null 2>&1",
                     OPEN_CMD, discid_get_submission_url(disc));
            int ret = system(cmd);
            if (ret != 0) {
                fprintf(stderr, "Error: Failed to open browser\n");
                discid_free(disc);
                return EX_UNAVAILABLE;
            }
        } else {
            fprintf(stderr, "Error: Browser opening not supported on this platform\n");
            discid_free(disc);
            return EX_UNAVAILABLE;
        }
        #endif
    }

    discid_free(disc);
    return EX_OK;
}
