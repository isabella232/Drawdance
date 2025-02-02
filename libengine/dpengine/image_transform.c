/*
 * Copyright (C) 2022 askmeaboutloom
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --------------------------------------------------------------------
 *
 * This code is wholly based on the Qt framework's raster paint engine
 * implementation, using it under the GNU General Public License, version 3.
 * See 3rdparty/licenses/qt/license.GPL3 for details.
 */
#include "image_transform.h"
#include "blend_mode.h"
#include "dpcommon/conversions.h"
#include "draw_context.h"
#include "image.h"
#include "pixels.h"
#include <dpcommon/common.h>
#include <dpcommon/geom.h>
#include <qgrayraster_inc.h>


struct DP_RenderSpansData {
    int src_width, src_height;
    DP_Pixel *src_pixels;
    int dst_width, dst_height;
    DP_Pixel *dst_pixels;
    DP_Transform tf;
    DP_Pixel *buffer;
};


static void fetch_transformed_bilinear_pixel_bounds(int l1, int l2, int v1,
                                                    int *out_v1, int *out_v2)
{
    if (v1 < l1) {
        *out_v1 = l1;
        *out_v2 = l1;
    }
    else if (v1 >= l2) {
        *out_v1 = l2;
        *out_v2 = l2;
    }
    else {
        *out_v1 = v1;
        *out_v2 = v1 + 1;
    }
}

static uint32_t interpolate_pixel(uint32_t x, uint32_t a, uint32_t y,
                                  uint32_t b)
{
    uint32_t t = (x & 0xff00ff) * a + (y & 0xff00ff) * b;
    t >>= 8;
    t &= 0xff00ff;
    x = ((x >> 8) & 0xff00ff) * a + ((y >> 8) & 0xff00ff) * b;
    x &= 0xff00ff00;
    x |= t;
    return x;
}

static inline uint32_t interpolate_4_pixels(uint32_t tl, uint32_t tr,
                                            uint32_t bl, uint32_t br,
                                            uint32_t distx, uint32_t disty)
{
    uint32_t idistx = 256 - distx;
    uint32_t idisty = 256 - disty;
    uint32_t xtop = interpolate_pixel(tl, idistx, tr, distx);
    uint32_t xbot = interpolate_pixel(bl, idistx, br, distx);
    return interpolate_pixel(xtop, idisty, xbot, disty);
}

static DP_Pixel *fetch_transformed_bilinear(int width, int height,
                                            DP_Pixel *pixels, DP_Transform tf,
                                            int x, int y, int length,
                                            DP_Pixel *out_buffer)
{
    double *m = tf.matrix;
    double fdx = m[0];
    double fdy = m[1];
    double fdw = m[2];
    double cx = DP_int_to_double(x) + 0.5;
    double cy = DP_int_to_double(y) + 0.5;
    double fx = m[3] * cy + m[0] * cx + m[6];
    double fy = m[4] * cy + m[1] * cx + m[7];
    double fw = m[5] * cy + m[2] * cx + m[8];
    DP_Pixel *end = out_buffer + length;
    DP_Pixel *b = out_buffer;

    while (b < end) {
        double iw = fw == 0.0 ? 1.0 : 1.0 / fw;
        double px = fx * iw - 0.5;
        double py = fy * iw - 0.5;

        int x1 = DP_double_to_int(px) - (px < 0 ? 1 : 0);
        int y1 = DP_double_to_int(py) - (py < 0 ? 1 : 0);

        uint32_t distx =
            DP_double_to_uint32((px - DP_int_to_double(x1)) * 256.0);
        uint32_t disty =
            DP_double_to_uint32((py - DP_int_to_double(y1)) * 256.0);

        int x2, y2;
        fetch_transformed_bilinear_pixel_bounds(0, width - 1, x1, &x1, &x2);
        fetch_transformed_bilinear_pixel_bounds(0, height - 1, y1, &y1, &y2);

        DP_Pixel *s1 = pixels + y1 * width;
        DP_Pixel *s2 = pixels + y2 * width;
        b->color =
            interpolate_4_pixels(s1[x1].color, s1[x2].color, s2[x1].color,
                                 s2[x2].color, distx, disty);

        fx += fdx;
        fy += fdy;
        fw += fdw;
        // Force increment to avoid division by zero.
        if (fw == 0.0) {
            fw += fdw;
        }
        ++b;
    }

    return out_buffer;
}

static void process_span(int len, int coverage, DP_Pixel *src, DP_Pixel *dst)
{
    DP_pixels_composite(dst, src, len, DP_int_to_uint8(coverage),
                        DP_BLEND_MODE_NORMAL);
}

static void render_spans(int count, const QT_FT_Span *spans, void *user)
{
    struct DP_RenderSpansData *rsd = user;
    int src_width = rsd->src_width;
    int src_height = rsd->src_height;
    DP_Pixel *src_pixels = rsd->src_pixels;
    int dst_width = rsd->dst_width;
    DP_Pixel *dst_pixels = rsd->dst_pixels;
    DP_Pixel *buffer = rsd->buffer;

    int coverage = 0;
    while (count) {
        if (!spans->len) {
            ++spans;
            --count;
            continue;
        }
        int x = spans->x;
        int y = spans->y;
        int right = x + spans->len;

        // compute length of adjacent spans
        for (int i = 1; i < count && spans[i].y == y && spans[i].x == right;
             ++i) {
            right += spans[i].len;
        }
        int length = right - x;

        while (length) {
            int l = DP_min_int(length, DP_DRAW_CONTEXT_TRANSFORM_BUFFER_SIZE);
            length -= l;

            DP_Pixel *dst = dst_pixels + y * dst_width + x;
            DP_Pixel *src = fetch_transformed_bilinear(
                src_width, src_height, src_pixels, rsd->tf, x, y, l, buffer);
            int offset = 0;
            while (l > 0) {
                if (x == spans->x) { // new span?
                    coverage = (spans->coverage * 256) >> 8;
                }

                int pr = spans->x + spans->len;
                int pl = DP_min_int(l, pr - x);
                process_span(pl, coverage, src + offset, dst + offset);

                l -= pl;
                x += pl;
                offset += pl;

                if (x == pr) { // done with current span?
                    ++spans;
                    --count;
                }
            }
        }
    }
}


static QT_FT_Vector transform_outline_point(DP_Transform tf, double x, double y)
{
    DP_Vec2 v = DP_transform_xy(tf, x, y);
    return (QT_FT_Vector){DP_double_to_int(v.x * 64.0 + 0.5),
                          DP_double_to_int(v.y * 64.0 + 0.5)};
}

bool DP_image_transform_draw(DP_Image *img, DP_DrawContext *dc,
                             DP_Image *dst_img, DP_Transform tf)
{
    DP_Transform delta = DP_transform_make(1.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                                           1.0 / 65536.0, 1.0 / 65536.0, 1.0);
    DP_MaybeTransform mtf = DP_transform_invert(DP_transform_mul(delta, tf));
    if (!mtf.valid) {
        DP_error_set("Failed to invert fill transform matrix");
        return false;
    }

    QT_FT_Raster gray_raster;
    if (qt_ft_grays_raster.raster_new(&gray_raster) != 0) {
        DP_error_set("Failed to initialize transform rasterer");
        return false;
    }

    int src_width = DP_image_width(img);
    int src_height = DP_image_height(img);
    int dst_width = DP_image_width(dst_img);
    int dst_height = DP_image_height(dst_img);
    struct DP_RenderSpansData rsd = {src_width,
                                     src_height,
                                     DP_image_pixels(img),
                                     dst_width,
                                     dst_height,
                                     DP_image_pixels(dst_img),
                                     DP_transform_transpose(mtf.tf),
                                     DP_draw_context_transform_buffer(dc)};

    QT_FT_Vector points[5];
    double w = DP_int_to_double(src_width);
    double h = DP_int_to_double(src_height);
    points[0] = transform_outline_point(tf, 0.0, 0.0);
    points[1] = transform_outline_point(tf, w, 0.0);
    points[2] = transform_outline_point(tf, w, h);
    points[3] = transform_outline_point(tf, 0.0, h);
    points[4] = points[0];

    char tags[5] = {QT_FT_CURVE_TAG_ON, QT_FT_CURVE_TAG_ON, QT_FT_CURVE_TAG_ON,
                    QT_FT_CURVE_TAG_ON, QT_FT_CURVE_TAG_ON};
    int contours[1] = {4};
    QT_FT_Outline outline = {1, 5, points, tags, contours, 0};
    QT_FT_BBox clip_box = {0, 0, dst_width, dst_height};

    size_t raster_pool_size;
    unsigned char *raster_pool =
        DP_draw_context_raster_pool(dc, &raster_pool_size);
    // Qt makes sure to align the buffer address here. I don't think we need to
    // do that, since we always allocate with malloc, which is guaranteed to
    // return something with maximum alignment, while Qt uses a stack buffer.

    qt_ft_grays_raster.raster_reset(gray_raster, raster_pool, raster_pool_size);

    QT_FT_Raster_Params params = {0};
    params.source = &outline;
    params.flags = QT_FT_RASTER_FLAG_CLIP;
    params.user = &rsd;
    params.clip_box = clip_box;

    bool done = false;
    int rendered_spans = 0;

    while (!done) {
        params.flags |= (QT_FT_RASTER_FLAG_AA | QT_FT_RASTER_FLAG_DIRECT);
        params.gray_spans = render_spans;
        params.skip_spans = rendered_spans;
        int error = qt_ft_grays_raster.raster_render(gray_raster, &params);

        if (error == ErrRaster_OutOfMemory) {
            // Try again with more memory, skipping already rendered spans.
            raster_pool_size *= 2;
            if (raster_pool_size > DP_DRAW_CONTEXT_RASTER_POOL_MAX_SIZE) {
                DP_error_set("Failed to rasterize transformed image");
                break;
            }

            rendered_spans += q_gray_rendered_spans(gray_raster);

            raster_pool =
                DP_draw_context_raster_pool_resize(dc, raster_pool_size);

            qt_ft_grays_raster.raster_done(gray_raster);
            if (qt_ft_grays_raster.raster_new(&gray_raster) != 0) {
                DP_error_set("Failed to reinitialize transform rasterer");
                break;
            }
            qt_ft_grays_raster.raster_reset(gray_raster, raster_pool,
                                            raster_pool_size);
        }
        else {
            qt_ft_grays_raster.raster_done(gray_raster);
            done = true;
        }
    }

    return done;
}
