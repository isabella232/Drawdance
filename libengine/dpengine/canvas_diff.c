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
#include "canvas_diff.h"
#include "layer_props_list.h"
#include "tile.h"
#include <dpcommon/common.h>
#include <dpcommon/conversions.h>


struct DP_CanvasDiff {
    int count;
    int xtiles, ytiles;
    int tile_changes_reserved;
    bool *tile_changes;
    bool layer_props_changed;
};

DP_CanvasDiff *DP_canvas_diff_new(void)
{
    DP_CanvasDiff *diff = DP_malloc(sizeof(*diff));
    *diff = (DP_CanvasDiff){0, 0, 0, 0, NULL, false};
    return diff;
}

void DP_canvas_diff_free(DP_CanvasDiff *diff)
{
    if (diff) {
        DP_free(diff->tile_changes);
        DP_free(diff);
    }
}


void DP_canvas_diff_begin(DP_CanvasDiff *diff, int old_width, int old_height,
                          int current_width, int current_height,
                          bool layer_props_changed)
{
    DP_ASSERT(diff);
    DP_ASSERT(old_width >= 0);
    DP_ASSERT(old_height >= 0);
    DP_ASSERT(current_width >= 0);
    DP_ASSERT(current_height >= 0);
    int xtiles = DP_tile_size_round_up(current_width);
    int ytiles = DP_tile_size_round_up(current_height);
    int count = xtiles * ytiles;
    diff->count = count;
    diff->xtiles = xtiles;
    diff->ytiles = ytiles;
    if (diff->tile_changes_reserved < count) {
        diff->tile_changes_reserved = count;
        size_t size = DP_int_to_size(count) * sizeof(*diff->tile_changes);
        diff->tile_changes = DP_realloc(diff->tile_changes, size);
    }
    // TODO: Mark only newly added tiles as changed, not all of them.
    bool init = old_width != current_width || old_height != current_height;
    bool *tile_changes = diff->tile_changes;
    for (int i = 0; i < count; ++i) {
        tile_changes[i] = init;
    }
    diff->layer_props_changed = layer_props_changed;
}

void DP_canvas_diff_check(DP_CanvasDiff *diff, DP_CanvasDiffCheckFn fn,
                          void *data)
{
    DP_ASSERT(diff);
    DP_ASSERT(fn);
    int count = diff->count;
    bool *tile_changes = diff->tile_changes;
    for (int i = 0; i < count; ++i) {
        bool *tile_change = &tile_changes[i];
        if (!*tile_change && fn(data, i)) {
            *tile_change = true;
        }
    }
}

void DP_canvas_diff_check_all(DP_CanvasDiff *diff)
{
    DP_ASSERT(diff);
    int count = diff->count;
    bool *tile_changes = diff->tile_changes;
    for (int i = 0; i < count; ++i) {
        tile_changes[i] = true;
    }
}

void DP_canvas_diff_each_index(DP_CanvasDiff *diff, DP_CanvasDiffEachIndexFn fn,
                               void *data)
{
    DP_ASSERT(diff);
    DP_ASSERT(fn);
    int count = diff->count;
    bool *tile_changes = diff->tile_changes;
    for (int i = 0; i < count; ++i) {
        if (tile_changes[i]) {
            fn(data, i);
        }
    }
}


void DP_canvas_diff_each_pos(DP_CanvasDiff *diff, DP_CanvasDiffEachPosFn fn,
                             void *data)
{
    DP_ASSERT(diff);
    DP_ASSERT(fn);
    int xtiles = diff->xtiles;
    int ytiles = diff->ytiles;
    bool *tile_changes = diff->tile_changes;
    for (int y = 0; y < ytiles; ++y) {
        for (int x = 0; x < xtiles; ++x) {
            if (tile_changes[y * xtiles + x]) {
                fn(data, x, y);
            }
        }
    }
}

bool DP_canvas_diff_tiles_changed(DP_CanvasDiff *diff)
{
    DP_ASSERT(diff);
    int count = diff->count;
    bool *tile_changes = diff->tile_changes;
    for (int i = 0; i < count; ++i) {
        if (tile_changes[i]) {
            return true;
        }
    }
    return false;
}

bool DP_canvas_diff_layer_props_changed_reset(DP_CanvasDiff *diff)
{
    DP_ASSERT(diff);
    bool layer_props_changed = diff->layer_props_changed;
    diff->layer_props_changed = false;
    return layer_props_changed;
}
