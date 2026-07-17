/*
 * qoi2img — Convert a QOI-compressed file back to an image
 *
 * Input:  .h (C header with QOI array) or .bin (raw QOI stream)
 * Output: PNG / BMP / TGA / PPM (native), JPG (optional, via stb_image_write)
 *
 * The QOI stream is decompressed to RGB565 pixels, then expanded to
 * 24-bit RGB888 for output.
 *
 * Usage:
 *   qoi2img <input_file> -o <output_file>
 *
 * Options:
 *   -o <file>     Output image path (required)
 *   -w <width>    Image width  (required if not in .h header)
 *   -h <height>   Image height (required if not in .h header)
 *   --help        Show this help
 *
 * The output format is determined by the file extension:
 *   .bmp  → BMP  (native, always available)
 *   .tga  → TGA  (native, always available)
 *   .ppm  → PPM  (native, always available)
 *   .png  → PNG  (requires stb_image_write.h)
 *   .jpg  → JPEG (requires stb_image_write.h)
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>

#ifdef HAS_STB_IMAGE_WRITE
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

#include "rgb565_qoi.h"

/* --------------------------------------------------------------------------
 * RGB565 → RGB888 expansion
 *
 * Each channel is expanded from 5/6 bits to 8 bits by replicating the
 * most significant bits into the lower bits (left-justified scaling).
 * -------------------------------------------------------------------------- */

static void rgb565_to_rgb888(uint16_t px, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (uint8_t)((px >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((px >>  5) & 0x3Fu);
    uint8_t b5 = (uint8_t)( px        & 0x1Fu);

    /* 5-bit → 8-bit: (v << 3) | (v >> 2) */
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    /* 6-bit → 8-bit: (v << 2) | (v >> 4) */
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    /* 5-bit → 8-bit */
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

/* ==========================================================================
 * .h file parser
 *
 * Parses generated C headers produced by img2qoi / video2qoi and
 * extracts the QOI byte array plus dimension metadata.
 *
 * The parser looks for:
 *   #define <prefix>_WIDTH  <number>
 *   #define <prefix>_HEIGHT <number>
 *   #define <prefix>_PIXELS <number>
 *   #define <prefix>_QOI_SIZE <number>
 *   static const uint8_t <name>[<size>] = { … };
 * ========================================================================== */

/*
 * Try to extract a dimension value from a #define line.
 * Returns 1 and sets *value if the line defines <prefix>_<suffix>.
 */
static int parse_define_int(const char *line,
                            const char *suffix,
                            int *value)
{
    const char *p = line;

    /* skip leading whitespace and '#' */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '#') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    /* expect "define" */
    if (strncmp(p, "define", 6) != 0) return 0;
    p += 6;
    if (*p != ' ' && *p != '\t') return 0;
    while (*p == ' ' || *p == '\t') p++;

    /* identifier must end with suffix */
    {
        size_t id_len = 0;
        const char *id_start = p;
        while (isalnum((unsigned char)*p) || *p == '_') { id_len++; p++; }
        if (id_len == 0) return 0;

        /* check if identifier ends with suffix */
        {
            size_t sfx_len = strlen(suffix);
            if (id_len < sfx_len) return 0;
            if (strncmp(id_start + id_len - sfx_len, suffix, sfx_len) != 0) {
                return 0;
            }
        }
    }

    /* skip whitespace before value */
    while (*p == ' ' || *p == '\t') p++;

    /* parse integer */
    {
        char *end;
        long v = strtol(p, &end, 0);
        if (end == p) return 0;
        *value = (int)v;
        return 1;
    }
}

/*
 * Locate the start of the byte array initializer in a .h file.
 *
 * Looks for a line containing "static const uint8_t" followed by
 * an array name, size, and '{'.
 *
 * On success returns 1 and sets *data_offset to the byte position
 * of the first hex value inside the braces.
 *
 * The caller is responsible for reading and decoding the hex values
 * from that point forward.
 */
static int find_array_start(FILE *fp,
                            size_t *data_size,
                            long *data_start)
{
    char  line[4096];
    long  pos_before_line;
    int   found_size = 0;

    rewind(fp);

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        /* look for array declaration */
        if (strstr(line, "static const uint8_t") != NULL) {
            /* extract size from e.g. "name[SIZE]" */
            {
                const char *lb = strchr(line, '[');
                const char *rb = NULL;

                if (lb != NULL) {
                    rb = strchr(lb + 1, ']');
                }

                if ((lb != NULL) && (rb != NULL)) {
                    char *end;
                    long sz = strtol(lb + 1, &end, 0);
                    if (end == rb) {
                        *data_size = (size_t)sz;
                        found_size = 1;
                    }
                }
            }

            /* find the opening brace */
            {
                const char *brace = strchr(line, '{');
                if (brace != NULL) {
                    /*
                     * The data starts after the '{'.
                     * We need the file position of the first
                     * character after the brace.
                     */
                    *data_start = pos_before_line
                                + (long)(brace - line) + 1L;
                    return 1;
                }

                /*
                 * Brace may be on the next line — record the
                 * current file position (after this line) and
                 * scan the next line for '{'.
                 */
                {
                    long after_line = ftell(fp);

                    if (fgets(line, (int)sizeof(line), fp) != NULL) {
                        brace = strchr(line, '{');
                        if (brace != NULL) {
                            *data_start = after_line
                                        + (long)(brace - line) + 1L;
                            return 1;
                        }
                    }
                }
            }
        }

        pos_before_line = ftell(fp);
    }

    (void)found_size;
    return 0;  /* not found */
}

/*
 * Read the .h file and extract:
 *   - width, height (from #defines)
 *   - a malloc'd buffer of QOI data bytes
 *   - the size of that buffer
 *
 * Returns 0 on success, non-zero on failure.
 */
static int parse_qoi_header_file(const char *path,
                                 int *width,
                                 int *height,
                                 uint8_t **qoi_data,
                                 size_t *qoi_size)
{
    FILE   *fp;
    char    line[4096];
    int     w = 0, h = 0;
    long    data_start = -1;
    size_t  data_size  = 0;
    size_t  byte_count;
    size_t  i;

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s'.\n", path);
        return -1;
    }

    /* --- Pass 1: scan for #define width / height --- */
    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        if (parse_define_int(line, "_WIDTH", &w)) {
            /* found */
        }
        if (parse_define_int(line, "_HEIGHT", &h)) {
            /* found */
        }
        /*
         * Also try without underscore prefix
         * (e.g. #define WIDTH 16, #define HEIGHT 16)
         */
        if (w == 0) parse_define_int(line, "WIDTH", &w);
        if (h == 0) parse_define_int(line, "HEIGHT", &h);
    }

    /* --- Pass 2: locate array --- */
    if (find_array_start(fp, &data_size, &data_start) == 0) {
        fprintf(stderr,
                "Error: cannot find QOI data array in '%s'.\n", path);
        fclose(fp);
        return -1;
    }

    if (data_size == 0) {
        fprintf(stderr,
                "Error: could not determine array size in '%s'.\n", path);
        fclose(fp);
        return -1;
    }

    /* --- Allocate and read the byte array --- */
    *qoi_data = (uint8_t *)malloc(data_size);
    if (*qoi_data == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        fclose(fp);
        return -1;
    }

    fseek(fp, data_start, SEEK_SET);

    byte_count = 0;
    while (byte_count < data_size) {
        int c;

        /* skip whitespace / commas / newlines / comments */
        for (;;) {
            c = fgetc(fp);
            if (c == EOF || c == '}' || c == ';') {
                break;
            }
            if (c == ' ' || c == '\t' || c == '\n'
                || c == '\r' || c == ',') {
                continue;
            }
            /* skip C-style comments */
            if (c == '/') {
                int next = fgetc(fp);
                if (next == '*') {
                    /* block comment: skip until *​/ */
                    int prev_c = 0;
                    for (;;) {
                        int bc = fgetc(fp);
                        if (bc == EOF) break;
                        if (prev_c == '*' && bc == '/') break;
                        prev_c = bc;
                    }
                    continue;
                } else if (next == '/') {
                    /* line comment: skip until newline */
                    int lc;
                    do { lc = fgetc(fp); }
                    while (lc != EOF && lc != '\n');
                    continue;
                } else {
                    ungetc(next, fp);
                }
            }
            break;  /* non-whitespace, non-comment character */
        }

        if (c == EOF || c == '}' || c == ';') {
            break;  /* end of array */
        }

        /* expect "0x" prefix or a decimal digit */
        if (c == '0') {
            int c2 = fgetc(fp);
            if (c2 == 'x' || c2 == 'X') {
                /* hex value */
                char hex[5];
                int  hi = 0;

                hex[hi++] = (char)c;
                hex[hi++] = (char)c2;
                while (hi < 4) {
                    int ch = fgetc(fp);
                    if (ch == EOF
                        || !isxdigit((unsigned char)ch)) {
                        ungetc(ch, fp);
                        break;
                    }
                    hex[hi++] = (char)ch;
                }
                hex[hi] = '\0';

                (*qoi_data)[byte_count] =
                    (uint8_t)strtoul(hex, NULL, 16);
                byte_count++;
            } else {
                /* "0" followed by non-x — it's just 0 */
                ungetc(c2, fp);
                (*qoi_data)[byte_count] = 0u;
                byte_count++;
            }
        } else if (isdigit((unsigned char)c)) {
            /* decimal value */
            char dec[16];
            int  di = 0;

            dec[di++] = (char)c;
            while (di < 15) {
                int ch = fgetc(fp);
                if (ch == EOF
                    || !isdigit((unsigned char)ch)) {
                    ungetc(ch, fp);
                    break;
                }
                dec[di++] = (char)ch;
            }
            dec[di] = '\0';

            (*qoi_data)[byte_count] =
                (uint8_t)strtoul(dec, NULL, 10);
            byte_count++;
        } else if (c == '\'') {
            /* character literal, e.g. 'q' */
            int ch = fgetc(fp);
            int endq = fgetc(fp);  /* closing quote */
            if (ch != EOF && endq == '\'') {
                (*qoi_data)[byte_count] = (uint8_t)ch;
                byte_count++;
            }
        }
        /* unknown character — skip it */
    }

    fclose(fp);

    *qoi_size = byte_count;
    *width    = w;
    *height   = h;

    if (byte_count == 0) {
        fprintf(stderr, "Error: no data found in '%s'.\n", path);
        free(*qoi_data);
        *qoi_data = NULL;
        return -1;
    }

    return 0;
}

/* ==========================================================================
 * Usage
 * ========================================================================== */

static void print_usage(const char *prog)
{
    printf("Usage: %s <input_file> -o <output_file> [-w <W> -h <H>]\n\n", prog);
    printf("  <input_file>   .h (C header) or .bin (raw QOI stream)\n");
    printf("  -o <file>      Output image path (required)\n");
    printf("  -w <width>     Image width  (required for .bin input)\n");
    printf("  -h <height>    Image height (required for .bin input)\n");
    printf("  --help         Show this help\n\n");
    printf("Output format is determined by extension:\n");
    printf("  .png → PNG  (native, via zlib)\n");
    printf("  .bmp → BMP  (native)\n");
    printf("  .tga → TGA  (native)\n");
    printf("  .ppm → PPM  (native)\n");
#ifdef HAS_STB_IMAGE_WRITE
    printf("  .jpg → JPEG (via stb_image_write)\n");
#endif
}

/* ==========================================================================
 * Native image writers (BMP, TGA, PPM) — no external dependencies
 * ========================================================================== */

/*
 * Write a 24-bit BMP file.
 *
 * BMP format: file header (14 bytes) + DIB header (40 bytes) + pixel data.
 * Pixels are stored bottom-up, BGR order, with 4-byte row alignment.
 */
static int write_bmp(const char *path,
                     int w, int h,
                     const uint8_t *rgb)
{
    FILE *fp;
    int   row_size;
    int   pad;
    int   y;
    int   file_size;
    int   data_offset = 54;  /* 14 + 40 */
    uint8_t hdr[54];

    /* Each row is padded to a multiple of 4 bytes */
    row_size = w * 3;
    pad      = (4 - (row_size & 3)) & 3;
    row_size += pad;

    file_size = data_offset + row_size * h;

    memset(hdr, 0, sizeof(hdr));

    /* BMP file header (14 bytes) */
    hdr[0]  = 'B';
    hdr[1]  = 'M';
    hdr[2]  = (uint8_t)(file_size & 0xFFu);
    hdr[3]  = (uint8_t)((file_size >>  8) & 0xFFu);
    hdr[4]  = (uint8_t)((file_size >> 16) & 0xFFu);
    hdr[5]  = (uint8_t)((file_size >> 24) & 0xFFu);
    hdr[10] = (uint8_t)(data_offset & 0xFFu);
    hdr[11] = (uint8_t)((data_offset >>  8) & 0xFFu);
    hdr[12] = (uint8_t)((data_offset >> 16) & 0xFFu);
    hdr[13] = (uint8_t)((data_offset >> 24) & 0xFFu);

    /* DIB header (BITMAPINFOHEADER, 40 bytes) */
    hdr[14] = 40;                              /* header size */
    hdr[18] = (uint8_t)(w & 0xFFu);
    hdr[19] = (uint8_t)((w >>  8) & 0xFFu);
    hdr[20] = (uint8_t)((w >> 16) & 0xFFu);
    hdr[21] = (uint8_t)((w >> 24) & 0xFFu);
    hdr[22] = (uint8_t)(h & 0xFFu);
    hdr[23] = (uint8_t)((h >>  8) & 0xFFu);
    hdr[24] = (uint8_t)((h >> 16) & 0xFFu);
    hdr[25] = (uint8_t)((h >> 24) & 0xFFu);
    hdr[26] = 1;                               /* planes */
    hdr[28] = 24;                              /* bits per pixel */

    fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    if (fwrite(hdr, 1u, (size_t)data_offset, fp) != (size_t)data_offset) {
        fclose(fp);
        return 0;
    }

    /* Write pixel rows bottom-up, BGR order */
    {
        uint8_t pad_bytes[4] = {0, 0, 0, 0};

        for (y = h - 1; y >= 0; y--) {
            int x;

            for (x = 0; x < w; x++) {
                size_t idx = (size_t)(y * w + x) * 3u;
                uint8_t bgr[3];

                bgr[0] = rgb[idx + 2u];  /* B */
                bgr[1] = rgb[idx + 1u];  /* G */
                bgr[2] = rgb[idx + 0u];  /* R */

                if (fwrite(bgr, 1u, 3u, fp) != 3u) {
                    fclose(fp);
                    return 0;
                }
            }

            if (pad > 0) {
                fwrite(pad_bytes, 1u, (size_t)pad, fp);
            }
        }
    }

    fclose(fp);
    return 1;
}

/*
 * Write a 24-bit TGA file (uncompressed, bottom-left origin).
 *
 * TGA header is 18 bytes.  Pixels are stored bottom-up, BGR order.
 */
static int write_tga(const char *path,
                     int w, int h,
                     const uint8_t *rgb)
{
    FILE   *fp;
    uint8_t hdr[18];
    int     y;

    memset(hdr, 0, sizeof(hdr));
    hdr[2]  = 2;                                /* image type: uncompressed true-colour */
    hdr[12] = (uint8_t)(w & 0xFFu);
    hdr[13] = (uint8_t)((w >> 8) & 0xFFu);
    hdr[14] = (uint8_t)(h & 0xFFu);
    hdr[15] = (uint8_t)((h >> 8) & 0xFFu);
    hdr[16] = 24;                               /* bits per pixel */
    hdr[17] = 0x20;                             /* origin: top-left (bit 5 = 0 for bottom-left) */
    /* Actually use bottom-left for TGA compatibility */
    hdr[17] = 0x00;

    fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    if (fwrite(hdr, 1u, 18u, fp) != 18u) {
        fclose(fp);
        return 0;
    }

    /* Pixels bottom-up, BGR */
    for (y = h - 1; y >= 0; y--) {
        int x;

        for (x = 0; x < w; x++) {
            size_t idx = (size_t)(y * w + x) * 3u;
            uint8_t bgr[3];

            bgr[0] = rgb[idx + 2u];  /* B */
            bgr[1] = rgb[idx + 1u];  /* G */
            bgr[2] = rgb[idx + 0u];  /* R */

            if (fwrite(bgr, 1u, 3u, fp) != 3u) {
                fclose(fp);
                return 0;
            }
        }
    }

    fclose(fp);
    return 1;
}

/*
 * Write a 24-bit PPM file (P6 binary format).
 *
 * The simplest possible image format — just a short ASCII header
 * followed by raw RGB bytes.
 */
static int write_ppm(const char *path,
                     int w, int h,
                     const uint8_t *rgb)
{
    FILE *fp;

    fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    fprintf(fp, "P6\n%d %d\n255\n", w, h);

    if (fwrite(rgb, 1u, (size_t)(w * h) * 3u, fp) != (size_t)(w * h) * 3u) {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

/* ==========================================================================
 * Native PNG writer — uses zlib for deflate compression
 * ========================================================================== */

/* CRC32 lookup table for PNG chunk integrity checks */
static uint32_t crc32_table[256];
static int      crc32_table_ready = 0;

static void crc32_init(void)
{
    uint32_t i, j;
    for (i = 0u; i < 256u; i++) {
        uint32_t c = i;
        for (j = 0u; j < 8u; j++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

static uint32_t crc32_compute(const uint8_t *data, size_t len,
                              uint32_t crc)
{
    size_t i;
    if (!crc32_table_ready) crc32_init();
    crc ^= 0xFFFFFFFFu;
    for (i = 0u; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* Write a big-endian uint32_t to file */
static int fwrite_u32(uint32_t v, FILE *fp)
{
    uint8_t b[4];
    b[0] = (uint8_t)((v >> 24) & 0xFFu);
    b[1] = (uint8_t)((v >> 16) & 0xFFu);
    b[2] = (uint8_t)((v >>  8) & 0xFFu);
    b[3] = (uint8_t)( v        & 0xFFu);
    return (fwrite(b, 1u, 4u, fp) == 4u);
}

/* Write a PNG chunk: [length:4][type:4][data...][crc:4] */
static int write_png_chunk(FILE *fp, const char *type,
                           const uint8_t *data, uint32_t length)
{
    uint32_t crc;
    uint8_t  type_bytes[4];

    if (!fwrite_u32(length, fp)) return 0;

    type_bytes[0] = (uint8_t)type[0];
    type_bytes[1] = (uint8_t)type[1];
    type_bytes[2] = (uint8_t)type[2];
    type_bytes[3] = (uint8_t)type[3];

    if (fwrite(type_bytes, 1u, 4u, fp) != 4u) return 0;

    if ((length > 0u) && (data != NULL)) {
        if (fwrite(data, 1u, (size_t)length, fp) != (size_t)length) {
            return 0;
        }
    }

    /* CRC = CRC32(type || data) */
    crc = crc32_compute(type_bytes, 4u, 0u);
    if ((length > 0u) && (data != NULL)) {
        crc = crc32_compute(data, (size_t)length, crc);
    }

    return fwrite_u32(crc, fp);
}

/*
 * Write a 24-bit PNG file using zlib for IDAT compression.
 *
 * Uses PNG filter type 0 (None) for simplicity — each row is
 * prefixed with a 0x00 filter byte, then R,G,B triples.
 */
static int write_png(const char *path,
                     int w, int h,
                     const uint8_t *rgb)
{
    FILE    *fp;
    uint8_t  ihdr_data[13];
    int      y;
    uint8_t *raw_rows;
    size_t   raw_size;
    uint8_t *compressed;
    size_t   comp_bound;
    int      ret;

    /* --- PNG signature --- */
    {
        static const uint8_t sig[8] = {
            0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n'
        };
        fp = fopen(path, "wb");
        if (fp == NULL) return 0;
        if (fwrite(sig, 1u, 8u, fp) != 8u) {
            fclose(fp); return 0;
        }
    }

    /* --- IHDR chunk --- */
    ihdr_data[0]  = (uint8_t)((w >> 24) & 0xFFu);
    ihdr_data[1]  = (uint8_t)((w >> 16) & 0xFFu);
    ihdr_data[2]  = (uint8_t)((w >>  8) & 0xFFu);
    ihdr_data[3]  = (uint8_t)( w        & 0xFFu);
    ihdr_data[4]  = (uint8_t)((h >> 24) & 0xFFu);
    ihdr_data[5]  = (uint8_t)((h >> 16) & 0xFFu);
    ihdr_data[6]  = (uint8_t)((h >>  8) & 0xFFu);
    ihdr_data[7]  = (uint8_t)( h        & 0xFFu);
    ihdr_data[8]  = 8;     /* bit depth */
    ihdr_data[9]  = 2;     /* colour type: RGB */
    ihdr_data[10] = 0;     /* compression */
    ihdr_data[11] = 0;     /* filter */
    ihdr_data[12] = 0;     /* interlace */

    if (!write_png_chunk(fp, "IHDR", ihdr_data, 13u)) {
        fclose(fp); return 0;
    }

    /* --- Build raw filtered rows:
     *   Each row: [0x00 filter byte] [R,G,B × width]
     *   Total: (width * 3 + 1) * height
     * --- */
    raw_size = (size_t)(w * 3 + 1) * (size_t)h;
    raw_rows = (uint8_t *)malloc(raw_size);
    if (raw_rows == NULL) { fclose(fp); return 0; }

    for (y = 0; y < h; y++) {
        size_t row_start = (size_t)y * (size_t)(w * 3 + 1);
        int    x;

        raw_rows[row_start] = 0x00;  /* filter: None */

        for (x = 0; x < w; x++) {
            size_t src_idx = (size_t)(y * w + x) * 3u;
            raw_rows[row_start + 1u + (size_t)x * 3u + 0u] =
                rgb[src_idx + 0u];
            raw_rows[row_start + 1u + (size_t)x * 3u + 1u] =
                rgb[src_idx + 1u];
            raw_rows[row_start + 1u + (size_t)x * 3u + 2u] =
                rgb[src_idx + 2u];
        }
    }

    /* --- Deflate with zlib --- */
    comp_bound = compressBound((uLong)raw_size);
    compressed = (uint8_t *)malloc(comp_bound);
    if (compressed == NULL) {
        free(raw_rows); fclose(fp); return 0;
    }

    {
        uLongf dest_len = (uLongf)comp_bound;
        int    zret;

        zret = compress2(compressed, &dest_len,
                         raw_rows, (uLong)raw_size,
                         Z_DEFAULT_COMPRESSION);
        if (zret != Z_OK) {
            free(compressed); free(raw_rows);
            fclose(fp); return 0;
        }

        ret = write_png_chunk(fp, "IDAT", compressed,
                              (uint32_t)dest_len);
    }

    free(compressed);
    free(raw_rows);

    if (!ret) { fclose(fp); return 0; }

    /* --- IEND chunk --- */
    if (!write_png_chunk(fp, "IEND", NULL, 0u)) {
        fclose(fp); return 0;
    }

    fclose(fp);
    return 1;
}

/* ==========================================================================
 * Output format detection
 * ========================================================================== */

static int ends_with(const char *str, const char *suffix)
{
    size_t slen = strlen(str);
    size_t xlen = strlen(suffix);
    if (slen < xlen) return 0;
    return (strcmp(str + slen - xlen, suffix) == 0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(int argc, char **argv)
{
    const char *input_path  = NULL;
    const char *output_path = NULL;
    int         i;

    int         width       = 0;
    int         height      = 0;
    int         req_w       = 0;
    int         req_h       = 0;
    uint8_t    *qoi_data    = NULL;
    size_t      qoi_size    = 0;
    uint16_t   *pixels      = NULL;
    size_t      pixel_count;
    uint8_t    *rgb888      = NULL;
    int         ret         = EXIT_FAILURE;

    /* --- parse arguments --- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-o") == 0 && (i + 1 < argc)) {
            output_path = argv[++i];
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

    if (output_path == NULL) {
        fprintf(stderr, "Error: no output file specified (-o).\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* --- load input --- */
    if (ends_with(input_path, ".h")) {
        /* C header file — parse it */
        if (parse_qoi_header_file(input_path,
                                  &width, &height,
                                  &qoi_data, &qoi_size) != 0) {
            goto done;
        }
        printf("Parsed '%s': %d×%d, %zu bytes QOI data.\n",
               input_path, width, height, qoi_size);

        if ((width <= 0) || (height <= 0)) {
            fprintf(stderr,
                    "Warning: dimensions not found in header; "
                    "using pixel count from QOI stream.\n");
        }
    } else {
        /* Binary QOI stream — read directly */
        FILE *fp;
        long  fsize;

        fp = fopen(input_path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "Error: cannot open '%s'.\n", input_path);
            goto done;
        }

        fseek(fp, 0L, SEEK_END);
        fsize = ftell(fp);
        fseek(fp, 0L, SEEK_SET);

        if (fsize <= 0L) {
            fprintf(stderr, "Error: '%s' is empty.\n", input_path);
            fclose(fp);
            goto done;
        }

        qoi_size = (size_t)fsize;
        qoi_data = (uint8_t *)malloc(qoi_size);
        if (qoi_data == NULL) {
            fprintf(stderr, "Error: memory allocation failed.\n");
            fclose(fp);
            goto done;
        }

        if (fread(qoi_data, 1u, qoi_size, fp) != qoi_size) {
            fprintf(stderr, "Error: failed to read '%s'.\n", input_path);
            fclose(fp);
            goto done;
        }
        fclose(fp);

        printf("Read '%s': %zu bytes.\n", input_path, qoi_size);
    }

    /* --- auto-detect video binary container vs raw QOI stream ---
     *
     * A raw QOI stream starts with magic "q565".
     * A video container starts with uint32_t frame_count (LE),
     * followed by uint32_t offsets[N+1].
     *
     * If the first 4 bytes are NOT the QOI magic, try to interpret
     * them as a frame_count and extract the first frame.
     */
    if (qoi_size >= 12u) {
        uint32_t first_word;

        first_word = (uint32_t)qoi_data[0]
                   | ((uint32_t)qoi_data[1] <<  8)
                   | ((uint32_t)qoi_data[2] << 16)
                   | ((uint32_t)qoi_data[3] << 24);

        if ((first_word != 0x35363571u)  /* "q565" in LE: 71 35 36 35 */
            && (first_word > 0u)
            && (first_word < 1000000u)) {
            /*
             * Looks like a video container.  Extract the first frame.
             */
            uint32_t frame_count = first_word;
            uint32_t offset0, offset1;

            if (4u + (frame_count + 1u) * 4u <= qoi_size) {
                offset0 = (uint32_t)qoi_data[4]
                        | ((uint32_t)qoi_data[5]  <<  8)
                        | ((uint32_t)qoi_data[6]  << 16)
                        | ((uint32_t)qoi_data[7]  << 24);

                offset1 = (uint32_t)qoi_data[8]
                        | ((uint32_t)qoi_data[9]  <<  8)
                        | ((uint32_t)qoi_data[10] << 16)
                        | ((uint32_t)qoi_data[11] << 24);

                if ((offset0 > 0u) && (offset1 > offset0)
                    && (offset1 <= qoi_size)) {
                    size_t frame_size = (size_t)(offset1 - offset0);

                    printf("Detected video container: %u frame(s), "
                           "extracting frame 0 (%zu bytes).\n",
                           frame_count, frame_size);

                    /* move frame data to start of buffer */
                    memmove(qoi_data,
                            qoi_data + (size_t)offset0,
                            frame_size);
                    qoi_size = frame_size;
                }
            }
        }
    }

    /* --- determine pixel count from QOI header --- */
    if (qoi_size < 8u) {
        fprintf(stderr, "Error: input too small for QOI header.\n");
        goto done;
    }

    if ((qoi_data[0] != 'q') || (qoi_data[1] != '5') ||
        (qoi_data[2] != '6') || (qoi_data[3] != '5')) {
        fprintf(stderr, "Error: not a valid QOI-for-RGB565 stream "
                "(bad magic).\n");
        goto done;
    }

    {
        uint32_t pc;

        pc = (uint32_t)qoi_data[4]
           | ((uint32_t)qoi_data[5] <<  8)
           | ((uint32_t)qoi_data[6] << 16)
           | ((uint32_t)qoi_data[7] << 24);

        pixel_count = (size_t)pc;
    }

    printf("QOI stream: %zu pixels.\n", pixel_count);

    if (pixel_count == 0u) {
        fprintf(stderr, "Error: zero pixels in stream.\n");
        goto done;
    }

    /* --- apply command-line dimension overrides --- */
    if (req_w > 0) width  = req_w;
    if (req_h > 0) height = req_h;

    /* --- validate / infer dimensions --- */
    if ((width <= 0) || (height <= 0)) {
        /*
         * No dimensions from .h header or command line.
         * Try square root heuristic; otherwise assume 1-row strip.
         */
        fprintf(stderr,
                "Warning: no dimensions specified.  "
                "Use -w and -h to set image size.\n"
                "  Assuming %zu×1 image.\n", pixel_count);
        width  = (int)pixel_count;
        height = 1;
    } else if ((size_t)(width * height) != pixel_count) {
        fprintf(stderr,
                "Error: dimensions %d×%d (%d pixels) do not match "
                "stream pixel count %zu.\n",
                width, height, width * height, pixel_count);
        goto done;
    }

    /* --- decompress --- */
    pixels = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (pixels == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        goto done;
    }

    {
        size_t decoded;

        decoded = rgb565_qoi_decompress(qoi_data, qoi_size,
                                        pixels, pixel_count);
        if (decoded != pixel_count) {
            fprintf(stderr,
                    "Error: decompression failed "
                    "(got %zu, expected %zu).\n",
                    decoded, pixel_count);
            goto done;
        }
    }

    printf("Decompressed %zu pixels successfully.\n", pixel_count);

    /* --- convert RGB565 → RGB888 (3 bytes per pixel, interleaved) --- */
    rgb888 = (uint8_t *)malloc(pixel_count * 3u);
    if (rgb888 == NULL) {
        fprintf(stderr, "Error: memory allocation failed.\n");
        goto done;
    }

    {
        size_t j;

        for (j = 0u; j < pixel_count; j++) {
            rgb565_to_rgb888(pixels[j],
                             &rgb888[j * 3u + 0u],
                             &rgb888[j * 3u + 1u],
                             &rgb888[j * 3u + 2u]);
        }
    }

    /* --- write output --- */
    {
        int ok = 0;

        if (ends_with(output_path, ".bmp")) {
            ok = write_bmp(output_path, width, height, rgb888);
        } else if (ends_with(output_path, ".tga")) {
            ok = write_tga(output_path, width, height, rgb888);
        } else if (ends_with(output_path, ".ppm")) {
            ok = write_ppm(output_path, width, height, rgb888);
        } else if (ends_with(output_path, ".png")) {
            ok = write_png(output_path, width, height, rgb888);
#ifdef HAS_STB_IMAGE_WRITE
        } else if (ends_with(output_path, ".jpg")
                   || ends_with(output_path, ".jpeg")) {
            ok = stbi_write_jpg(
                output_path, width, height, 3, rgb888, 95);
#endif
        } else {
#ifndef HAS_STB_IMAGE_WRITE
            if (ends_with(output_path, ".jpg")
                || ends_with(output_path, ".jpeg")) {
                fprintf(stderr,
                        "Error: JPEG output requires "
                        "stb_image_write.h.\n"
                        "  Use .png, .bmp, .tga, or .ppm instead, "
                        "or rebuild with stb_image_write.h.\n");
                goto done;
            }
#endif
            fprintf(stderr,
                    "Error: unsupported output format '%s'.\n"
                    "  Supported: .png .bmp .tga .ppm"
#ifdef HAS_STB_IMAGE_WRITE
                    " .jpg"
#endif
                    "\n",
                    output_path);
            goto done;
        }

        if (!ok) {
            fprintf(stderr, "Error: failed to write '%s'.\n",
                    output_path);
            goto done;
        }
    }

    printf("Wrote '%s' (%d×%d, %zu pixels, RGB).\n",
           output_path, width, height, pixel_count);

    ret = EXIT_SUCCESS;

done:
    free(rgb888);
    free(pixels);
    free(qoi_data);

    return ret;
}
