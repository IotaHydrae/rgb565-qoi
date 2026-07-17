# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Configure (from repo root)
mkdir build && cd build
cmake .. -DRGB565_QOI_BUILD_TESTS=ON -DRGB565_QOI_BUILD_EXAMPLES=ON -DRGB565_QOI_BUILD_TOOLS=ON

# Build
cmake --build .

# Run all tests
ctest --output-on-failure

# Run a single test with verbose output
./tests/test_rgb565_qoi

# Run examples
./examples/encode_example
./examples/decode_example [input.qoi]

# Tools: image → QOI
./tools/img2qoi input.png -o output.h           # C header
./tools/img2qoi input.png -t bin -o output.bin  # raw binary

# Tools: QOI → image
./tools/qoi2img output.qoi -o image.png -w 480 -h 320   # .bin input (needs -w -h)
./tools/qoi2img output.h -o image.png                    # .h input (reads dimensions from #defines)

# Tools: video / raw frames → QOI
./tools/video2qoi frame_*.png -o video.h
./tools/video2qoi --raw frames.bin -w 320 -h 240 -o video.h
```

CMake options:

| Option | Default | Description |
|---|---|---|
| `RGB565_QOI_BUILD_EXAMPLES` | OFF | Example programs (`encode_example`, `decode_example`) |
| `RGB565_QOI_BUILD_TESTS` | OFF | Test suite (`test_rgb565_qoi`, registered with CTest) |
| `RGB565_QOI_BUILD_TOOLS` | OFF | Conversion tools (`img2qoi`, `video2qoi`, `qoi2img`) |

At configure time, CMake auto-downloads `stb_image.h` and `stb_image_write.h` from GitHub (nothings/stb) into `${CMAKE_CURRENT_BINARY_DIR}/tools/`. If the download fails, `stb_image.h` is a fatal error (required by `img2qoi`/`video2qoi`); `stb_image_write.h` is a soft warning (`qoi2img` JPEG output needs it, but PNG/BMP/TGA/PPM work natively).

## Project Structure

```
├── include/rgb565_qoi.h    — Public API (freestanding C99, only <stddef.h> + <stdint.h>)
├── src/rgb565_qoi.c        — Library implementation (zero deps, no malloc)
├── examples/               — encode_example, decode_example
├── tests/                  — test_rgb565_qoi (34 tests, inline harness)
├── tools/
│   ├── qoi_utils.{h,c}     — Shared: write C header (.h), write binary, print size comparison
│   ├── img2qoi.c           — PNG/JPEG/BMP → RGB565 QOI  (stb_image.h)
│   ├── video2qoi.c         — Image sequence / raw frames → per-frame QOI (stb_image.h)
│   └── qoi2img.c           — QOI → PNG/BMP/TGA/PPM/JPEG  (zlib for PNG, stb_image_write.h for JPEG)
└── CLAUDE.md
```

The library (`src/rgb565_qoi.c`) depends on **nothing but `<stddef.h>` and `<stdint.h>`** — no libc calls, no dynamic allocation, no platform APIs. It compiles in freestanding environments (bare-metal MCUs, kernel space). The examples, tests, and tools are regular hosted C programs that link the same library.

## Wire Format (adapted from QOI for RGB565)

```
Header:  magic "q565" (4 bytes) + uint32_t pixel_count (LE, 4 bytes) = 8 bytes
Body:    QOI chunks (variable length)
End:     8-byte marker {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
```

Total worst-case size: `8 + pixel_count × 3 + 8` bytes. Use `rgb565_qoi_max_compressed_size()` to size the output buffer.

### Chunk Types

| Byte range | Chunk | Encoding |
|---|---|---|
| `0x00..0x3F` | QOI_OP_INDEX | 6-bit hash index into `uint16_t index[64]` |
| `0x40..0x7F` | QOI_OP_DIFF | 2-bit dr, dg, db (each biased +2, range −2..1) |
| `0x80..0xBF` | QOI_OP_LUMA | 6-bit dg (biased +32) + 1 byte: 4-bit dr−dg, 4-bit db−dg (each biased +8) |
| `0xC0..0xFD` | QOI_OP_RUN | Run length = `(tag & 0x3F) + 1`, range 1..62 |
| `0xFE` | QOI_OP_RGB565 | Full RGB565 pixel follows as 2 bytes (LE) |
| `0xFF` | (reserved) | Rejected by decoder |

### Key Differences from Standard QOI

- Standard QOI encodes 8-bit RGBA pixels; this library works with 16-bit RGB565 (5-bit R, 6-bit G, 5-bit B, no alpha).
- Standard QOI header is 14 bytes (magic "qoif" + width/height BE + channels + colorspace); this uses an 8-byte header (magic "q565" + pixel_count LE). All multi-byte header values are **little-endian**.
- `0xFE` emits a 2-byte RGB565 pixel (vs 3 bytes for standard QOI_OP_RGB).
- There is no QOI_OP_RGBA equivalent — `0xFF` is reserved.
- Channel arithmetic wraps modulo 32 for R/B and modulo 64 for G (standard QOI wraps modulo 256).
- Hash function: `(r×3 + g×5 + b×7) % 64` (no alpha term).

## API (`include/rgb565_qoi.h`)

All three functions return 0 on error (NULL pointers, zero counts, insufficient buffers, bad magic, corrupted/truncated data, integer overflow).

```c
// Worst-case output buffer size: 8 + pixel_count * 3 + 8
size_t rgb565_qoi_max_compressed_size(size_t pixel_count);

// Returns bytes written, or 0 on error
size_t rgb565_qoi_compress(const uint16_t *pixels, size_t pixel_count,
                           uint8_t *output, size_t output_capacity);

// Returns pixel count written, or 0 on error
size_t rgb565_qoi_decompress(const uint8_t *input, size_t input_size,
                             uint16_t *pixels, size_t pixel_capacity);
```

## Encoder / Decoder Architecture

The encoder and decoder follow the reference [phoboslab/qoi](https://github.com/phoboslab/qoi) algorithm structure:

**Encoder** (`rgb565_qoi_compress`):
1. Zero-initialise `uint16_t index[64]`; set `prev = 0x0000`, `run = 0`.
2. For each pixel, if `px == prev` accumulate run (flush at 62). Otherwise flush any pending run, then try encoding in order: INDEX (hash match) → DIFF (dr, dg, db in −2..1) → LUMA (dg in −32..31, dr−dg and db−dg in −8..7) → RGB565 (fallback).
3. After the loop, append 8-byte end marker.

**Decoder** (`rgb565_qoi_decompress`):
1. Validate magic "q565", read `pixel_count`, compute `chunks_len = input_size - 8` (excluding end marker).
2. Initialise `index[64]`, `px = 0x0000`, `run = 0`.
3. Loop: if `run > 0` decrement it; otherwise read a chunk byte, dispatch on tag (8-bit tag `0xFE` checked before 2-bit masks), decode pixel, update `index[hash(px)] = px`.
4. Output each pixel. Return `px_pos` when done.

Channel difference encoding (`qoi_diff5` / `qoi_diff6` in the encoder) computes the shortest modular distance explicitly via bit masking and signed conversion — necessary because 5/6-bit channels don't naturally wrap with C's `unsigned char` arithmetic like 8-bit channels do.

## Tools Binary Formats

**img2qoi `.bin`**: Raw QOI stream (`q565` magic + pixel_count + chunks + end marker). Single-frame.

**video2qoi `.bin`**: Container — `[uint32_t frame_count (LE)] [uint32_t offsets[N+1] (LE)] [frame 0 QOI stream] [frame 1 QOI stream] ...`. Each frame's QOI stream is the standard format.

**video2qoi `.h`**: C header with `#define` dimensions, `_FRAME_OFFSETS[]` table, and concatenated `_FRAMES[]` array containing raw QOI streams per frame.

**qoi2img** auto-detects the video binary container (first 4 bytes ≠ `q565` magic but looks like a small frame_count) and extracts the first frame. `.h` files provide width/height via `#define`; `.bin` files require `-w`/`-h` on the command line.

## Testing

One test executable registered with CTest:

| Test | Coverage |
|---|---|
| `test_rgb565_qoi` | 34 tests: max size (zero/single/typical/overflow), parameter validation (NULL, zero count, capacity, bad magic), round-trip (single pixel, identical, different, solid, zeroes, alternating, gradient, colour extremes, repeating patterns, 64-distinct-then-repeat), edge cases (62/63/128 run lengths), corrupted data (truncated body, wrong pixel count, capacity exceeded, reserved tag 0xFF, truncated RGB565/LUMA), 64K stress, compression sanity |

Uses an inline test harness (no external test framework). Macros: `TEST(name)`, `RUN_TEST(name)`, `ASSERT_TRUE(cond)`, `ASSERT_EQ(a,b)`, `ASSERT_U16_EQ(a,b)`.
