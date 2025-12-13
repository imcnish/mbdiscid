/*
 * mbdiscid - Disc ID calculator
 * Copyright (C) 2025 Ian McNish
 * SPDX-License-Identifier: GPL-3.0-or-later
 * cli.h - Command-line interface handling
 */

#ifndef MBDISCID_CLI_H
#define MBDISCID_CLI_H

#include "types.h"

/*
 * Parse command-line arguments
 * Returns 0 on success, exit code on error
 */
int cli_parse(int argc, char **argv, options_t *opts);

/*
 * Validate option combinations
 * Returns 0 on success, exit code on error
 */
int cli_validate(const options_t *opts);

/*
 * Apply default behaviors based on what was specified
 */
void cli_apply_defaults(options_t *opts);

/*
 * Print usage/help message
 */
void cli_print_help(void);

/*
 * Print version information
 */
void cli_print_version(void);

/*
 * Check if mode requires physical disc
 */
bool cli_mode_requires_disc(cli_mode_t mode);

/*
 * Check if action is valid for mode
 */
bool cli_action_valid_for_mode(action_t action, cli_mode_t mode);

/*
 * Get TOC format for current mode (for -c input)
 */
toc_format_t cli_get_toc_format(cli_mode_t mode);

#endif /* MBDISCID_CLI_H */
