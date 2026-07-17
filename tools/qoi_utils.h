/*
 * qoi_utils.h — Shared utilities for the conversion tools
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef QOI_UTILS_H
#define QOI_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 * Write compressed data as a C header file
 *
 * Produces a .h file with:
 *   - dimension #defines
 *   - compressed size #define
 *   - static const uint8_t array of the QOI data
 *
 * Returns 0 on success, non-zero on failure.
 * -------------------------------------------------------------------------- */

int write_qoi_header(FILE       *out,
                     const char  *array_name,
                     const char  *source_name,
                     int          width,
                     int          height,
                     const uint8_t *qoi_data,
                     size_t       qoi_size,
                     size_t       raw_size);

/* --------------------------------------------------------------------------
 * Write compressed data as a raw binary file (.bin)
 *
 * The file contains only the QOI stream, identical to what
 * rgb565_qoi_compress() produces.  Dimensions must be recovered from
 * the QOI header at decode time.
 *
 * Returns 0 on success, non-zero on failure.
 * -------------------------------------------------------------------------- */

int write_qoi_binary(FILE *out, const uint8_t *qoi_data, size_t qoi_size);

/* --------------------------------------------------------------------------
 * Print a size-comparison summary line
 * -------------------------------------------------------------------------- */

void print_size_comparison(const char *label,
                           size_t raw_bytes,
                           size_t compressed_bytes);

#endif /* QOI_UTILS_H */
