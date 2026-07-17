# RGB565 QOI

一个平台无关的 C 语言库，使用 Quite OK Image (QOI) 算法对 RGB565 像素数据进行压缩和解压，针对 16 位 RGB565 颜色格式做了适配。

[English](README_en.md)

## 特性

- **零依赖** — 仅使用 `<stddef.h>` 和 `<stdint.h>`，不调用任何 libc 函数
- **独立环境可用** — 可在裸机 MCU、MPU、Linux 用户空间及内核空间运行
- **端序无关** — 压缩格式不受平台字节序影响，跨平台一致
- **简洁 API** — 仅三个函数，出错时均返回 0
- **QOI 算法** — 通过索引查找、差值编码、亮度编码和游程编码实现快速无损压缩

## 压缩格式

```
头部:    magic "q565" (4 字节) + uint32_t pixel_count (小端序，4 字节) = 8 字节
数据体:  QOI 数据块（变长）
尾部:    8 字节结束标记 {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}
```

### 数据块类型

| 字节范围 | 操作 | 说明 |
|---|---|---|
| `0x00..0x3F` | QOI_OP_INDEX | 6 位哈希索引，查找 64 个已见像素的数组 |
| `0x40..0x7F` | QOI_OP_DIFF | R、G、B 与前一像素的 2 位有符号差值（偏置 +2，范围 −2..1） |
| `0x80..0xBF` | QOI_OP_LUMA | 6 位绿色差值（偏置 +32）+ 1 字节：4 位 R−G 和 B−G 差值（偏置 +8） |
| `0xC0..0xFD` | QOI_OP_RUN | 重复前一像素，游程 = `(tag & 0x3F) + 1`，范围 1..62 |
| `0xFE` | QOI_OP_RGB565 | 后跟完整 RGB565 像素（2 字节，小端序） |

每个游程最多 62 像素。所有多字节值均使用小端序。通道运算回绕（R/B 模 32，G 模 64）。

### 与标准 QOI 的区别

标准 QOI 编码 8 位 RGB/RGBA 像素。本库做了以下适配：

- 使用 5 位 R、6 位 G、5 位 B 通道（RGB565），替代 8 位 RGBA
- 去掉 RGBA 数据块类型（`0xFF`），因为 RGB565 没有 alpha 通道
- 哈希函数适配 16 位像素值：`(r×3 + g×5 + b×7) % 64`
- 差值范围适配 5/6/5 位通道宽度
- 文件头使用 magic `q565` 替代 `qoif`
- 不含色彩空间元数据

## API

```c
// 计算最坏情况下压缩缓冲区大小 (8 + pixel_count × 3 + 8)
size_t rgb565_qoi_max_compressed_size(size_t pixel_count);

// 压缩：返回写入的字节数，出错返回 0
size_t rgb565_qoi_compress(const uint16_t *pixels, size_t pixel_count,
                           uint8_t *output, size_t output_capacity);

// 解压：返回写入的像素数，出错返回 0
size_t rgb565_qoi_decompress(const uint8_t *input, size_t input_size,
                             uint16_t *pixels, size_t pixel_capacity);
```

所有函数在出错时返回 0（空指针、零计数、缓冲区不足、magic 不匹配、数据损坏/截断、整数溢出）。

## 构建

```bash
mkdir build && cd build
cmake .. -DRGB565_QOI_BUILD_TESTS=ON -DRGB565_QOI_BUILD_EXAMPLES=ON -DRGB565_QOI_BUILD_TOOLS=ON
cmake --build .
ctest --output-on-failure
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `RGB565_QOI_BUILD_EXAMPLES` | OFF | 构建示例程序 |
| `RGB565_QOI_BUILD_TESTS` | OFF | 构建测试套件 |
| `RGB565_QOI_BUILD_TOOLS` | OFF | 构建转换工具 (`img2qoi`、`video2qoi`、`qoi2img`) |

CMake 配置时会自动从 GitHub (nothings/stb) 下载 `stb_image.h` 和 `stb_image_write.h` 到构建目录。`stb_image.h` 下载失败会导致配置终止（`img2qoi`/`video2qoi` 依赖它）；`stb_image_write.h` 下载失败仅为警告（`qoi2img` 的原生 PNG 输出始终可用，JPEG 输出才需要它）。

### 集成到你的项目

```cmake
add_subdirectory(path/to/rgb565-qoi)
target_link_libraries(your_target PRIVATE rgb565_qoi)
```

或者安装到系统目录：

```bash
cmake --install build --prefix /usr/local
```

## 示例

```c
#include "rgb565_qoi.h"

uint16_t pixels[] = { 0xF800, 0xF800, 0xF800, 0x07E0, 0x001F };

// 分配最坏情况输出缓冲区
size_t max_size = rgb565_qoi_max_compressed_size(5);
uint8_t *compressed = malloc(max_size);

// 压缩
size_t comp_size = rgb565_qoi_compress(pixels, 5, compressed, max_size);

// 解压
uint16_t restored[5];
size_t count = rgb565_qoi_decompress(compressed, comp_size, restored, 5);
```

## 工具

项目附带转换工具，可将图片和视频转换为 QOI 压缩的 C 头文件或二进制文件，也支持反向转换。

### img2qoi — 图片转 QOI

将 PNG、JPEG、BMP 等常见图片格式转换为 RGB565 QOI。

```bash
# 输出 C 头文件（默认）
./tools/img2qoi input.png -o image.h

# 输出二进制文件
./tools/img2qoi input.png -t bin -o image.bin

# 转换时缩放
./tools/img2qoi input.png -w 64 -h 64 -n my_sprite -o sprite.h
```

### video2qoi — 视频/帧序列转 QOI

将图片序列或原始 RGB565 帧转换为逐帧 QOI 压缩输出。

```bash
# 图片序列模式
./tools/video2qoi frame_0001.png frame_0002.png frame_0003.png -o video.h

# 二进制容器输出
./tools/video2qoi frame_*.png -t bin -o video.bin

# 原始 RGB565 帧
./tools/video2qoi --raw frames.bin -w 320 -h 240 -o video.h
```

### qoi2img — QOI 转图片

将 QOI 压缩文件（.h 或 .bin）转换回标准图片格式。

```bash
# .bin 文件转 PNG（需指定宽高）
./tools/qoi2img output.qoi -o image.png -w 480 -h 320

# .h 文件转 PNG（宽高从 #define 中读取）
./tools/qoi2img image.qoi.h -o image.png

# 转为 BMP / TGA / PPM / JPEG
./tools/qoi2img output.qoi -o image.bmp -w 64 -h 64
./tools/qoi2img output.qoi -o image.tga -w 64 -h 64
./tools/qoi2img output.qoi -o image.ppm -w 64 -h 64
./tools/qoi2img output.qoi -o image.jpg -w 64 -h 64
```

原生输出格式（始终可用）：**PNG**（基于 zlib）、**BMP**、**TGA**、**PPM**。配置了 `stb_image_write.h` 后额外支持 **JPEG**。

## 压缩算法

编码器顺序处理每个像素，维护以下状态：

1. **前一像素** — 用于计算差值和游程
2. **64 个已见像素数组** — 以像素值的哈希为索引
3. **游程计数器** — 累积连续相同像素

对每个像素，编码器按效率从高到低尝试以下编码：

1. **QOI_OP_RUN** — 若像素与前一像素相同，累积游程（最多 62）
2. **QOI_OP_INDEX** — 若像素在已见数组的哈希位置命中
3. **QOI_OP_DIFF** — 若各通道与前一像素的差值均在 [−2, 1] 范围内
4. **QOI_OP_LUMA** — 若绿色差值在 [−32, 31] 且红绿/蓝绿差值在 [−8, 7] 范围内
5. **QOI_OP_RGB565** — 兜底：直接输出完整像素值

## 许可证

MIT
