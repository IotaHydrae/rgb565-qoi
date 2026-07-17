/*
 * decode_example — RGB565 QOI Decompression Example
 *
 * Reads a compressed ".qoi" file and decompresses it back to
 * RGB565 pixels.  Prints the first few pixels and statistics.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb565_qoi.h"

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *filename;
    FILE       *fp;
    long        file_size;
    uint8_t    *compressed;
    size_t      comp_size;
    uint32_t    pixel_count;
    uint16_t   *pixels;
    size_t      decomp_pixels;
    size_t      i;

    filename = (argc >= 2) ? argv[1] : "output.qoi";

    printf("RGB565 QOI Decode Example\n");
    printf("==========================\n\n");

    /* --- read input file --- */
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s'.\n", filename);
        return EXIT_FAILURE;
    }

    fseek(fp, 0L, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    if (file_size <= 0L) {
        fprintf(stderr, "Error: file is empty or unreadable.\n");
        fclose(fp);
        return EXIT_FAILURE;
    }

    comp_size = (size_t)file_size;
    compressed = (uint8_t *)malloc(comp_size);
    if (compressed == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        fclose(fp);
        return EXIT_FAILURE;
    }

    if (fread(compressed, 1u, comp_size, fp) != comp_size) {
        fprintf(stderr, "Error: failed to read input file.\n");
        free(compressed);
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);

    printf("Read %zu bytes from '%s'.\n", comp_size, filename);

    /* --- validate magic and extract pixel count from header --- */
    if (comp_size < 8u) {
        fprintf(stderr, "Error: file too small to contain a valid header.\n");
        free(compressed);
        return EXIT_FAILURE;
    }

    if ((compressed[0] != 'q') || (compressed[1] != '5') ||
        (compressed[2] != '6') || (compressed[3] != '5')) {
        fprintf(stderr, "Error: not a valid QOI-for-RGB565 file "
                "(bad magic).\n");
        free(compressed);
        return EXIT_FAILURE;
    }

    pixel_count = (uint32_t)compressed[4]
                | ((uint32_t)compressed[5] << 8)
                | ((uint32_t)compressed[6] << 16)
                | ((uint32_t)compressed[7] << 24);

    printf("Header reports %u pixels.\n", pixel_count);

    if (pixel_count == 0u) {
        fprintf(stderr, "Error: invalid pixel count in header.\n");
        free(compressed);
        return EXIT_FAILURE;
    }

    /* --- allocate output buffer --- */
    pixels = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (pixels == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        free(compressed);
        return EXIT_FAILURE;
    }

    /* --- decompress --- */
    decomp_pixels = rgb565_qoi_decompress(compressed, comp_size,
                                          pixels, pixel_count);
    if (decomp_pixels == 0u) {
        fprintf(stderr, "Error: decompression failed "
                "(corrupted data or insufficient buffer).\n");
        free(pixels);
        free(compressed);
        return EXIT_FAILURE;
    }

    printf("Decompressed %zu pixels.\n\n", decomp_pixels);

    /* --- print first 8 pixels --- */
    printf("First 8 pixels (RGB565 hex):\n");
    for (i = 0u; i < 8u && i < decomp_pixels; i++) {
        printf("  pixel[%zu] = 0x%04X  (R=%u G=%u B=%u)\n",
               i,
               (unsigned int)pixels[i],
               (unsigned int)((pixels[i] >> 11) & 0x1Fu),
               (unsigned int)((pixels[i] >> 5)  & 0x3Fu),
               (unsigned int)( pixels[i]        & 0x1Fu));
    }

    if (decomp_pixels > 8u) {
        printf("  ... (%zu more)\n", decomp_pixels - 8u);
    }

    printf("\nDone.\n");

    free(pixels);
    free(compressed);
    return EXIT_SUCCESS;
}
