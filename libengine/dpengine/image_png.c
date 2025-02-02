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
#include "image_png.h"
#include "image.h"
#include "pixels.h"
#include <dpcommon/common.h>
#include <dpcommon/conversions.h>
#include <dpcommon/input.h>
#include <dpcommon/output.h>
#include <png.h>
#include <setjmp.h>
// IWYU pragma: no_include <pngconf.h>


bool DP_image_png_guess(unsigned char *buf, size_t size)
{
    return png_sig_cmp(buf, 0, size) == 0;
}


static void error_png(png_structp png_ptr, png_const_charp error_msg)
{
    DP_error_set("PNG error: %s", error_msg);
    png_longjmp(png_ptr, 1);
}

static void warn_png(DP_UNUSED png_structp png_ptr, png_const_charp warning_msg)
{
    DP_warn("PNG warning: %s", warning_msg);
}

static png_voidp malloc_png(DP_UNUSED png_structp png_ptr,
                            png_alloc_size_t size)
{
    return DP_malloc(size);
}

static void free_png(DP_UNUSED png_structp png_ptr, png_voidp ptr)
{
    DP_free(ptr);
}

static void read_png(png_structp png_ptr, png_bytep data, size_t length)
{
    DP_Input *input = png_get_io_ptr(png_ptr);
    bool error;
    size_t read = DP_input_read(input, data, length, &error);
    if (error) {
        png_longjmp(png_ptr, 1);
    }
    else if (read != length) {
        DP_error_set("PNG wanted %zu bytes, but got %zu", length, read);
        png_longjmp(png_ptr, 1);
    }
}

static void write_png(png_structp png_ptr, png_bytep data, size_t length)
{
    DP_Output *output = png_get_io_ptr(png_ptr);
    if (!DP_output_write(output, data, length)) {
        png_longjmp(png_ptr, 1);
    }
}

static void flush_png(png_structp png_ptr)
{
    DP_Output *output = png_get_io_ptr(png_ptr);
    if (!DP_output_flush(output)) {
        png_longjmp(png_ptr, 1);
    }
}


DP_Image *DP_image_png_read(DP_Input *input)
{
    png_structp png_ptr =
        png_create_read_struct_2(PNG_LIBPNG_VER_STRING, NULL, error_png,
                                 warn_png, NULL, malloc_png, free_png);
    if (!png_ptr) {
        DP_error_set("Can't create PNG read struct");
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        DP_error_set("Can't create PNG read info struct");
        return NULL;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    png_set_read_fn(png_ptr, input, read_png);
    png_set_user_limits(png_ptr, INT16_MAX, INT16_MAX);

    int transforms =
        PNG_TRANSFORM_SCALE_16 | PNG_TRANSFORM_BGR | PNG_TRANSFORM_GRAY_TO_RGB;
    png_read_png(png_ptr, info_ptr, transforms, NULL);

    png_uint_32 width = png_get_image_width(png_ptr, info_ptr);
    png_uint_32 height = png_get_image_height(png_ptr, info_ptr);
    DP_ASSERT(width <= INT16_MAX);
    DP_ASSERT(height <= INT16_MAX);

    size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    size_t expected_rowbytes = width * 4u;
    if (rowbytes != expected_rowbytes) {
        DP_error_set("Expected PNG row length of %zu, but got %zu",
                     expected_rowbytes, rowbytes);
        png_longjmp(png_ptr, 1);
    }

    png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);
    DP_Image *img = DP_image_new((int)width, (int)height);
    DP_Pixel *pixels = DP_image_pixels(img);
    for (png_uint_32 y = 0; y < height; ++y) {
        png_bytep row = row_pointers[y];
        for (png_uint_32 x = 0; x < width; ++x) {
            png_uint_32 offset = x * 4;
            pixels[y * width + x] = (DP_Pixel){
                .b = row[offset],
                .g = row[offset + 1],
                .r = row[offset + 2],
                .a = row[offset + 3],
            };
        }
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return img;
}


bool DP_image_png_write(DP_Output *output, int width, int height,
                        DP_Pixel *pixels)
{
    png_structp png_ptr =
        png_create_write_struct_2(PNG_LIBPNG_VER_STRING, NULL, error_png,
                                  warn_png, NULL, malloc_png, free_png);
    if (!png_ptr) {
        DP_error_set("Can't create PNG write struct");
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        DP_error_set("Can't create PNG write info struct");
        png_destroy_write_struct(&png_ptr, NULL);
        return false;
    }

    size_t stride = DP_int_to_size(width);
    size_t row_count = DP_int_to_size(height);
    png_bytepp row_pointers =
        png_malloc(png_ptr, row_count * sizeof(*row_pointers));
    png_bytep bytes = (png_bytep)pixels;
    for (size_t i = 0; i < row_count; ++i) {
        row_pointers[i] = bytes + i * stride * sizeof(*pixels);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_free(png_ptr, row_pointers);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    png_set_write_fn(png_ptr, output, write_png, flush_png);

    png_set_IHDR(png_ptr, info_ptr, DP_int_to_uint32(width),
                 DP_int_to_uint32(height), 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_BGR, NULL);

    png_free(png_ptr, row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return true;
}
