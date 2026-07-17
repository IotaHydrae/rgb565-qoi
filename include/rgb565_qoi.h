/*
 * RGB565 QOI Compression Library
 *
 * A platform-independent C library for compressing and decompressing
 * RGB565 pixel data using the Quite OK Image (QOI) algorithm,
 * adapted for 16-bit RGB565 colour format.
 *
 * This library is designed to work in freestanding environments,
 * including bare-metal MCUs, MPUs, Linux userspace, and kernel space.
 * It depends only on freestanding C99 headers: <stddef.h> and <stdint.h>.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RGB565_QOI_H
#define RGB565_QOI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * Compressed Data Format
 *---------------------------------------------------------------------------
 *
 * The compressed stream consists of an 8-byte header, followed by a sequence
 * of QOI-encoded chunks, and an 8-byte end marker:
 *
 *   Header: magic "q565" (4 bytes) + uint32_t pixel_count (little-endian)
 *   Body:   QOI chunks (variable length)
 *   End:    8-byte marker {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
 *
 * Chunk types (each identified by the first byte):
 *
 *   QOI_OP_INDEX  0x00..0x3F  (00xxxxxx)
 *     Look up the 6-bit hash index in the running array of 64 previously
 *     seen pixels.  No additional data bytes.
 *
 *   QOI_OP_DIFF   0x40..0x7F  (01xxxxxx)
 *     2-bit signed differences for R, G, B from the previous pixel,
 *     each biased by 2 (range -2..1).  Layout:
 *       bits [5:4] = dr + 2
 *       bits [3:2] = dg + 2
 *       bits [1:0] = db + 2
 *     No additional data bytes.
 *
 *   QOI_OP_LUMA   0x80..0xBF  (10xxxxxx)
 *     6-bit signed green difference (biased by 32, range -32..31) followed
 *     by one byte containing 4-bit red-green and blue-green differences
 *     (each biased by 8, range -8..7).  Layout:
 *       Byte 0 bits [5:0] = dg + 32
 *       Byte 1 bits [7:4] = (dr - dg) + 8
 *       Byte 1 bits [3:0] = (db - dg) + 8
 *
 *   QOI_OP_RUN    0xC0..0xFD  (11xxxxxx, excluding 0xFE and 0xFF)
 *     Run of identical pixels from the previous pixel value.
 *     Run length = (tag & 0x3F) + 1, range 1..62.
 *     No additional data bytes.
 *
 *   QOI_OP_RGB565 0xFE
 *     A full RGB565 pixel follows as 2 bytes (little-endian).
 *     Used when no other encoding is efficient.
 *
 * All multi-byte values are stored in little-endian byte order.
 * Channel arithmetic wraps (modulo 32 for R/B, modulo 64 for G).
 *
 * The hash function for the 64-entry index array:
 *   hash = (r * 3 + g * 5 + b * 7) & 0x3F
 *   where r = (pixel >> 11) & 0x1F  (5-bit red)
 *         g = (pixel >>  5) & 0x3F  (6-bit green)
 *         b =  pixel        & 0x1F  (5-bit blue)
 */

/*---------------------------------------------------------------------------
 * Constants
 *--------------------------------------------------------------------------- */

/** Maximum number of pixels in a single QOI run. */
#define RGB565_QOI_MAX_RUN 62u

/*---------------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------------- */

/**
 * Calculate the worst-case compressed buffer size.
 *
 * In the worst case every pixel must be emitted as QOI_OP_RGB565:
 *   1 tag byte (0xFE) + 2 data bytes = 3 bytes per pixel.
 *
 * Total worst-case: 8 (header) + pixel_count * 3 + 8 (end marker).
 *
 * @param pixel_count  Number of RGB565 pixels to compress.
 * @return  Maximum possible compressed size in bytes,
 *          or 0 if pixel_count is 0 or would cause integer overflow.
 */
size_t rgb565_qoi_max_compressed_size(size_t pixel_count);

/**
 * Compress an array of RGB565 pixels using QOI encoding.
 *
 * @param pixels           Input pixel array (uint16_t per pixel, RGB565).
 * @param pixel_count      Number of pixels in the input.
 * @param output           Output buffer for compressed data.
 * @param output_capacity  Size of the output buffer in bytes.
 * @return  Number of bytes written to output on success,
 *          or 0 on error (NULL pointer, zero count, insufficient capacity).
 */
size_t rgb565_qoi_compress(const uint16_t *pixels,
                           size_t pixel_count,
                           uint8_t *output,
                           size_t output_capacity);

/**
 * Decompress a QOI-encoded stream back to RGB565 pixels.
 *
 * @param input           Input buffer containing compressed data.
 * @param input_size      Size of the input buffer in bytes.
 * @param pixels          Output buffer for decompressed pixels.
 * @param pixel_capacity  Maximum number of pixels the output can hold.
 * @return  Number of pixels written on success,
 *          or 0 on error (NULL pointer, bad magic, malformed data,
 *          insufficient capacity, truncated input).
 */
size_t rgb565_qoi_decompress(const uint8_t *input,
                             size_t input_size,
                             uint16_t *pixels,
                             size_t pixel_capacity);

#ifdef __cplusplus
}
#endif

#endif /* RGB565_QOI_H */
