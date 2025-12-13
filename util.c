/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * util.c - Common utilities
 */

#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/*
 * Print error message to stderr with program prefix
 */
void error(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "mbdiscid: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/*
 * Print error message unless quiet mode is enabled
 */
void error_quiet(bool quiet, const char *fmt, ...)
{
    if (quiet)
        return;

    va_list ap;
    fprintf(stderr, "mbdiscid: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/*
 * Print verbose message if verbosity level is sufficient
 */
void verbose(int level, int current_verbosity, const char *fmt, ...)
{
    if (current_verbosity < level)
        return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/*
 * Allocate memory or exit on failure
 */
void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        error("out of memory");
        exit(EX_SOFTWARE);
    }
    return ptr;
}

/*
 * Allocate zeroed memory or exit on failure
 */
void *xcalloc(size_t nmemb, size_t size)
{
    void *ptr = calloc(nmemb, size);
    if (!ptr && nmemb > 0 && size > 0) {
        error("out of memory");
        exit(EX_SOFTWARE);
    }
    return ptr;
}

/*
 * Reallocate memory or exit on failure
 */
void *xrealloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        error("out of memory");
        exit(EX_SOFTWARE);
    }
    return new_ptr;
}

/*
 * Duplicate string or exit on failure
 */
char *xstrdup(const char *s)
{
    if (!s)
        return NULL;
    char *dup = strdup(s);
    if (!dup) {
        error("out of memory");
        exit(EX_SOFTWARE);
    }
    return dup;
}

/*
 * Trim leading and trailing whitespace in-place
 */
char *trim(char *str)
{
    if (!str)
        return NULL;

    /* Trim leading */
    while (isspace((unsigned char)*str))
        str++;

    if (*str == '\0')
        return str;

    /* Trim trailing */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';

    return str;
}

/*
 * Check if string contains only digits
 */
bool is_all_digits(const char *str)
{
    if (!str || *str == '\0')
        return false;

    while (*str) {
        if (!isdigit((unsigned char)*str))
            return false;
        str++;
    }
    return true;
}

/*
 * Validate ISRC format: 2 letters + 3 alphanumeric + 2 digits + 5 digits
 */
bool is_valid_isrc(const char *isrc)
{
    if (!isrc || strlen(isrc) != ISRC_LENGTH)
        return false;

    /* Country code: 2 uppercase letters */
    if (!isupper((unsigned char)isrc[0]) || !isupper((unsigned char)isrc[1]))
        return false;

    /* Registrant code: 3 alphanumeric */
    for (int i = 2; i < 5; i++) {
        if (!isalnum((unsigned char)isrc[i]))
            return false;
    }

    /* Year: 2 digits */
    if (!isdigit((unsigned char)isrc[5]) || !isdigit((unsigned char)isrc[6]))
        return false;

    /* Designation code: 5 digits */
    for (int i = 7; i < 12; i++) {
        if (!isdigit((unsigned char)isrc[i]))
            return false;
    }

    return true;
}

/*
 * Validate MCN format: 13 digits, not all zeros
 */
bool is_valid_mcn(const char *mcn)
{
    if (!mcn || strlen(mcn) != MCN_LENGTH)
        return false;

    bool all_zero = true;
    for (int i = 0; i < MCN_LENGTH; i++) {
        if (!isdigit((unsigned char)mcn[i]))
            return false;
        if (mcn[i] != '0')
            all_zero = false;
    }

    return !all_zero;
}

/*
 * Convert LBA to seconds
 */
int32_t lba_to_seconds(int32_t lba)
{
    return lba / FRAMES_PER_SECOND;
}

/*
 * Convert frame count to seconds
 */
int32_t frames_to_seconds(int32_t frames)
{
    return frames / FRAMES_PER_SECOND;
}

/*
 * Convert LBA to MSF (minutes, seconds, frames)
 * Input is raw frame count (not adjusted for pregap)
 */
void lba_to_msf(int32_t lba, int *m, int *s, int *f)
{
    if (lba < 0) {
        *m = *s = *f = 0;
        return;
    }

    *f = lba % FRAMES_PER_SECOND;
    int total_seconds = lba / FRAMES_PER_SECOND;
    *s = total_seconds % 60;
    *m = total_seconds / 60;
}
