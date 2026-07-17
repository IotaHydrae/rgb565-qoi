/*
 * encode_example — RGB565 QOI Compression Example
 *
 * Generates a synthetic 16x16 RGB565 test image and compresses it.
 * Writes the compressed stream to "output.qoi".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb565_qoi.h"

/* --------------------------------------------------------------------------
 * Test image dimensions
 * -------------------------------------------------------------------------- */

#define IMG_WIDTH   16u
#define IMG_HEIGHT  16u
#define IMG_PIXELS  (IMG_WIDTH * IMG_HEIGHT)   /* 256 pixels */

/* --------------------------------------------------------------------------
 * Generate a synthetic RGB565 image
 *
 * Top half: horizontal green gradient.
 * Bottom half: vertical colour bars (red / blue).
 * -------------------------------------------------------------------------- */

static void generate_test_image(uint16_t pixels[IMG_PIXELS])
{
    size_t x, y;

    for (y = 0u; y < IMG_HEIGHT; y++) {
        for (x = 0u; x < IMG_WIDTH; x++) {
            uint16_t r, g, b;

            if (y < IMG_HEIGHT / 2u) {
                /* Top half: green gradient */
                r = (uint16_t)(x * 31u / IMG_WIDTH) & 0x1Fu;
                g = (uint16_t)(y * 63u / (IMG_HEIGHT / 2u)) & 0x3Fu;
                b = 0u;
            } else {
                /* Bottom half: alternating colour blocks */
                if (x < IMG_WIDTH / 2u) {
                    r = 0x1Fu; g = 0u; b = 0u;          /* red */
                } else {
                    r = 0u; g = 0u; b = 0x1Fu;          /* blue */
                }
            }

            pixels[y * IMG_WIDTH + x] =
                (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(void)
{
    uint16_t  pixels[IMG_PIXELS];
    size_t    max_size;
    uint8_t  *compressed;
    size_t    comp_size;
    FILE     *fp;

    printf("RGB565 QOI Encode Example\n");
    printf("==========================\n\n");

    /* --- generate test image --- */
    generate_test_image(pixels);
    printf("Generated %ux%u test image (%zu pixels, %zu bytes raw).\n",
           IMG_WIDTH, IMG_HEIGHT,
           (size_t)IMG_PIXELS,
           (size_t)IMG_PIXELS * 2u);

    /* --- allocate worst-case output buffer --- */
    max_size = rgb565_qoi_max_compressed_size(IMG_PIXELS);
    if (max_size == 0u) {
        fprintf(stderr, "Error: max compressed size overflow.\n");
        return EXIT_FAILURE;
    }

    compressed = (uint8_t *)malloc(max_size);
    if (compressed == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        return EXIT_FAILURE;
    }

    /* --- compress --- */
    comp_size = rgb565_qoi_compress(pixels, IMG_PIXELS,
                                    compressed, max_size);
    if (comp_size == 0u) {
        fprintf(stderr, "Error: compression failed.\n");
        free(compressed);
        return EXIT_FAILURE;
    }

    printf("Compressed: %zu bytes (%.1f%% of raw).\n",
           comp_size,
           100.0 * (double)comp_size / (double)(IMG_PIXELS * 2u));

    /* --- save to file --- */
    fp = fopen("output.qoi", "wb");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open output.qoi for writing.\n");
        free(compressed);
        return EXIT_FAILURE;
    }

    if (fwrite(compressed, 1u, comp_size, fp) != comp_size) {
        fprintf(stderr, "Error: failed to write output file.\n");
        fclose(fp);
        free(compressed);
        return EXIT_FAILURE;
    }

    fclose(fp);
    free(compressed);

    printf("Wrote compressed data to 'output.qoi'.\n");

    return EXIT_SUCCESS;
}
