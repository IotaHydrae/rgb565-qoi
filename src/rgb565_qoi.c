/*
 * RGB565 QOI Compression Library — Implementation
 *
 * Platform-independent.  Uses only <stddef.h> and <stdint.h>.
 * No dynamic allocation, no libc calls, no platform-specific APIs.
 *
 * The QOI algorithm is adapted from the reference implementation by
 * Dominic Szablewski (https://github.com/phoboslab/qoi), modified
 * for 16-bit RGB565 pixel data.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rgb565_qoi.h"

/* --------------------------------------------------------------------------
 * Internal constants
 * -------------------------------------------------------------------------- */

/** Magic bytes that identify the QOI-for-RGB565 format: "q565". */
#define QOI_MAGIC_0 0x71u  /* 'q' */
#define QOI_MAGIC_1 0x35u  /* '5' */
#define QOI_MAGIC_2 0x36u  /* '6' */
#define QOI_MAGIC_3 0x35u  /* '5' */

/** Header size in bytes (4 magic + 4 pixel_count). */
#define QOI_HEADER_SIZE 8u

/** Size of the seen-pixel hash table. */
#define QOI_INDEX_SIZE 64u

/** Chunk tag byte values (matching official QOI). */
#define QOI_OP_INDEX  0x00u  /* 00xxxxxx */
#define QOI_OP_DIFF   0x40u  /* 01xxxxxx */
#define QOI_OP_LUMA   0x80u  /* 10xxxxxx */
#define QOI_OP_RUN    0xC0u  /* 11xxxxxx */
#define QOI_OP_RGB565 0xFEu  /* 11111110  — replaces QOI_OP_RGB for RGB565 */

/** 2-bit tag mask (matching official QOI_MASK_2). */
#define QOI_MASK_2    0xC0u  /* 11000000 */

/** 6-bit data mask for run length / index / luma green diff. */
#define QOI_MASK_6    0x3Fu  /* 00111111 */

/** 8-byte end marker (matching official QOI padding). */
#define QOI_PADDING_SIZE 8u

static const uint8_t qoi_padding[QOI_PADDING_SIZE] = {
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
};

/* --------------------------------------------------------------------------
 * Helper: compute the hash index for an RGB565 pixel value
 *
 * Uses the same mixing coefficients as standard QOI (r*3, g*5, b*7)
 * adapted for the 5/6/5-bit RGB565 channel sizes.
 *
 * Matches QOI_COLOR_HASH(C) from the reference implementation, without
 * the alpha term.
 * -------------------------------------------------------------------------- */

static uint8_t qoi_color_hash(uint16_t pixel)
{
    /* Extract channels: R(5) G(6) B(5) */
    unsigned int r = (pixel >> 11) & 0x1Fu;
    unsigned int g = (pixel >>  5) & 0x3Fu;
    unsigned int b =  pixel        & 0x1Fu;

    return (uint8_t)((r * 3u + g * 5u + b * 7u) & 0x3Fu);
}

/* --------------------------------------------------------------------------
 * Helpers: little-endian read / write
 *
 * These operate byte-at-a-time with explicit shift+mask so the
 * compressed format is identical regardless of the platform's
 * native byte order.
 * -------------------------------------------------------------------------- */

static void qoi_write_u16(uint8_t *bytes, int *p, uint16_t value)
{
    bytes[(*p)++] = (uint8_t)(value & 0xFFu);
    bytes[(*p)++] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t qoi_read_u16(const uint8_t *bytes, int *p)
{
    uint16_t a = (uint16_t)bytes[(*p)++];
    uint16_t b = (uint16_t)bytes[(*p)++];
    return a | (b << 8);
}

static void qoi_write_u32(uint8_t *bytes, int *p, uint32_t value)
{
    bytes[(*p)++] = (uint8_t)(value & 0xFFu);
    bytes[(*p)++] = (uint8_t)((value >>  8) & 0xFFu);
    bytes[(*p)++] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[(*p)++] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t qoi_read_u32(const uint8_t *bytes, int *p)
{
    uint32_t a = (uint32_t)bytes[(*p)++];
    uint32_t b = (uint32_t)bytes[(*p)++];
    uint32_t c = (uint32_t)bytes[(*p)++];
    uint32_t d = (uint32_t)bytes[(*p)++];
    return a | (b << 8) | (c << 16) | (d << 24);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

size_t rgb565_qoi_max_compressed_size(size_t pixel_count)
{
    if (pixel_count == 0u) {
        return 0u;
    }

    /*
     * Worst case: every pixel is emitted as QOI_OP_RGB565.
     * Total = QOI_HEADER_SIZE + pixel_count * 3 + QOI_PADDING_SIZE.
     *
     * Check for overflow before multiplying.
     */
    if (pixel_count > (SIZE_MAX - QOI_HEADER_SIZE - QOI_PADDING_SIZE) / 3u) {
        return 0u;  /* would overflow size_t */
    }

    return QOI_HEADER_SIZE + pixel_count * 3u + QOI_PADDING_SIZE;
}

size_t rgb565_qoi_compress(const uint16_t *pixels,
                           size_t pixel_count,
                           uint8_t *output,
                           size_t output_capacity)
{
    uint16_t  index[QOI_INDEX_SIZE];
    uint16_t  px;
    uint16_t  px_prev;
    size_t    px_pos;
    size_t    px_end;
    int       p;
    int       run;
    int       i;

    /* --- parameter validation --- */
    if ((pixels == NULL) || (output == NULL) || (pixel_count == 0u)) {
        return 0u;
    }

    if (output_capacity < rgb565_qoi_max_compressed_size(pixel_count)) {
        return 0u;
    }

    /* --- zero-initialise the seen-pixel index --- */
    for (i = 0; i < (int)QOI_INDEX_SIZE; i++) {
        index[i] = 0u;
    }

    /* --- write header --- */
    output[0] = QOI_MAGIC_0;
    output[1] = QOI_MAGIC_1;
    output[2] = QOI_MAGIC_2;
    output[3] = QOI_MAGIC_3;
    p = 4;
    qoi_write_u32(output, &p, (uint32_t)pixel_count);

    /*
     * Initialise previous pixel to black {0,0,0}.
     * This matches the reference QOI encoder which starts with
     * {r:0, g:0, b:0, a:255}.
     */
    px_prev = 0x0000u;
    px      = px_prev;
    run     = 0;

    px_end = pixel_count - 1u;

    /* --- encode --- */
    for (px_pos = 0u; px_pos < pixel_count; px_pos++) {
        px = pixels[px_pos];

        if (px == px_prev) {
            run++;
            if (run == 62 || px_pos == px_end) {
                output[p++] = (uint8_t)(QOI_OP_RUN | (run - 1));
                run = 0;
            }
        } else {
            int index_pos;

            /* flush any pending run before encoding a new pixel value */
            if (run > 0) {
                output[p++] = (uint8_t)(QOI_OP_RUN | (run - 1));
                run = 0;
            }

            index_pos = (int)(qoi_color_hash(px) & 0x3Fu);

            if (index[index_pos] == px) {
                output[p++] = (uint8_t)(QOI_OP_INDEX | index_pos);
            } else {
                unsigned int cur_r, cur_g, cur_b;
                unsigned int prv_r, prv_g, prv_b;

                index[index_pos] = px;

                cur_r = (px >> 11) & 0x1Fu;
                cur_g = (px >>  5) & 0x3Fu;
                cur_b =  px        & 0x1Fu;

                prv_r = (px_prev >> 11) & 0x1Fu;
                prv_g = (px_prev >>  5) & 0x3Fu;
                prv_b =  px_prev        & 0x1Fu;

                {
                    /*
                     * Compute differences using signed 8-bit arithmetic.
                     * For 5/6-bit channels, the modular difference is computed
                     * explicitly via bit masking since signed char wrapping
                     * only works for 8-bit channels.
                     */
                    int vr = (int)((cur_r - prv_r) & 0x1Fu);
                    int vg = (int)((cur_g - prv_g) & 0x3Fu);
                    int vb = (int)((cur_b - prv_b) & 0x1Fu);

                    /* Convert to signed shortest-distance representation */
                    if (vr > 15) vr -= 32;
                    if (vg > 31) vg -= 64;
                    if (vb > 15) vb -= 32;

                    if ((vr > -3) && (vr < 2) &&
                        (vg > -3) && (vg < 2) &&
                        (vb > -3) && (vb < 2)) {
                        /* --- QOI_OP_DIFF --- */
                        output[p++] = (uint8_t)(
                            QOI_OP_DIFF | ((vr + 2) << 4)
                                        | ((vg + 2) << 2)
                                        |  (vb + 2));
                    } else {
                        int vg_r = vr - vg;
                        int vg_b = vb - vg;

                        if ((vg_r >  -9) && (vg_r <  8) &&
                            (vg   > -33) && (vg   < 32) &&
                            (vg_b >  -9) && (vg_b <  8)) {
                            /* --- QOI_OP_LUMA --- */
                            output[p++] = (uint8_t)(QOI_OP_LUMA | (vg + 32));
                            output[p++] = (uint8_t)(
                                ((vg_r + 8) << 4) | (vg_b + 8));
                        } else {
                            /* --- QOI_OP_RGB565 --- */
                            output[p++] = QOI_OP_RGB565;
                            qoi_write_u16(output, &p, px);
                        }
                    }
                }
            }
        }

        px_prev = px;
    }

    /* --- write end marker --- */
    for (i = 0; i < (int)QOI_PADDING_SIZE; i++) {
        output[p++] = qoi_padding[i];
    }

    return (size_t)p;
}

size_t rgb565_qoi_decompress(const uint8_t *input,
                             size_t input_size,
                             uint16_t *pixels,
                             size_t pixel_capacity)
{
    uint16_t  index[QOI_INDEX_SIZE];
    uint16_t  px;
    uint32_t  total_pixels;
    int       px_len;
    int       chunks_len;
    int       px_pos;
    int       p;
    int       run;
    int       i;

    /* --- parameter validation --- */
    if ((input == NULL) || (pixels == NULL)) {
        return 0u;
    }

    if (input_size < QOI_HEADER_SIZE + QOI_PADDING_SIZE) {
        return 0u;
    }

    /* --- validate magic bytes --- */
    if ((input[0] != QOI_MAGIC_0) ||
        (input[1] != QOI_MAGIC_1) ||
        (input[2] != QOI_MAGIC_2) ||
        (input[3] != QOI_MAGIC_3)) {
        return 0u;  /* not a QOI-for-RGB565 stream */
    }

    /* --- read header --- */
    p = 4;
    total_pixels = qoi_read_u32(input, &p);

    if ((total_pixels == 0u) || (total_pixels > pixel_capacity)) {
        return 0u;
    }

    /*
     * Chunks end before the 8-byte end marker, matching the reference
     * QOI decoder which computes chunks_len = size - sizeof(qoi_padding).
     */
    chunks_len = (int)input_size - (int)QOI_PADDING_SIZE;

    /* --- zero-initialise the seen-pixel index --- */
    for (i = 0; i < (int)QOI_INDEX_SIZE; i++) {
        index[i] = 0u;
    }

    /*
     * Initialise previous pixel to black {0,0,0}, matching the reference
     * QOI decoder which starts with {r:0, g:0, b:0, a:255}.
     */
    px  = 0x0000u;
    run = 0;

    px_len = (int)total_pixels;

    /* --- decode --- */
    for (px_pos = 0; px_pos < px_len; px_pos++) {
        if (run > 0) {
            run--;
        } else if (p < chunks_len) {
            int b1 = (int)input[p++];

            if (b1 == QOI_OP_RGB565) {
                /* --- QOI_OP_RGB565: full pixel follows (2 bytes LE) --- */
                px = qoi_read_u16(input, &p);
            } else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
                /* --- QOI_OP_INDEX: look up in seen-pixel array --- */
                px = index[b1 & QOI_MASK_6];
            } else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
                /* --- QOI_OP_DIFF: small per-channel differences --- */
                unsigned int r = (px >> 11) & 0x1Fu;
                unsigned int g = (px >>  5) & 0x3Fu;
                unsigned int b =  px        & 0x1Fu;

                r = (r + (((b1 >> 4) & 0x03u) - 2)) & 0x1Fu;
                g = (g + (((b1 >> 2) & 0x03u) - 2)) & 0x3Fu;
                b = (b + ( (b1      ) & 0x03u)  - 2) & 0x1Fu;

                px = (uint16_t)((r << 11) | (g << 5) | b);
            } else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
                /* --- QOI_OP_LUMA: green diff + r-g and b-g deltas --- */
                int b2;
                int vg;

                b2 = (int)input[p++];
                vg = (b1 & QOI_MASK_6) - 32;

                {
                    unsigned int r = (px >> 11) & 0x1Fu;
                    unsigned int g = (px >>  5) & 0x3Fu;
                    unsigned int b =  px        & 0x1Fu;

                    r = (r + vg - 8 + ((b2 >> 4) & 0x0Fu)) & 0x1Fu;
                    g = (g + vg)                           & 0x3Fu;
                    b = (b + vg - 8 +  (b2       & 0x0Fu)) & 0x1Fu;

                    px = (uint16_t)((r << 11) | (g << 5) | b);
                }
            } else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
                /* --- QOI_OP_RUN: repeat previous pixel --- */
                run = b1 & QOI_MASK_6;
            }
            /* else: unknown chunk type — fall through, px unchanged */

            /*
             * Update the seen-pixel index.  This matches the reference
             * decoder which updates the index after every decoded chunk
             * (including INDEX lookups).
             */
            index[qoi_color_hash(px) & 0x3Fu] = px;
        }

        pixels[px_pos] = px;
    }

    return (size_t)px_pos;
}
