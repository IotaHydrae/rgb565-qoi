/*
 * img2qoi — Convert an image file to RGB565 QOI format
 *
 * Input:  PNG / JPEG / BMP / GIF / … (any format stb_image can decode)
 * Output: .h (C header with array) or .bin (raw QOI stream)
 *
 * The image is converted to RGB565 on the fly and compressed.
 * Prints a size-comparison summary.
 *
 * Usage:
 *   img2qoi [options] <input_file>
 *
 * Options:
 *   -o <file>     Output file path (default: <input>.qoi.h)
 *   -t h          Output as C header  (default)
 *   -t bin        Output as raw binary
 *   -n <name>     Array name for C header (default: derived from filename)
 *   -w <width>    Resize width  (0 = original)
 *   -h <height>   Resize height (0 = original)
 *   --help        Show this help
 *
 * Dependencies: stb_image.h (auto-downloaded by CMake)
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "rgb565_qoi.h"
#include "qoi_utils.h"

/* --------------------------------------------------------------------------
 * Pixel conversion helpers
 * -------------------------------------------------------------------------- */

/*
 * Convert an 8-bit R/G/B pixel to RGB565.
 * Each channel is assumed to be 8 bits (0–255).
 */
static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = ((uint16_t)r * 31u + 127u) / 255u;
    uint16_t g6 = ((uint16_t)g * 63u + 127u) / 255u;
    uint16_t b5 = ((uint16_t)b * 31u + 127u) / 255u;

    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

/*
 * Convert an RGBA8888 image (from stb_image) to RGB565, optionally
 * resizing by nearest-neighbour.  Caller frees `out`.
 */
static uint16_t *rgba_to_rgb565(const uint8_t *src,
                                int sw, int sh,
                                int dw, int dh)
{
    uint16_t *dst;
    int       x, y;

    dst = (uint16_t *)malloc((size_t)(dw * dh) * sizeof(uint16_t));
    if (dst == NULL) return NULL;

    for (y = 0; y < dh; y++) {
        int sy = (sh > 0) ? (y * sh / dh) : 0;

        for (x = 0; x < dw; x++) {
            int      sx = (sw > 0) ? (x * sw / dw) : 0;
            int      idx = (sy * sw + sx) * 4;  /* RGBA */
            uint8_t  r = src[idx + 0u];
            uint8_t  g = src[idx + 1u];
            uint8_t  b = src[idx + 2u];

            dst[y * dw + x] = rgb888_to_rgb565(r, g, b);
        }
    }

    return dst;
}

/* --------------------------------------------------------------------------
 * Derive a C-identifier-friendly name from a file path
 * -------------------------------------------------------------------------- */

static void make_array_name(const char *path, char *out, size_t out_sz)
{
    const char *base;
    size_t      len;
    size_t      i;

    /* strip directory */
    base = strrchr(path, '/');
    if (base != NULL) {
        base++;
    } else {
        base = strrchr(path, '\\');
        if (base != NULL) base++;
    }
    if (base == NULL) base = path;

    len = strlen(base);
    for (i = 0u; i < out_sz - 1u && i < len; i++) {
        char c = base[i];
        if (c == '.') {
            out[i] = '_';
        } else if ((c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')) {
            out[i] = c;
        } else {
            out[i] = '_';
        }
    }
    out[i] = '\0';
}

/* --------------------------------------------------------------------------
 * Derive a default output filename
 * -------------------------------------------------------------------------- */

static void make_output_name(const char *input,
                             const char *ext,
                             char *out, size_t out_sz)
{
    const char *dot;
    size_t      base_len;

    dot = strrchr(input, '.');
    if (dot != NULL) {
        base_len = (size_t)(dot - input);
    } else {
        base_len = strlen(input);
    }

    if (base_len > out_sz - strlen(ext) - 1u) {
        base_len = out_sz - strlen(ext) - 1u;
    }

    memcpy(out, input, base_len);
    out[base_len] = '\0';
    strcat(out, ext);
}

/* --------------------------------------------------------------------------
 * Usage
 * -------------------------------------------------------------------------- */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] <input_file>\n\n", prog);
    printf("Options:\n");
    printf("  -o <file>    Output file (default: <input>.qoi.h or .qoi.bin)\n");
    printf("  -t h         Output as C header  (default)\n");
    printf("  -t bin       Output as raw binary\n");
    printf("  -n <name>    C array name (default: derived from filename)\n");
    printf("  -w <width>   Resize width  (0 = keep original)\n");
    printf("  -h <height>  Resize height (0 = keep original)\n");
    printf("  --help       Show this help\n");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(int argc, char **argv)
{
    const char *input_path   = NULL;
    const char *output_path  = NULL;
    const char *array_name   = NULL;
    int         out_header   = 1;         /* default: .h */
    int         req_w         = 0;
    int         req_h         = 0;
    int         i;

    int         img_w, img_h, img_ch;
    uint8_t    *img_data   = NULL;
    int         out_w, out_h;
    uint16_t   *pixels     = NULL;
    size_t      pixel_count;
    size_t      raw_size;
    size_t      max_size;
    uint8_t    *qoi_data   = NULL;
    size_t      qoi_size;
    FILE       *fp         = NULL;
    int         ret        = EXIT_FAILURE;

    char        name_buf[128];
    char        path_buf[512];

    /* --- parse arguments --- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-o") == 0 && (i + 1 < argc)) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && (i + 1 < argc)) {
            i++;
            if (strcmp(argv[i], "bin") == 0) {
                out_header = 0;
            } else if (strcmp(argv[i], "h") == 0) {
                out_header = 1;
            } else {
                fprintf(stderr, "Error: unknown type '%s' (use 'h' or 'bin').\n",
                        argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-n") == 0 && (i + 1 < argc)) {
            array_name = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 && (i + 1 < argc)) {
            req_w = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && (i + 1 < argc)) {
            req_h = atoi(argv[++i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'.\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        } else {
            input_path = argv[i];
        }
    }

    if (input_path == NULL) {
        fprintf(stderr, "Error: no input file specified.\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* --- load image --- */
    img_data = stbi_load(input_path, &img_w, &img_h, &img_ch, 4);
    if (img_data == NULL) {
        fprintf(stderr, "Error: cannot load '%s': %s\n",
                input_path, stbi_failure_reason());
        goto done;
    }

    out_w = (req_w > 0) ? req_w : img_w;
    out_h = (req_h > 0) ? req_h : img_h;

    if (out_w <= 0 || out_h <= 0) {
        fprintf(stderr, "Error: invalid dimensions %d×%d.\n", out_w, out_h);
        goto done;
    }

    /* --- convert to RGB565 --- */
    pixels = rgba_to_rgb565(img_data, img_w, img_h, out_w, out_h);
    if (pixels == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        goto done;
    }

    pixel_count = (size_t)(out_w * out_h);
    raw_size    = pixel_count * 2u;

    /* --- compress --- */
    max_size = rgb565_qoi_max_compressed_size(pixel_count);
    if (max_size == 0u) {
        fprintf(stderr, "Error: pixel count overflow.\n");
        goto done;
    }

    qoi_data = (uint8_t *)malloc(max_size);
    if (qoi_data == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        goto done;
    }

    qoi_size = rgb565_qoi_compress(pixels, pixel_count, qoi_data, max_size);
    if (qoi_size == 0u) {
        fprintf(stderr, "Error: compression failed.\n");
        goto done;
    }

    /* --- determine output path --- */
    if (output_path == NULL) {
        make_output_name(input_path,
                         out_header ? ".qoi.h" : ".qoi.bin",
                         path_buf, sizeof(path_buf));
        output_path = path_buf;
    }

    /* --- determine array name --- */
    if (array_name == NULL && out_header) {
        make_array_name(output_path, name_buf, sizeof(name_buf));
        array_name = name_buf;
    }

    /* --- write output --- */
    fp = fopen(output_path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s' for writing.\n", output_path);
        goto done;
    }

    if (out_header) {
        if (write_qoi_header(fp, array_name, input_path,
                             out_w, out_h,
                             qoi_data, qoi_size,
                             raw_size) != 0) {
            fprintf(stderr, "Error: failed to write header.\n");
            goto done;
        }
    } else {
        if (write_qoi_binary(fp, qoi_data, qoi_size) != 0) {
            fprintf(stderr, "Error: failed to write binary.\n");
            goto done;
        }
    }

    fclose(fp);
    fp = NULL;

    /* --- summary --- */
    printf("\n");
    print_size_comparison(input_path, raw_size, qoi_size);
    printf("  Dimensions : %d × %d  (%zu pixels, RGB565)\n",
           out_w, out_h, pixel_count);
    printf("  Output     : %s  (%s)\n",
           output_path, out_header ? "C header" : "binary");
    printf("\n");

    ret = EXIT_SUCCESS;

done:
    if (fp       != NULL) fclose(fp);
    if (qoi_data != NULL) free(qoi_data);
    if (pixels   != NULL) free(pixels);
    if (img_data != NULL) stbi_image_free(img_data);

    return ret;
}
