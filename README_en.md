# RGB565 QOI

A platform-independent C library for compressing and decompressing RGB565 pixel data using the Quite OK Image (QOI) algorithm, adapted for the 16-bit RGB565 colour format.

[中文](README.md)

## Features

- **Zero dependencies** — only `<stddef.h>` and `<stdint.h>`, no libc calls
- **Freestanding ready** — works on bare-metal MCUs, MPUs, Linux userspace, and kernel space
- **Endianness-portable** — compressed format is identical regardless of platform byte order
- **Simple API** — three functions, all returning 0 on error
- **QOI algorithm** — fast, lossless compression via index lookup, difference encoding, luma encoding, and run-length encoding

## Compression Format

```
Header:  magic "q565" (4 bytes) + uint32_t pixel_count (little-endian, 4 bytes) = 8 bytes
Body:    QOI chunks (variable length)
Footer:  8-byte end marker {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
```

### Chunk Types

| Byte range | Operation | Description |
|---|---|---|
| `0x00..0x3F` | QOI_OP_INDEX | 6-bit hash index into a 64-entry array of previously seen pixels |
| `0x40..0x7F` | QOI_OP_DIFF | 2-bit signed R, G, B differences from the previous pixel (biased +2, range −2..1) |
| `0x80..0xBF` | QOI_OP_LUMA | 6-bit green difference (biased +32) + 1 byte: 4-bit R−G and B−G deltas (biased +8) |
| `0xC0..0xFD` | QOI_OP_RUN | Repeat previous pixel, run length = `(tag & 0x3F) + 1`, range 1..62 |
| `0xFE` | QOI_OP_RGB565 | Full RGB565 pixel follows as 2 bytes (little-endian) |

Maximum 62 pixels per run. All multi-byte values are little-endian. Channel arithmetic wraps (modulo 32 for R/B, modulo 64 for G).

### Differences from Standard QOI

Standard QOI encodes 8-bit RGBA pixels. This adaptation:

- Uses 5-bit R, 6-bit G, 5-bit B channels (RGB565) instead of 8-bit RGBA
- Omits the RGBA chunk type (`0xFF`) — RGB565 has no alpha channel
- Hash function adapted for 16-bit pixel values: `(r×3 + g×5 + b×7) % 64`
- Difference ranges adapted for 5/6/5-bit channel widths
- Uses magic bytes `q565` instead of `qoif` to identify the format
- Does not include colourspace metadata in the header

## API

```c
// Worst-case compressed buffer size (8 + pixel_count × 3 + 8)
size_t rgb565_qoi_max_compressed_size(size_t pixel_count);

// Compress: returns bytes written, or 0 on error
size_t rgb565_qoi_compress(const uint16_t *pixels, size_t pixel_count,
                           uint8_t *output, size_t output_capacity);

// Decompress: returns pixel count written, or 0 on error
size_t rgb565_qoi_decompress(const uint8_t *input, size_t input_size,
                             uint16_t *pixels, size_t pixel_capacity);
```

All functions return 0 on error (NULL pointers, zero counts, insufficient buffer, bad magic, corrupted/truncated data, integer overflow).

## Building

```bash
mkdir build && cd build
cmake .. -DRGB565_QOI_BUILD_TESTS=ON -DRGB565_QOI_BUILD_EXAMPLES=ON -DRGB565_QOI_BUILD_TOOLS=ON
cmake --build .
ctest --output-on-failure
```

### CMake Options

| Option | Default | Description |
|---|---|---|
| `RGB565_QOI_BUILD_EXAMPLES` | OFF | Build example programs |
| `RGB565_QOI_BUILD_TESTS` | OFF | Build test suite |
| `RGB565_QOI_BUILD_TOOLS` | OFF | Build conversion tools (`img2qoi`, `video2qoi`, `qoi2img`) |

At configure time, CMake auto-downloads `stb_image.h` and `stb_image_write.h` from GitHub (nothings/stb) into the build directory. `stb_image.h` failure is fatal (`img2qoi`/`video2qoi` require it); `stb_image_write.h` failure is a warning (`qoi2img` native PNG output always works, only JPEG needs it).

### Integrating into Your Project

```cmake
add_subdirectory(path/to/rgb565-qoi)
target_link_libraries(your_target PRIVATE rgb565_qoi)
```

Or install system-wide:

```bash
cmake --install build --prefix /usr/local
```

## Example

```c
#include "rgb565_qoi.h"

uint16_t pixels[] = { 0xF800, 0xF800, 0xF800, 0x07E0, 0x001F };

// Allocate worst-case output buffer
size_t max_size = rgb565_qoi_max_compressed_size(5);
uint8_t *compressed = malloc(max_size);

// Compress
size_t comp_size = rgb565_qoi_compress(pixels, 5, compressed, max_size);

// Decompress
uint16_t restored[5];
size_t count = rgb565_qoi_decompress(compressed, comp_size, restored, 5);
```

## Tools

The project includes conversion tools that turn images and videos into QOI-compressed C headers or binary files, and vice versa.

### img2qoi — Image to QOI

Converts PNG, JPEG, BMP, and other common image formats to RGB565 QOI.

```bash
# C header output (default)
./tools/img2qoi input.png -o image.h

# Binary output
./tools/img2qoi input.png -t bin -o image.bin

# Resize during conversion
./tools/img2qoi input.png -w 64 -h 64 -n my_sprite -o sprite.h
```

### video2qoi — Video / Frame Sequence to QOI

Converts image sequences or raw RGB565 frames into per-frame QOI-compressed output.

```bash
# Image sequence mode
./tools/video2qoi frame_0001.png frame_0002.png frame_0003.png -o video.h

# Binary container output
./tools/video2qoi frame_*.png -t bin -o video.bin

# Raw RGB565 frames
./tools/video2qoi --raw frames.bin -w 320 -h 240 -o video.h
```

### qoi2img — QOI to Image

Converts QOI-compressed files (.h or .bin) back to standard image formats.

```bash
# .bin file to PNG (width and height required)
./tools/qoi2img output.qoi -o image.png -w 480 -h 320

# .h file to PNG (dimensions read from #defines)
./tools/qoi2img image.qoi.h -o image.png

# Output to BMP / TGA / PPM / JPEG
./tools/qoi2img output.qoi -o image.bmp -w 64 -h 64
./tools/qoi2img output.qoi -o image.tga -w 64 -h 64
./tools/qoi2img output.qoi -o image.ppm -w 64 -h 64
./tools/qoi2img output.qoi -o image.jpg -w 64 -h 64
```

Native output formats (always available): **PNG** (via zlib), **BMP**, **TGA**, **PPM**. With `stb_image_write.h`: **JPEG**.

## Compression Algorithm

The encoder processes pixels sequentially, maintaining:

1. **Previous pixel** — for computing differences and runs
2. **64-entry seen-pixel array** — indexed by a hash of the pixel value
3. **Run counter** — for accumulating runs of identical pixels

For each pixel, the encoder tries encodings in order of efficiency:

1. **QOI_OP_RUN** — if the pixel matches the previous pixel, accumulate a run (up to 62)
2. **QOI_OP_INDEX** — if the pixel is found in the seen-pixel array at its hash position
3. **QOI_OP_DIFF** — if the per-channel differences from the previous pixel are all in [−2, 1]
4. **QOI_OP_LUMA** — if the green difference is in [−32, 31] and the R−G and B−G deltas are in [−8, 7]
5. **QOI_OP_RGB565** — fallback: emit the full pixel value

## License

MIT
