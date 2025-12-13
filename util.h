/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * util.h - Common utilities
 */

#ifndef MBDISCID_UTIL_H
#define MBDISCID_UTIL_H

#include "types.h"
#include <stdio.h>

/* Error reporting */
void error(const char *fmt, ...);
void error_quiet(bool quiet, const char *fmt, ...);

/* Verbose output (to stderr) */
void verbose(int level, int current_verbosity, const char *fmt, ...);

/* Memory allocation with error checking */
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);

/* String utilities */
char *trim(char *str);
bool is_all_digits(const char *str);
bool is_valid_isrc(const char *isrc);
bool is_valid_mcn(const char *mcn);

/* Conversion */
int32_t lba_to_seconds(int32_t lba);
int32_t frames_to_seconds(int32_t frames);

/* MSF formatting */
void lba_to_msf(int32_t lba, int *m, int *s, int *f);

#endif /* MBDISCID_UTIL_H */
