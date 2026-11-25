/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_png.h"
#include <xxfclib/algo/deflate/xx_deflate.h>
#include <xxfclib/rt/xx_rt.h>
#include <stdint.h>

static uint32_t png_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static unsigned char png_paeth(unsigned char a, unsigned char b, unsigned char c)
{
    int p = (int)a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Reverses the five PNG row filters in place; rows are 1 + stride bytes. */
static int png_unfilter(unsigned char *raw, unsigned int height, size_t stride, unsigned int pixel)
{
    unsigned int y;
    size_t x;
    for (y = 0; y < height; ++y) {
        unsigned char *row = raw + (size_t)y * (stride + 1), *line = row + 1;
        const unsigned char *previous = y ? raw + (size_t)(y - 1) * (stride + 1) + 1 : NULL;
        for (x = 0; x < stride; ++x) {
            unsigned char left = x >= pixel ? line[x - pixel] : 0, up = previous ? previous[x] : 0;
            unsigned char corner = previous && x >= pixel ? previous[x - pixel] : 0;
            switch (row[0]) {
                case 0: break;
                case 1: line[x] = (unsigned char)(line[x] + left); break;
                case 2: line[x] = (unsigned char)(line[x] + up); break;
                case 3: line[x] = (unsigned char)(line[x] + ((left + up) >> 1)); break;
                case 4: line[x] = (unsigned char)(line[x] + png_paeth(left, up, corner)); break;
                default: return 0;
            }
        }
    }
    return 1;
}

int gui_png_decode_rgba(const unsigned char *data, size_t size, unsigned char **rgba, unsigned int *width, unsigned int *height)
{
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    unsigned char *compressed = NULL, *raw = NULL, *out = NULL;
    size_t offset = 8, compressed_size = 0, raw_size, written = 0, stride, i;
    uint32_t w = 0, h = 0;
    unsigned int pixel = 0, x, y;
    int ok = 0;
    if (!data || !rgba || !width || !height) return 0;
    *rgba = NULL;
    if (size < 8 + 25 || xx_rt_memcmp(data, signature, 8)) return 0;
    /* First pass: header and total image data size. */
    while (offset + 12 <= size) {
        uint32_t length = png_u32(data + offset);
        const unsigned char *type = data + offset + 4;
        if (length > size - offset - 12) return 0;
        if (!xx_rt_memcmp(type, "IHDR", 4)) {
            if (length != 13) return 0;
            w = png_u32(data + offset + 8);
            h = png_u32(data + offset + 12);
            /* 8-bit depth, RGB (2) or RGBA (6), deflate, adaptive filters, no interlace. */
            if (data[offset + 16] != 8 || (data[offset + 17] != 2 && data[offset + 17] != 6) || data[offset + 18] || data[offset + 19] || data[offset + 20]) return 0;
            pixel = data[offset + 17] == 6 ? 4 : 3;
        } else if (!xx_rt_memcmp(type, "IDAT", 4)) compressed_size += length;
        else if (!xx_rt_memcmp(type, "IEND", 4)) break;
        offset += 12 + (size_t)length;
    }
    if (!pixel || !w || !h || w > 4096 || h > 4096 || !compressed_size) return 0;
    stride = (size_t)w * pixel;
    raw_size = (size_t)h * (stride + 1);
    compressed = xx_rt_malloc(compressed_size);
    raw = xx_rt_malloc(raw_size);
    out = xx_rt_malloc((size_t)w * h * 4);
    if (!compressed || !raw || !out) goto done;
    /* Second pass: the IDAT chunks form one zlib stream. */
    for (offset = 8, i = 0; offset + 12 <= size;) {
        uint32_t length = png_u32(data + offset);
        if (!xx_rt_memcmp(data + offset + 4, "IDAT", 4)) {
            xx_rt_memcpy(compressed + i, data + offset + 8, length);
            i += length;
        } else if (!xx_rt_memcmp(data + offset + 4, "IEND", 4)) break;
        offset += 12 + (size_t)length;
    }
    if (!xx_zlib_stream_decode_memory(compressed, compressed_size, raw, raw_size, &written) || written != raw_size || !png_unfilter(raw, h, stride, pixel)) goto done;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            const unsigned char *src = raw + (size_t)y * (stride + 1) + 1 + (size_t)x * pixel;
            unsigned char *dst = out + ((size_t)y * w + x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = pixel == 4 ? src[3] : 255;
        }
    *rgba = out;
    *width = w;
    *height = h;
    out = NULL;
    ok = 1;
done:
    xx_rt_free(compressed);
    xx_rt_free(raw);
    xx_rt_free(out);
    return ok;
}
