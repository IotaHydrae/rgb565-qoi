/*
 * test_rgb565_qoi — Unit tests for the RGB565 QOI library
 *
 * Uses a minimal inline test harness (no external framework).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rgb565_qoi.h"

/* ==========================================================================
 * Minimal test harness
 * ========================================================================== */

static int   g_tests_run  = 0;
static int   g_tests_pass = 0;
static int   g_tests_fail = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void test_##name(void)

#define RUN_TEST(name) do {                          \
    g_tests_run++;                                   \
    printf("  [RUN ] %s\n", #name);                  \
    test_##name();                                   \
    printf("  [PASS] %s\n", #name);                  \
    g_tests_pass++;                                  \
} while (0)

#define ASSERT_TRUE(cond) do {                       \
    if (!(cond)) {                                   \
        printf("  [FAIL] %s:%d: ASSERT_TRUE(%s)\n",  \
               __FILE__, __LINE__, #cond);           \
        g_tests_fail++;                              \
        return;                                      \
    }                                                \
} while (0)

#define ASSERT_EQ(a, b) do {                         \
    if ((a) != (b)) {                                \
        printf("  [FAIL] %s:%d: ASSERT_EQ(%s, %s) "  \
               "(%zu vs %zu)\n",                     \
               __FILE__, __LINE__, #a, #b,           \
               (size_t)(a), (size_t)(b));            \
        g_tests_fail++;                              \
        return;                                      \
    }                                                \
} while (0)

#define ASSERT_U16_EQ(a, b) do {                     \
    if ((a) != (b)) {                                \
        printf("  [FAIL] %s:%d: "                    \
               "ASSERT_U16_EQ(%s, %s) "              \
               "(0x%04X vs 0x%04X)\n",               \
               __FILE__, __LINE__, #a, #b,           \
               (unsigned int)(a),                    \
               (unsigned int)(b));                   \
        g_tests_fail++;                              \
        return;                                      \
    }                                                \
} while (0)

/* ==========================================================================
 * Helper: round-trip pixels through compress -> decompress
 * ========================================================================== */

static int round_trip(const uint16_t *pixels, size_t pixel_count)
{
    size_t   max_size;
    uint8_t *compressed;
    size_t   comp_size;
    uint16_t *decompressed;
    size_t   decomp_count;
    size_t   i;
    int      ok = 1;

    max_size = rgb565_qoi_max_compressed_size(pixel_count);
    if (max_size == 0u) {
        return 0;
    }

    compressed = (uint8_t *)malloc(max_size);
    if (compressed == NULL) {
        return 0;
    }

    comp_size = rgb565_qoi_compress(pixels, pixel_count,
                                    compressed, max_size);
    if (comp_size == 0u || comp_size > max_size) {
        free(compressed);
        return 0;
    }

    decompressed = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (decompressed == NULL) {
        free(compressed);
        return 0;
    }

    decomp_count = rgb565_qoi_decompress(compressed, comp_size,
                                         decompressed, pixel_count);
    if (decomp_count != pixel_count) {
        free(decompressed);
        free(compressed);
        return 0;
    }

    for (i = 0u; i < pixel_count; i++) {
        if (decompressed[i] != pixels[i]) {
            ok = 0;
            break;
        }
    }

    free(decompressed);
    free(compressed);
    return ok;
}

/* ==========================================================================
 * Test cases
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * max_compressed_size
 * -------------------------------------------------------------------------- */

TEST(max_size_zero_pixels)
{
    ASSERT_EQ(rgb565_qoi_max_compressed_size(0u), 0u);
}

TEST(max_size_single_pixel)
{
    /* 8 header + 1 * 3 + 8 padding = 19 */
    ASSERT_EQ(rgb565_qoi_max_compressed_size(1u), 19u);
}

TEST(max_size_typical)
{
    /* 8 + 256 * 3 + 8 = 784 */
    ASSERT_EQ(rgb565_qoi_max_compressed_size(256u), 784u);
}

TEST(max_size_overflow)
{
    /* SIZE_MAX / 3 is the threshold */
    ASSERT_EQ(rgb565_qoi_max_compressed_size(SIZE_MAX), 0u);
}

/* --------------------------------------------------------------------------
 * Compress / decompress — parameter validation
 * -------------------------------------------------------------------------- */

TEST(compress_null_pixels)
{
    uint8_t buf[64];
    ASSERT_EQ(rgb565_qoi_compress(NULL, 16u, buf, sizeof(buf)), 0u);
}

TEST(compress_null_output)
{
    uint16_t pixels[16];
    memset(pixels, 0, sizeof(pixels));
    ASSERT_EQ(rgb565_qoi_compress(pixels, 16u, NULL, 64u), 0u);
}

TEST(compress_zero_count)
{
    uint8_t buf[64];
    ASSERT_EQ(rgb565_qoi_compress(NULL, 0u, buf, sizeof(buf)), 0u);
}

TEST(compress_insufficient_capacity)
{
    uint16_t pixels[16];
    uint8_t  buf[3];  /* way too small */
    memset(pixels, 0, sizeof(pixels));
    ASSERT_EQ(rgb565_qoi_compress(pixels, 16u, buf, sizeof(buf)), 0u);
}

TEST(decompress_null_input)
{
    uint16_t pixels[16];
    ASSERT_EQ(rgb565_qoi_decompress(NULL, 64u, pixels, 16u), 0u);
}

TEST(decompress_null_output)
{
    uint8_t buf[64];
    ASSERT_EQ(rgb565_qoi_decompress(buf, sizeof(buf), NULL, 16u), 0u);
}

TEST(decompress_too_small_input)
{
    uint8_t  buf[2];  /* smaller than header */
    uint16_t pixels[16];
    ASSERT_EQ(rgb565_qoi_decompress(buf, sizeof(buf), pixels, 16u), 0u);
}

TEST(decompress_bad_magic)
{
    /* Construct a stream with invalid magic bytes */
    uint8_t  comp[] = {
        'B', 'A', 'D', '!',     /* bad magic */
        1u, 0u, 0u, 0u,         /* pixel_count = 1 */
        0xFEu, 0x00u, 0xF8u     /* QOI_OP_RGB565: red */
    };
    uint16_t pixels[16];

    ASSERT_EQ(rgb565_qoi_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

/* --------------------------------------------------------------------------
 * Round-trip: basic patterns
 * -------------------------------------------------------------------------- */

TEST(roundtrip_single_pixel)
{
    uint16_t pixels[] = { 0xF800u };  /* pure red */
    ASSERT_TRUE(round_trip(pixels, 1u));
}

TEST(roundtrip_two_identical)
{
    uint16_t pixels[] = { 0x07E0u, 0x07E0u };  /* both pure green */
    ASSERT_TRUE(round_trip(pixels, 2u));
}

TEST(roundtrip_two_different)
{
    uint16_t pixels[] = { 0xF800u, 0x001Fu };  /* red, blue */
    ASSERT_TRUE(round_trip(pixels, 2u));
}

TEST(roundtrip_solid_color)
{
    uint16_t pixels[256];
    size_t i;

    for (i = 0u; i < 256u; i++) {
        pixels[i] = 0xFFFFu;  /* white */
    }
    ASSERT_TRUE(round_trip(pixels, 256u));

    /* Solid colour should compress very well via runs */
    {
        size_t   max_sz = rgb565_qoi_max_compressed_size(256u);
        uint8_t *comp   = (uint8_t *)malloc(max_sz);
        size_t   sz     = rgb565_qoi_compress(pixels, 256u, comp, max_sz);

        ASSERT_TRUE(sz > 0u);
        /*
         * 256 identical pixels:
         * 256 / 62 = 4 runs of 62 + 1 run of 8 = 5 runs.
         * Header (8) + 5 * 1 byte + padding (8) = 21 bytes.
         */
        ASSERT_TRUE(sz <= 22u);
        free(comp);
    }
}

TEST(roundtrip_all_zeroes)
{
    /*
     * All-zero pixels (black).  This exercises the corner case where
     * the initial prev pixel (0x0000) matches the first input pixel,
     * requiring the encoder to accumulate a run from the start.
     */
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = 0x0000u;
    }
    ASSERT_TRUE(round_trip(pixels, 128u));
}

TEST(roundtrip_alternating)
{
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = (i & 1u) ? 0xF800u : 0x001Fu;  /* red, blue... */
    }
    ASSERT_TRUE(round_trip(pixels, 128u));
}

TEST(roundtrip_gradient)
{
    uint16_t pixels[256];
    size_t i;

    for (i = 0u; i < 256u; i++) {
        uint16_t r = (uint16_t)((i * 31u) / 256u) & 0x1Fu;
        uint16_t g = (uint16_t)((i * 63u) / 256u) & 0x3Fu;
        uint16_t b = (uint16_t)((i * 31u) / 256u) & 0x1Fu;
        pixels[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
    ASSERT_TRUE(round_trip(pixels, 256u));
}

/* --------------------------------------------------------------------------
 * Round-trip: edge cases around max run length (62)
 * -------------------------------------------------------------------------- */

TEST(roundtrip_run_exactly_62)
{
    uint16_t pixels[62];
    size_t i;

    for (i = 0u; i < 62u; i++) {
        pixels[i] = 0xAAAAu;
    }
    ASSERT_TRUE(round_trip(pixels, 62u));
}

TEST(roundtrip_run_63_same)
{
    uint16_t pixels[63];
    size_t i;

    for (i = 0u; i < 63u; i++) {
        pixels[i] = 0x5555u;
    }
    ASSERT_TRUE(round_trip(pixels, 63u));
}

TEST(roundtrip_run_128_same)
{
    /* 128 identical — should produce 2 runs of 62 + 1 run of 4 */
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = 0x1234u;
    }
    ASSERT_TRUE(round_trip(pixels, 128u));
}

/* --------------------------------------------------------------------------
 * Round-trip: RGB565 colour space coverage
 * -------------------------------------------------------------------------- */

TEST(roundtrip_all_channel_max)
{
    /* Exercise all channel bits */
    uint16_t pixels[] = {
        0x0000u,  /* pure black */
        0xFFFFu,  /* pure white */
        0xF800u,  /* max red, no green, no blue */
        0x07E0u,  /* no red, max green, no blue */
        0x001Fu,  /* no red, no green, max blue */
        0xF81Fu,  /* max red, no green, max blue (magenta) */
        0xFFE0u,  /* max red, max green, no blue (yellow) */
        0x07FFu,  /* no red, max green, max blue (cyan) */
        0x8410u,  /* mid grey */
    };
    ASSERT_TRUE(round_trip(pixels, 9u));
}

/* --------------------------------------------------------------------------
 * Round-trip: index lookup exercise
 * -------------------------------------------------------------------------- */

TEST(roundtrip_repeating_pattern)
{
    /*
     * A pattern that repeats every 4 pixels to exercise the
     * 64-entry index lookup (QOI_OP_INDEX).
     */
    uint16_t pattern[] = { 0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu };
    uint16_t pixels[256];
    size_t   i;

    for (i = 0u; i < 256u; i++) {
        pixels[i] = pattern[i & 3u];
    }
    ASSERT_TRUE(round_trip(pixels, 256u));
}

TEST(roundtrip_64_distinct_then_repeat)
{
    /*
     * 64 distinct values (each with a unique colour), then repeat them.
     * The first 64 populate the index; the next 64 should all hit INDEX.
     */
    uint16_t pixels[128];
    size_t   i;

    for (i = 0u; i < 64u; i++) {
        pixels[i] = (uint16_t)((i << 5) & 0xFFFFu);
    }
    for (i = 64u; i < 128u; i++) {
        pixels[i] = pixels[i - 64u];
    }
    ASSERT_TRUE(round_trip(pixels, 128u));
}

/* --------------------------------------------------------------------------
 * Decompress: corrupted / truncated data
 * -------------------------------------------------------------------------- */

TEST(decompress_truncated_body)
{
    uint16_t pixels_in[]  = { 0x1234u, 0x5678u, 0x9ABCu };
    size_t   max_sz       = rgb565_qoi_max_compressed_size(3u);
    uint8_t *comp         = (uint8_t *)malloc(max_sz);
    size_t   comp_sz;
    uint16_t pixels_out[16];

    comp_sz = rgb565_qoi_compress(pixels_in, 3u, comp, max_sz);
    ASSERT_TRUE(comp_sz > 0u);

    /*
     * Truncate to just the header plus a few bytes — not enough
     * to decode all 3 pixels.  The minimum valid size is
     * HEADER_SIZE (8) + PADDING_SIZE (8) = 16 bytes.
     */
    ASSERT_EQ(rgb565_qoi_decompress(comp, 12u,
                                    pixels_out, 16u), 0u);

    free(comp);
}

TEST(decompress_wrong_pixel_count_in_header)
{
    uint8_t  comp[] = {
        'q', '5', '6', '5',   /* magic */
        10u, 0u, 0u, 0u,      /* header: 10 pixels */
        0xC0u                  /* run of 1 — only 1 pixel provided */
    };
    uint16_t pixels[16];

    /*
     * Stream declares 10 pixels but contains only 1 — should fail.
     */
    ASSERT_EQ(rgb565_qoi_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

TEST(decompress_pixel_count_exceeds_capacity)
{
    uint8_t comp[] = {
        'q', '5', '6', '5',   /* magic */
        100u, 0u, 0u, 0u,     /* header: 100 pixels */
        0xFEu, 0x00u, 0x00u   /* QOI_OP_RGB565, not enough data for 100 */
    };
    uint16_t pixels[16];    /* capacity only 16, but header says 100 */

    ASSERT_EQ(rgb565_qoi_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

TEST(decompress_reserved_tag_ff)
{
    uint8_t  comp[] = {
        'q', '5', '6', '5',   /* magic */
        1u, 0u, 0u, 0u,       /* header: 1 pixel */
        0xFFu                  /* reserved tag byte */
    };
    uint16_t pixels[16];

    ASSERT_EQ(rgb565_qoi_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

TEST(decompress_truncated_rgb565)
{
    uint8_t  comp[] = {
        'q', '5', '6', '5',   /* magic */
        1u, 0u, 0u, 0u,       /* header: 1 pixel */
        0xFEu, 0x00u           /* QOI_OP_RGB565 but only 1 data byte */
    };
    uint16_t pixels[16];

    ASSERT_EQ(rgb565_qoi_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

TEST(decompress_truncated_luma)
{
    uint8_t  comp[] = {
        'q', '5', '6', '5',   /* magic */
        2u, 0u, 0u, 0u,       /* header: 2 pixels */
        0xFEu, 0x00u, 0xF8u,  /* QOI_OP_RGB565: red (0xF800) */
        0x80u                   /* QOI_OP_LUMA but missing luma byte */
    };
    uint16_t pixels[16];

    ASSERT_EQ(rgb565_qoi_decompress(comp, sizeof(comp), pixels, 16u), 0u);
}

/* --------------------------------------------------------------------------
 * Round-trip: large data set (stress test)
 * -------------------------------------------------------------------------- */

TEST(roundtrip_large)
{
    /*
     * 64 K pixels — exercises the code path with many runs and
     * all encoding types.
     */
    size_t   count = 65536u;
    uint16_t *pixels;
    size_t   i;
    int      ok;

    pixels = (uint16_t *)malloc(count * sizeof(uint16_t));
    ASSERT_TRUE(pixels != NULL);

    for (i = 0u; i < count; i++) {
        if ((i & 0x3Fu) == 0u) {
            /* every 64 pixels: a repeating value */
            pixels[i] = (uint16_t)((i >> 6) & 0xFFFFu);
        } else if ((i & 0x07u) == 0u) {
            /* some identical runs */
            pixels[i] = pixels[i - 1u];
        } else {
            /* otherwise: gradient-like values */
            pixels[i] = (uint16_t)(i & 0xFFFFu);
        }
    }

    ok = round_trip(pixels, count);
    free(pixels);
    ASSERT_TRUE(ok);
}

/* --------------------------------------------------------------------------
 * Compression ratio sanity checks
 * -------------------------------------------------------------------------- */

TEST(compression_solid_is_compact)
{
    /*
     * 1024 identical pixels should compress to < 30 bytes
     * (header 8 + ~17 runs of 62 + final run)
     */
    uint16_t pixels[1024];
    size_t   max_sz;
    uint8_t *comp;
    size_t   sz;
    size_t   i;

    for (i = 0u; i < 1024u; i++) {
        pixels[i] = 0xF800u;  /* all red */
    }

    max_sz = rgb565_qoi_max_compressed_size(1024u);
    comp   = (uint8_t *)malloc(max_sz);
    ASSERT_TRUE(comp != NULL);

    sz = rgb565_qoi_compress(pixels, 1024u, comp, max_sz);
    ASSERT_TRUE(sz > 0u);
    /*
     * 1024 / 62 = 16.5 runs → 17 runs.
     * Header (8) + 17 + padding (8) = 33 bytes.
     */
    ASSERT_TRUE(sz <= 34u);

    free(comp);
}

TEST(compression_never_exceeds_max)
{
    /*
     * Verify that the compressed size never exceeds the max function's
     * guarantee, even for worst-case data (random-looking noise).
     */
    uint16_t pixels[256];
    size_t   max_sz;
    uint8_t *comp;
    size_t   sz;
    size_t   i;

    /* Generate pseudo-random looking data */
    for (i = 0u; i < 256u; i++) {
        pixels[i] = (uint16_t)((i * 0x9E37u + 0x1234u) & 0xFFFFu);
    }

    max_sz = rgb565_qoi_max_compressed_size(256u);
    comp   = (uint8_t *)malloc(max_sz);
    ASSERT_TRUE(comp != NULL);

    sz = rgb565_qoi_compress(pixels, 256u, comp, max_sz);
    ASSERT_TRUE(sz > 0u);
    ASSERT_TRUE(sz <= max_sz);
    ASSERT_TRUE(sz <= 256u * 3u + 8u);

    free(comp);
}

/* ==========================================================================
 * Callback API — helpers
 * ========================================================================== */

struct callback_ctx {
    uint16_t *pixels;
    size_t    capacity;
    size_t    written;
};

static void collect_pixels(const uint16_t *pixels, size_t count,
                           uint16_t xs, uint16_t ys,
                           uint16_t xe, uint16_t ye,
                           void *user_data)
{
    struct callback_ctx *ctx = (struct callback_ctx *)user_data;
    size_t i;

    (void)xs; (void)ys; (void)xe; (void)ye;  /* unused in collector */

    for (i = 0u; i < count; i++) {
        if (ctx->written < ctx->capacity) {
            ctx->pixels[ctx->written] = pixels[i];
            ctx->written++;
        }
    }
}

/*
 * Round-trip helper using the callback API: compress → decompress_callback.
 * Collects pixels via callback into an array, then compares with original.
 */
static int round_trip_callback(const uint16_t *pixels, size_t pixel_count)
{
    size_t            max_size;
    uint8_t          *compressed;
    size_t            comp_size;
    uint16_t         *decompressed;
    uint16_t         *acc_buf;
    size_t            acc_capacity;
    struct callback_ctx ctx;
    size_t            total;
    size_t            i;
    int               ok = 1;

    max_size = rgb565_qoi_max_compressed_size(pixel_count);
    if (max_size == 0u) {
        return 0;
    }

    compressed = (uint8_t *)malloc(max_size);
    if (compressed == NULL) {
        return 0;
    }

    comp_size = rgb565_qoi_compress(pixels, pixel_count,
                                    compressed, max_size);
    if (comp_size == 0u || comp_size > max_size) {
        free(compressed);
        return 0;
    }

    decompressed = (uint16_t *)malloc(pixel_count * sizeof(uint16_t));
    if (decompressed == NULL) {
        free(compressed);
        return 0;
    }

    acc_capacity = pixel_count > 65535u ? 65535u : pixel_count;
    acc_buf = (uint16_t *)malloc(acc_capacity * sizeof(uint16_t));
    if (acc_buf == NULL) {
        free(decompressed);
        free(compressed);
        return 0;
    }

    ctx.pixels   = decompressed;
    ctx.capacity = pixel_count;
    ctx.written  = 0u;

    total = rgb565_qoi_decompress_callback(
                compressed, comp_size,
                pixel_count > 65535u ? 65535u : (uint16_t)pixel_count,
                acc_buf, NULL, acc_capacity,
                collect_pixels, &ctx);

    if (total != pixel_count || ctx.written != pixel_count) {
        free(acc_buf);
        free(decompressed);
        free(compressed);
        return 0;
    }

    for (i = 0u; i < pixel_count; i++) {
        if (decompressed[i] != pixels[i]) {
            ok = 0;
            break;
        }
    }

    free(acc_buf);
    free(decompressed);
    free(compressed);
    return ok;
}

/* ==========================================================================
 * Callback API — parameter validation
 * ========================================================================== */

TEST(callback_null_input)
{
    uint16_t pixels[16];
    uint16_t acc_buf[1];
    struct callback_ctx ctx;

    ctx.pixels   = pixels;
    ctx.capacity = 16u;
    ctx.written  = 0u;

    ASSERT_EQ(rgb565_qoi_decompress_callback(NULL, 64u, 16u,
                                              acc_buf, NULL, 1u,
                                              collect_pixels, &ctx), 0u);
}

TEST(callback_null_callback)
{
    uint8_t  buf[64];
    uint16_t acc_buf[1];
    ASSERT_EQ(rgb565_qoi_decompress_callback(buf, sizeof(buf), 16u,
                                              acc_buf, NULL, 1u,
                                              NULL, NULL), 0u);
}

TEST(callback_too_small_input)
{
    uint8_t  buf[2];  /* smaller than header + padding */
    uint16_t acc_buf[1];
    ASSERT_EQ(rgb565_qoi_decompress_callback(buf, sizeof(buf), 16u,
                                              acc_buf, NULL, 1u,
                                              collect_pixels, NULL), 0u);
}

TEST(callback_zero_pixels)
{
    uint8_t  comp[] = {
        'q', '5', '6', '5',   /* magic */
        0u, 0u, 0u, 0u,       /* header: 0 pixels */
        0u, 0u, 0u, 0u,       /* padding */
        0u, 0u, 0u, 0x01u
    };
    uint16_t acc_buf[1];

    ASSERT_EQ(rgb565_qoi_decompress_callback(comp, sizeof(comp), 16u,
                                              acc_buf, NULL, 1u,
                                              collect_pixels, NULL), 0u);
}

TEST(callback_width_zero)
{
    uint8_t comp[] = {
        'q', '5', '6', '5',   /* magic */
        3u, 0u, 0u, 0u,       /* header: 3 pixels */
        0xFEu, 0x34u, 0x12u,  /* QOI_OP_RGB565: 0x1234 */
        0xFEu, 0x78u, 0x56u,  /* QOI_OP_RGB565: 0x5678 */
        0xFEu, 0xBCu, 0x9Au,  /* QOI_OP_RGB565: 0x9ABC */
        0u, 0u, 0u, 0u,       /* padding */
        0u, 0u, 0u, 0x01u
    };
    uint16_t acc_buf[1];

    ASSERT_EQ(rgb565_qoi_decompress_callback(comp, sizeof(comp), 0u,
                                              acc_buf, NULL, 1u,
                                              collect_pixels, NULL), 0u);
}

TEST(callback_buf_null)
{
    uint8_t  buf[64];
    uint16_t pixels[16];
    struct callback_ctx ctx;

    ctx.pixels   = pixels;
    ctx.capacity = 16u;
    ctx.written  = 0u;

    ASSERT_EQ(rgb565_qoi_decompress_callback(buf, sizeof(buf), 16u,
                                              NULL, NULL, 16u,
                                              collect_pixels, &ctx), 0u);
}

TEST(callback_buf_capacity_zero)
{
    uint8_t  buf[64];
    uint16_t acc_buf[16];

    ASSERT_EQ(rgb565_qoi_decompress_callback(buf, sizeof(buf), 16u,
                                              acc_buf, NULL, 0u,
                                              collect_pixels, NULL), 0u);
}

TEST(callback_bad_magic)
{
    uint8_t comp[] = {
        'B', 'A', 'D', '!',   /* bad magic */
        1u, 0u, 0u, 0u,       /* pixel_count = 1 */
        0xFEu, 0x00u, 0xF8u,  /* QOI_OP_RGB565: red */
        0u, 0u, 0u, 0u,       /* padding */
        0u, 0u, 0u, 0x01u
    };
    uint16_t acc_buf[1];

    ASSERT_EQ(rgb565_qoi_decompress_callback(comp, sizeof(comp), 16u,
                                              acc_buf, NULL, 1u,
                                              collect_pixels, NULL), 0u);
}

/* ==========================================================================
 * Callback API — round-trip basics
 * ========================================================================== */

TEST(callback_roundtrip_single_pixel)
{
    uint16_t pixels[] = { 0xF800u };  /* pure red */
    ASSERT_TRUE(round_trip_callback(pixels, 1u));
}

TEST(callback_roundtrip_solid_color)
{
    uint16_t pixels[256];
    size_t i;

    for (i = 0u; i < 256u; i++) {
        pixels[i] = 0xFFFFu;  /* white */
    }
    ASSERT_TRUE(round_trip_callback(pixels, 256u));
}

TEST(callback_roundtrip_all_zeroes)
{
    /*
     * All-zero pixels exercise the corner case where the initial
     * prev pixel (0x0000) matches the first input pixel.
     */
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = 0x0000u;
    }
    ASSERT_TRUE(round_trip_callback(pixels, 128u));
}

TEST(callback_roundtrip_alternating)
{
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = (i & 1u) ? 0xF800u : 0x001Fu;  /* red, blue... */
    }
    ASSERT_TRUE(round_trip_callback(pixels, 128u));
}

TEST(callback_roundtrip_gradient)
{
    uint16_t pixels[256];
    size_t i;

    for (i = 0u; i < 256u; i++) {
        uint16_t r = (uint16_t)((i * 31u) / 256u) & 0x1Fu;
        uint16_t g = (uint16_t)((i * 63u) / 256u) & 0x3Fu;
        uint16_t b = (uint16_t)((i * 31u) / 256u) & 0x1Fu;
        pixels[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
    ASSERT_TRUE(round_trip_callback(pixels, 256u));
}

/* ==========================================================================
 * Callback API — edge cases around max run length (62)
 * ========================================================================== */

TEST(callback_roundtrip_run_exactly_62)
{
    uint16_t pixels[62];
    size_t i;

    for (i = 0u; i < 62u; i++) {
        pixels[i] = 0xAAAAu;
    }
    ASSERT_TRUE(round_trip_callback(pixels, 62u));
}

TEST(callback_roundtrip_run_63_same)
{
    uint16_t pixels[63];
    size_t i;

    for (i = 0u; i < 63u; i++) {
        pixels[i] = 0x5555u;
    }
    ASSERT_TRUE(round_trip_callback(pixels, 63u));
}

TEST(callback_roundtrip_run_128_same)
{
    uint16_t pixels[128];
    size_t i;

    for (i = 0u; i < 128u; i++) {
        pixels[i] = 0x1234u;
    }
    ASSERT_TRUE(round_trip_callback(pixels, 128u));
}

/* ==========================================================================
 * Callback API — corrupted / truncated data
 * ========================================================================== */

TEST(callback_decompress_truncated_body)
{
    uint16_t pixels_in[]  = { 0x1234u, 0x5678u, 0x9ABCu };
    size_t   max_sz       = rgb565_qoi_max_compressed_size(3u);
    uint8_t *comp         = (uint8_t *)malloc(max_sz);
    size_t   comp_sz;
    uint16_t acc_buf[8];

    comp_sz = rgb565_qoi_compress(pixels_in, 3u, comp, max_sz);
    ASSERT_TRUE(comp_sz > 0u);

    /* truncate below minimum valid size (header + padding = 16) */
    ASSERT_EQ(rgb565_qoi_decompress_callback(comp, 12u, 3u,
                                              acc_buf, NULL, 8u,
                                              collect_pixels, NULL), 0u);

    free(comp);
}

TEST(callback_decompress_wrong_pixel_count)
{
    uint8_t  comp[] = {
        'q', '5', '6', '5',   /* magic */
        10u, 0u, 0u, 0u,      /* header: 10 pixels */
        0xC0u,                 /* run of 1 — only 1 pixel provided */
        0u, 0u, 0u, 0u,       /* padding */
        0u, 0u, 0u, 0x01u
    };
    uint16_t pixels[16];
    struct callback_ctx ctx;

    ctx.pixels   = pixels;
    ctx.capacity = 16u;
    ctx.written  = 0u;

    /*
     * Stream declares 10 pixels but contains only 1 — should fail.
     */
    ASSERT_EQ(rgb565_qoi_decompress_callback(comp, sizeof(comp), 10u,
                                              pixels, NULL, 16u,
                                              collect_pixels, &ctx), 0u);
}

/* ==========================================================================
 * Callback API — multi-row splitting
 * ========================================================================== */

struct split_ctx {
    int      call_count;
    uint16_t xs[16];
    uint16_t ys[16];
    uint16_t xe[16];
    uint16_t ye[16];
    size_t   count[16];
};

static void record_splits(const uint16_t *pixels, size_t count,
                          uint16_t xs, uint16_t ys,
                          uint16_t xe, uint16_t ye,
                          void *user_data)
{
    struct split_ctx *ctx = (struct split_ctx *)user_data;
    (void)pixels;

    if (ctx->call_count < 16) {
        ctx->xs[ctx->call_count]    = xs;
        ctx->ys[ctx->call_count]    = ys;
        ctx->xe[ctx->call_count]    = xe;
        ctx->ye[ctx->call_count]    = ye;
        ctx->count[ctx->call_count] = count;
        ctx->call_count++;
    }
}

TEST(callback_multi_row_split)
{
    /*
     * 256 identical white pixels, width = 100, buf_capacity = 100.
     * QOI encoder emits 1 chunk for the first pixel, then runs.
     *
     * With same-row merging (buf_capacity == width):
     *   First pixel (DIFF from black) + repeated runs of white.
     *   Buffer fills every 100 pixels, causing flush at row boundaries.
     *
     * Expected: 3 callbacks (rows 0, 1, and partial row 2)
     */
    uint16_t pixels[256];
    uint16_t acc_buf[100];
    size_t   max_sz;
    uint8_t *comp;
    size_t   comp_sz;
    struct split_ctx ctx;
    size_t   total;
    size_t   i;

    for (i = 0u; i < 256u; i++) {
        pixels[i] = 0xFFFFu;
    }

    max_sz = rgb565_qoi_max_compressed_size(256u);
    ASSERT_TRUE(max_sz > 0u);

    comp = (uint8_t *)malloc(max_sz);
    ASSERT_TRUE(comp != NULL);

    comp_sz = rgb565_qoi_compress(pixels, 256u, comp, max_sz);
    ASSERT_TRUE(comp_sz > 0u);

    ctx.call_count = 0;
    total = rgb565_qoi_decompress_callback(comp, comp_sz, 100u,
                                            acc_buf, NULL, 100u,
                                            record_splits, &ctx);
    ASSERT_EQ(total, 256u);
    /* 3 calls: row 0 (100 px), row 1 (100 px), row 2 (56 px) */
    ASSERT_EQ((size_t)ctx.call_count, 3u);

    /* row 0: buffer full at 100 px */
    ASSERT_U16_EQ(ctx.xs[0], 0u);
    ASSERT_U16_EQ(ctx.ys[0], 0u);
    ASSERT_U16_EQ(ctx.xe[0], 99u);
    ASSERT_U16_EQ(ctx.ye[0], 0u);
    ASSERT_EQ(ctx.count[0], 100u);

    /* row 1: buffer full at 100 px */
    ASSERT_U16_EQ(ctx.xs[1], 0u);
    ASSERT_U16_EQ(ctx.ys[1], 1u);
    ASSERT_U16_EQ(ctx.xe[1], 99u);
    ASSERT_U16_EQ(ctx.ye[1], 1u);
    ASSERT_EQ(ctx.count[1], 100u);

    /* row 2: final flush — 56 px */
    ASSERT_U16_EQ(ctx.xs[2], 0u);
    ASSERT_U16_EQ(ctx.ys[2], 2u);
    ASSERT_U16_EQ(ctx.xe[2], 55u);
    ASSERT_U16_EQ(ctx.ye[2], 2u);
    ASSERT_EQ(ctx.count[2], 56u);

    free(comp);
}

TEST(callback_multi_row_batch)
{
    /*
     * 256 identical white pixels, width = 100, buf_capacity = 200.
     * The buffer holds 2 rows — consecutive rows merge into one
     * callback with a multi-row bounding rectangle.
     *
     * Expected: 2 callbacks
     *   Batch 1: 200 px, (xs=0,ys=0) → (xe=99,ye=1)
     *   Batch 2:  56 px, (xs=0,ys=2) → (xe=55,ye=2)
     */
    uint16_t pixels[256];
    uint16_t acc_buf[200];
    size_t   max_sz;
    uint8_t *comp;
    size_t   comp_sz;
    struct split_ctx ctx;
    size_t   total;
    size_t   i;

    for (i = 0u; i < 256u; i++) {
        pixels[i] = 0xFFFFu;
    }

    max_sz = rgb565_qoi_max_compressed_size(256u);
    ASSERT_TRUE(max_sz > 0u);

    comp = (uint8_t *)malloc(max_sz);
    ASSERT_TRUE(comp != NULL);

    comp_sz = rgb565_qoi_compress(pixels, 256u, comp, max_sz);
    ASSERT_TRUE(comp_sz > 0u);

    ctx.call_count = 0;
    total = rgb565_qoi_decompress_callback(comp, comp_sz, 100u,
                                            acc_buf, NULL, 200u,
                                            record_splits, &ctx);
    ASSERT_EQ(total, 256u);
    ASSERT_EQ((size_t)ctx.call_count, 2u);

    /* batch 1: spans row 0 and row 1 */
    ASSERT_U16_EQ(ctx.xs[0], 0u);
    ASSERT_U16_EQ(ctx.ys[0], 0u);
    ASSERT_U16_EQ(ctx.xe[0], 99u);
    ASSERT_U16_EQ(ctx.ye[0], 1u);
    ASSERT_EQ(ctx.count[0], 200u);

    /* batch 2: final flush, row 2 only */
    ASSERT_U16_EQ(ctx.xs[1], 0u);
    ASSERT_U16_EQ(ctx.ys[1], 2u);
    ASSERT_U16_EQ(ctx.xe[1], 55u);
    ASSERT_U16_EQ(ctx.ye[1], 2u);
    ASSERT_EQ(ctx.count[1], 56u);

    free(comp);
}

/* ==========================================================================
 * Callback API — large data set (stress test)
 * ========================================================================== */

TEST(callback_roundtrip_large)
{
    size_t   count = 65536u;
    uint16_t *pixels;
    size_t   i;
    int      ok;

    pixels = (uint16_t *)malloc(count * sizeof(uint16_t));
    ASSERT_TRUE(pixels != NULL);

    for (i = 0u; i < count; i++) {
        if ((i & 0x3Fu) == 0u) {
            pixels[i] = (uint16_t)((i >> 6) & 0xFFFFu);
        } else if ((i & 0x07u) == 0u) {
            pixels[i] = pixels[i - 1u];
        } else {
            pixels[i] = (uint16_t)(i & 0xFFFFu);
        }
    }

    ok = round_trip_callback(pixels, count);
    free(pixels);
    ASSERT_TRUE(ok);
}

/* ==========================================================================
 * Callback API — ping-pong double buffering
 * ========================================================================== */

struct pp_ctx {
    uint16_t *storage;
    size_t    capacity;
    size_t    written;
    int       call_count;
    uint16_t *last_buf;     /* buffer pointer from previous callback */
    int      toggled_ok;    /* 1 if buffer alternated every call */
};

static void collect_pingpong(const uint16_t *pixels, size_t count,
                              uint16_t xs, uint16_t ys,
                              uint16_t xe, uint16_t ye,
                              void *user_data)
{
    struct pp_ctx *ctx = (struct pp_ctx *)user_data;
    size_t i;

    (void)xs; (void)ys; (void)xe; (void)ye;

    for (i = 0u; i < count; i++) {
        if (ctx->written < ctx->capacity) {
            ctx->storage[ctx->written++] = pixels[i];
        }
    }

    /* verify buffer toggles every call */
    if (ctx->call_count > 0 && pixels == ctx->last_buf) {
        ctx->toggled_ok = 0;  /* same buffer twice — not toggling */
    }
    ctx->last_buf = (uint16_t *)pixels;  /* cast away const for comparison */
    ctx->call_count++;
}

TEST(pingpong_roundtrip)
{
    uint16_t pixels[256];
    size_t   max_sz;
    uint8_t *comp;
    size_t   comp_sz;
    uint16_t buf_a[100];
    uint16_t buf_b[100];
    uint16_t decompressed[256];
    struct pp_ctx ctx;
    size_t   total;
    size_t   i;
    int      ok = 1;

    for (i = 0u; i < 256u; i++) {
        pixels[i] = (uint16_t)(i & 0xFFFFu);
    }

    max_sz = rgb565_qoi_max_compressed_size(256u);
    ASSERT_TRUE(max_sz > 0u);

    comp = (uint8_t *)malloc(max_sz);
    ASSERT_TRUE(comp != NULL);

    comp_sz = rgb565_qoi_compress(pixels, 256u, comp, max_sz);
    ASSERT_TRUE(comp_sz > 0u);

    memset(decompressed, 0, sizeof(decompressed));
    ctx.storage    = decompressed;
    ctx.capacity   = 256u;
    ctx.written    = 0u;
    ctx.call_count = 0;
    ctx.last_buf   = NULL;
    ctx.toggled_ok = 1;

    total = rgb565_qoi_decompress_callback(
                comp, comp_sz, 256u,
                buf_a, buf_b, 100u,
                collect_pingpong, &ctx);

    ASSERT_EQ(total, 256u);
    ASSERT_EQ(ctx.written, 256u);
    /* should get at least 3 callbacks (256 px / 100 capacity = 3 flushes) */
    ASSERT_TRUE(ctx.call_count >= 3);
    /* buffer must have toggled every time */
    ASSERT_TRUE(ctx.toggled_ok);

    for (i = 0u; i < 256u; i++) {
        if (decompressed[i] != pixels[i]) {
            ok = 0;
            break;
        }
    }
    ASSERT_TRUE(ok);

    free(comp);
}

TEST(pingpong_null_params)
{
    uint16_t buf_a[16];
    uint16_t buf_b[16];
    uint8_t  input[64];

    memset(input, 0, sizeof(input));  /* header: 0 pixels → immediate return */

    ASSERT_EQ(rgb565_qoi_decompress_callback(
                  NULL, 64u, 16u, buf_a, buf_b, 16u,
                  collect_pingpong, NULL), 0u);
    ASSERT_EQ(rgb565_qoi_decompress_callback(
                  input, sizeof(input), 16u, NULL, buf_b, 16u,
                  collect_pingpong, NULL), 0u);
    ASSERT_EQ(rgb565_qoi_decompress_callback(
                  input, sizeof(input), 16u, buf_a, NULL, 16u,
                  collect_pingpong, NULL), 0u);
    ASSERT_EQ(rgb565_qoi_decompress_callback(
                  input, sizeof(input), 0u, buf_a, buf_b, 16u,
                  collect_pingpong, NULL), 0u);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  RGB565 QOI Library — Test Suite\n");
    printf("========================================\n\n");

    /* --- max_compressed_size --- */
    printf("--- max_compressed_size ---\n");
    RUN_TEST(max_size_zero_pixels);
    RUN_TEST(max_size_single_pixel);
    RUN_TEST(max_size_typical);
    RUN_TEST(max_size_overflow);

    /* --- parameter validation --- */
    printf("\n--- parameter validation ---\n");
    RUN_TEST(compress_null_pixels);
    RUN_TEST(compress_null_output);
    RUN_TEST(compress_zero_count);
    RUN_TEST(compress_insufficient_capacity);
    RUN_TEST(decompress_null_input);
    RUN_TEST(decompress_null_output);
    RUN_TEST(decompress_too_small_input);
    RUN_TEST(decompress_bad_magic);

    /* --- round-trip basics --- */
    printf("\n--- round-trip basics ---\n");
    RUN_TEST(roundtrip_single_pixel);
    RUN_TEST(roundtrip_two_identical);
    RUN_TEST(roundtrip_two_different);
    RUN_TEST(roundtrip_solid_color);
    RUN_TEST(roundtrip_all_zeroes);
    RUN_TEST(roundtrip_alternating);
    RUN_TEST(roundtrip_gradient);

    /* --- colour space coverage --- */
    printf("\n--- colour space coverage ---\n");
    RUN_TEST(roundtrip_all_channel_max);

    /* --- index lookup exercise --- */
    printf("\n--- index lookup exercise ---\n");
    RUN_TEST(roundtrip_repeating_pattern);
    RUN_TEST(roundtrip_64_distinct_then_repeat);

    /* --- edge cases (max run length) --- */
    printf("\n--- edge cases (max run length) ---\n");
    RUN_TEST(roundtrip_run_exactly_62);
    RUN_TEST(roundtrip_run_63_same);
    RUN_TEST(roundtrip_run_128_same);

    /* --- corrupted / truncated data --- */
    printf("\n--- corrupted / truncated data ---\n");
    RUN_TEST(decompress_truncated_body);
    RUN_TEST(decompress_wrong_pixel_count_in_header);
    RUN_TEST(decompress_pixel_count_exceeds_capacity);
    RUN_TEST(decompress_reserved_tag_ff);
    RUN_TEST(decompress_truncated_rgb565);
    RUN_TEST(decompress_truncated_luma);

    /* --- stress test --- */
    printf("\n--- stress test ---\n");
    RUN_TEST(roundtrip_large);

    /* --- compression ratio sanity --- */
    printf("\n--- compression ratio sanity ---\n");
    RUN_TEST(compression_solid_is_compact);
    RUN_TEST(compression_never_exceeds_max);

    /* --- callback API: parameter validation --- */
    printf("\n--- callback API: parameter validation ---\n");
    RUN_TEST(callback_null_input);
    RUN_TEST(callback_null_callback);
    RUN_TEST(callback_too_small_input);
    RUN_TEST(callback_zero_pixels);
    RUN_TEST(callback_width_zero);
    RUN_TEST(callback_buf_null);
    RUN_TEST(callback_buf_capacity_zero);
    RUN_TEST(callback_bad_magic);

    /* --- callback API: round-trip basics --- */
    printf("\n--- callback API: round-trip basics ---\n");
    RUN_TEST(callback_roundtrip_single_pixel);
    RUN_TEST(callback_roundtrip_solid_color);
    RUN_TEST(callback_roundtrip_all_zeroes);
    RUN_TEST(callback_roundtrip_alternating);
    RUN_TEST(callback_roundtrip_gradient);

    /* --- callback API: edge cases --- */
    printf("\n--- callback API: edge cases (max run length) ---\n");
    RUN_TEST(callback_roundtrip_run_exactly_62);
    RUN_TEST(callback_roundtrip_run_63_same);
    RUN_TEST(callback_roundtrip_run_128_same);

    /* --- callback API: corrupted / truncated --- */
    printf("\n--- callback API: corrupted / truncated ---\n");
    RUN_TEST(callback_decompress_truncated_body);
    RUN_TEST(callback_decompress_wrong_pixel_count);

    /* --- callback API: multi-row splitting --- */
    printf("\n--- callback API: multi-row splitting ---\n");
    RUN_TEST(callback_multi_row_split);
    RUN_TEST(callback_multi_row_batch);

    /* --- callback API: stress --- */
    printf("\n--- callback API: stress test ---\n");
    RUN_TEST(callback_roundtrip_large);

    /* --- ping-pong double buffering --- */
    printf("\n--- ping-pong double buffering ---\n");
    RUN_TEST(pingpong_roundtrip);
    RUN_TEST(pingpong_null_params);

    /* --- summary --- */
    printf("\n========================================\n");
    printf("  Results:  %d / %d passed", g_tests_pass, g_tests_run);
    if (g_tests_fail > 0) {
        printf(",  %d FAILED", g_tests_fail);
    }
    printf("\n========================================\n\n");

    return (g_tests_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
