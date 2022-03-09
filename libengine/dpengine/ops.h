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
 * This code is based on Drawpile, using it under the GNU General Public
 * License, version 3. See 3rdparty/licenses/drawpile/COPYING for details.
 */
#ifndef DPENGINE_OPS_H
#define DPENGINE_OPS_H
#include <dpcommon/common.h>

typedef struct DP_CanvasState DP_CanvasState;
typedef struct DP_DrawContext DP_DrawContext;
typedef struct DP_Image DP_Image;
typedef struct DP_PaintDrawDabsParams DP_PaintDrawDabsParams;
typedef struct DP_Quad DP_Quad;
typedef struct DP_Rect DP_Rect;
typedef struct DP_Tile DP_Tile;


DP_CanvasState *DP_ops_canvas_resize(DP_CanvasState *cs,
                                     unsigned int context_id, int top,
                                     int right, int bottom, int left);

DP_CanvasState *DP_ops_layer_create(DP_CanvasState *cs, int layer_id,
                                    int source_id, DP_Tile *tile, bool insert,
                                    bool copy, const char *title,
                                    size_t title_length);

DP_CanvasState *DP_ops_layer_attr(DP_CanvasState *cs, int layer_id,
                                  int sublayer_id, uint8_t opacity,
                                  int blend_mode, bool censored, bool fixed);

DP_CanvasState *DP_ops_layer_reorder(DP_CanvasState *cs, int layer_id_count,
                                     int (*get_layer_id)(void *, int),
                                     void *user);

DP_CanvasState *DP_ops_layer_retitle(DP_CanvasState *cs, int layer_id,
                                     const char *title, size_t title_length);

DP_CanvasState *DP_ops_layer_delete(DP_CanvasState *cs, unsigned int context_id,
                                    int layer_id, bool merge);

DP_CanvasState *DP_ops_layer_visibility(DP_CanvasState *cs, int layer_id,
                                        bool visible);

DP_CanvasState *DP_ops_put_image(DP_CanvasState *cs, unsigned int context_id,
                                 int layer_id, int blend_mode, int x, int y,
                                 int width, int height,
                                 const unsigned char *image, size_t image_size);

DP_CanvasState *DP_ops_region_move(DP_CanvasState *cs, DP_DrawContext *dc,
                                   unsigned int context_id, int layer_id,
                                   const DP_Rect *src_rect,
                                   const DP_Quad *dst_quad, DP_Image *mask);

DP_CanvasState *DP_ops_fill_rect(DP_CanvasState *cs, unsigned int context_id,
                                 int layer_id, int blend_mode, int left,
                                 int top, int right, int bottom,
                                 uint32_t color);

DP_CanvasState *DP_ops_put_tile(DP_CanvasState *cs, DP_Tile *tile, int layer_id,
                                int sublayer_id, int x, int y, int repeat);

DP_CanvasState *DP_ops_pen_up(DP_CanvasState *cs, unsigned int context_id);

DP_CanvasState *DP_ops_draw_dabs(DP_CanvasState *cs, int layer_id,
                                 int sublayer_id, int sublayer_blend_mode,
                                 int sublayer_opacity,
                                 DP_PaintDrawDabsParams *params);


#endif
