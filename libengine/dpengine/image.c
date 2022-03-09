/*
 * Copyright (c) 2022 askmeaboutloom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "image.h"
#include "compress.h"
#include "image_png.h"
#include "image_transform.h"
#include <dpcommon/binary.h>
#include <dpcommon/common.h>
#include <dpcommon/conversions.h>
#include <dpcommon/geom.h>
#include <dpcommon/input.h>
#include <dpcommon/output.h>


struct DP_Image {
    int width, height;
    size_t count;
    DP_Pixel pixels[];
};

DP_Image *DP_image_new(int width, int height)
{
    DP_ASSERT(width >= 0);
    DP_ASSERT(height >= 0);
    size_t count = DP_int_to_size(width) * DP_int_to_size(height);
    DP_Image *img = DP_malloc(DP_FLEX_SIZEOF(DP_Image, pixels, count));
    img->width = width;
    img->height = height;
    img->count = count;
    memset(img->pixels, 0, sizeof(*img->pixels) * count);
    return img;
}


static DP_Image *read_image_guess(DP_Input *input)
{
    unsigned char buf[8];
    bool error;
    size_t read = DP_input_read(input, buf, sizeof(buf), &error);
    if (error) {
        return NULL;
    }

    DP_Image *(*read_fn)(DP_Input *);
    if (DP_image_png_guess(buf, read)) {
        read_fn = DP_image_png_read;
    }
    else {
        DP_error_set("Could not guess image file format");
        return NULL;
    }

    if (DP_input_rewind_by(input, read)) {
        return read_fn(input);
    }
    else {
        return NULL;
    }
}

DP_Image *DP_image_new_from_file(DP_Input *input, DP_ImageFileType type)
{
    DP_ASSERT(input);
    switch (type) {
    case DP_IMAGE_FILE_TYPE_GUESS:
        return read_image_guess(input);
    case DP_IMAGE_FILE_TYPE_PNG:
        return DP_image_png_read(input);
    default:
        DP_error_set("Unknown image file type %d", (int)type);
        return NULL;
    }
}


struct DP_ImageInflateArgs {
    int width, height;
    DP_Image *img;
};

static unsigned char *get_output_buffer(size_t out_size, void *user)
{
    struct DP_ImageInflateArgs *args = user;
    int width = args->width;
    int height = args->height;
    size_t expected_size =
        DP_int_to_size(width) * DP_int_to_size(height) * sizeof(uint32_t);
    if (out_size == expected_size) {
        DP_Image *img = DP_image_new(width, height);
        args->img = img;
        return (unsigned char *)img->pixels;
    }
    else {
        DP_error_set("Image decompression needs size %zu, but got %zu",
                     expected_size, out_size);
        return NULL;
    }
}

DP_Image *DP_image_new_from_compressed(int width, int height,
                                       const unsigned char *in, size_t in_size)
{
    struct DP_ImageInflateArgs args = {width, height, NULL};
    if (DP_compress_inflate(in, in_size, get_output_buffer, &args)) {
#if DP_BYTE_ORDER == DP_LITTLE_ENDIAN
        // Nothing else to do here.
#elif DP_BYTE_ORDER == DP_BIG_ENDIAN
        // Gotta byte-swap the pixels.
        for (int i = 0; i < width * height; ++i) {
            args.img->pixels[i] = DP_swap_uint32(args.img->pixels[i]);
        }
#else
#    error "Unknown byte order"
#endif
        return args.img;
    }
    else {
        DP_image_free(args.img);
        return NULL;
    }
}


// Monochrome MSB format: 1 bit per pixel, bytes packed with the most
// significant bit first, lines padded to 32 bit boundaries.

struct DP_MonochromeInflateArgs {
    int line_width;
    int line_count;
    uint8_t *buffer;
};

static unsigned char *get_monochrome_buffer(size_t out_size, void *user)
{
    struct DP_MonochromeInflateArgs *args = user;
    size_t expected_size =
        DP_int_to_size(args->line_width) * DP_int_to_size(args->line_count);
    if (out_size == expected_size) {
        uint8_t *buffer = DP_malloc(out_size);
        args->buffer = buffer;
        return buffer;
    }
    else {
        DP_error_set("Monochrome decompression needs size %zu, but got %zu",
                     expected_size, out_size);
        return NULL;
    }
}

static DP_Image *extract_monochrome(int width, int height, int line_width,
                                    const unsigned char *buffer)
{
    DP_Image *img = DP_image_new(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int byte_index = y * line_width + x / 8;
            int bit_mask = 1 << (7 - (x % 8)); // most significant bit first
            bool white = buffer[byte_index] & bit_mask;
            DP_Pixel pixel = {white ? 0xffffffff : 0x00000000};
            DP_image_pixel_at_set(img, x, y, pixel);
        }
    }
    return img;
}

DP_Image *DP_image_new_from_compressed_monochrome(int width, int height,
                                                  const unsigned char *in,
                                                  size_t in_size)
{
    int line_width = (width + 31) / 32 * 4;
    struct DP_MonochromeInflateArgs args = {line_width, height, NULL};
    DP_Image *img;
    if (DP_compress_inflate(in, in_size, get_monochrome_buffer, &args)) {
        img = extract_monochrome(width, height, line_width, args.buffer);
    }
    else {
        img = NULL;
    }
    DP_free(args.buffer);
    return img;
}


static void copy_pixels(DP_Image *DP_RESTRICT dst, DP_Image *DP_RESTRICT src,
                        int dst_x, int dst_y, int src_x, int src_y,
                        int copy_width, int copy_height)
{
    DP_ASSERT(dst);
    DP_ASSERT(src);
    DP_ASSERT(dst_x >= 0);
    DP_ASSERT(dst_y >= 0);
    DP_ASSERT(src_x >= 0);
    DP_ASSERT(src_y >= 0);
    DP_ASSERT(copy_width >= 0);
    DP_ASSERT(copy_height >= 0);
    DP_ASSERT(dst_x + copy_width <= dst->width);
    DP_ASSERT(src_x + copy_width <= src->width);
    DP_ASSERT(dst_y + copy_height <= dst->height);
    DP_ASSERT(src_y + copy_height <= src->height);
    int dst_width = dst->width;
    int src_width = src->width;
    DP_Pixel *DP_RESTRICT dst_pixels = dst->pixels;
    DP_Pixel *DP_RESTRICT src_pixels = src->pixels;
    size_t row_size = sizeof(uint32_t) * DP_int_to_size(copy_width);
    for (int y = 0; y < copy_height; ++y) {
        int d = (y + dst_y) * dst_width + dst_x;
        int s = (y + src_y) * src_width + src_x;
        memcpy(dst_pixels + d, src_pixels + s, row_size);
    }
}

DP_Image *DP_image_new_subimage(DP_Image *img, int x, int y, int width,
                                int height)
{
    DP_ASSERT(img);
    DP_ASSERT(width >= 0);
    DP_ASSERT(height >= 0);
    DP_Image *sub = DP_image_new(width, height);
    int dst_x = x < 0 ? -x : 0;
    int dst_y = y < 0 ? -y : 0;
    int src_x = x > 0 ? x : 0;
    int src_y = y > 0 ? y : 0;
    int copy_width = DP_min_int(width - dst_x, img->width - src_x);
    int copy_height = DP_min_int(height - dst_y, img->height - src_y);
    copy_pixels(sub, img, dst_x, dst_y, src_x, src_y, copy_width, copy_height);
    return sub;
}


void DP_image_free(DP_Image *img)
{
    DP_free(img);
}

int DP_image_width(DP_Image *img)
{
    DP_ASSERT(img);
    return img->width;
}

int DP_image_height(DP_Image *img)
{
    DP_ASSERT(img);
    return img->height;
}

DP_Pixel *DP_image_pixels(DP_Image *img)
{
    DP_ASSERT(img);
    return img->pixels;
}

DP_Pixel DP_image_pixel_at(DP_Image *img, int x, int y)
{
    DP_ASSERT(img);
    DP_ASSERT(x >= 0);
    DP_ASSERT(y >= 0);
    DP_ASSERT(x < img->width);
    DP_ASSERT(y < img->height);
    return img->pixels[y * img->width + x];
}

void DP_image_pixel_at_set(DP_Image *img, int x, int y, DP_Pixel pixel)
{
    DP_ASSERT(img);
    DP_ASSERT(x >= 0);
    DP_ASSERT(y >= 0);
    DP_ASSERT(x < img->width);
    DP_ASSERT(y < img->height);
    img->pixels[y * img->width + x] = pixel;
}


DP_Image *DP_image_transform(DP_Image *img, DP_DrawContext *dc,
                             const DP_Quad *dst_quad, int *out_offset_x,
                             int *out_offset_y)
{
    DP_ASSERT(img);
    DP_ASSERT(dst_quad);

    int src_width = img->width;
    int src_height = img->height;
    DP_Quad src_quad =
        DP_quad_make(0, 0, src_width, 0, src_width, src_height, 0, src_height);

    DP_Rect dst_bounds = DP_quad_bounds(*dst_quad);
    int dst_bounds_x = DP_rect_x(dst_bounds);
    int dst_bounds_y = DP_rect_y(dst_bounds);
    DP_Quad translated_dst_quad =
        DP_quad_translate(*dst_quad, -dst_bounds_x, -dst_bounds_y);

    DP_MaybeTransform mtf =
        DP_transform_quad_to_quad(src_quad, translated_dst_quad);
    if (!mtf.valid) {
        DP_error_set("Image transform failed");
        return NULL;
    }

    DP_Image *dst_img =
        DP_image_new(DP_rect_width(dst_bounds), DP_rect_height(dst_bounds));
    if (!DP_image_transform_draw(img, dc, dst_img, mtf.tf)) {
        DP_image_free(dst_img);
        return NULL;
    }

    if (out_offset_x) {
        *out_offset_x = dst_bounds_x;
    }
    if (out_offset_y) {
        *out_offset_y = dst_bounds_y;
    }
    return dst_img;
}


bool DP_image_write_png(DP_Image *img, DP_Output *output)
{
    DP_ASSERT(img);
    DP_ASSERT(output);
    return DP_image_png_write(output, img->width, img->height, img->pixels);
}
