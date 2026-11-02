#include "workspace/workspace.h"

BOOL mora_layout_is_valid(mora_layout_config_t layout)
{
    return layout.kind == MORA_LAYOUT_COLUMNS || layout.kind == MORA_LAYOUT_ROWS ||
        layout.kind == MORA_LAYOUT_MAIN;
}

static BOOL split_axis(int64_t length, size_t index, size_t count, int64_t* start, int64_t* end)
{
    int64_t n = (int64_t)count;

    if (n > length) {
        return FALSE;
    }

    int64_t size = length / n;

    *start = (int64_t)index * size;
    *end = index == count - 1 ? length : *start + size;

    return TRUE;
}

BOOL mora_workspace_tile_rect(mora_layout_config_t layout, RECT area, size_t index, size_t count, RECT* tile)
{
    if (!tile || count == 0 || index >= count || !mora_layout_is_valid(layout)) {
        return FALSE;
    }

    /* 64-bit arithmetic keeps extreme RECT coordinates from overflowing. */
    int64_t left = area.left;
    int64_t top = area.top;
    int64_t right = area.right;
    int64_t bottom = area.bottom;
    int64_t width = right - left;
    int64_t height = bottom - top;

    if (width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX) {
        return FALSE;
    }

    int64_t start, end;

    if (layout.kind == MORA_LAYOUT_MAIN) {
        if (count > 1) {
            if (width < 2) {
                return FALSE;
            }

            int64_t primary = width * MORA_MAIN_PERCENT / 100;

            if (primary < 1) {
                primary = 1;
            }

            if (index == 0) {
                right = left + primary;
            } else {
                if (!split_axis(height, index - 1, count - 1, &start, &end)) {
                    return FALSE;
                }

                left += primary;
                bottom = top + end;
                top += start;
            }
        }
    } else if (layout.kind == MORA_LAYOUT_COLUMNS) {
        if (!split_axis(width, index, count, &start, &end)) {
            return FALSE;
        }

        right = left + end;
        left += start;
    } else {
        if (!split_axis(height, index, count, &start, &end)) {
            return FALSE;
        }

        bottom = top + end;
        top += start;
    }

    *tile = (RECT){ (LONG)left, (LONG)top, (LONG)right, (LONG)bottom };

    return TRUE;
}

static BOOL selected_fit(mora_layout_config_t layout, RECT area,
    mora_layout_item_t* items, size_t count, size_t selected)
{
    size_t slot = 0;

    for (size_t i = 0; i < count; ++i) {
        if (!items[i].selected) {
            continue;
        }

        RECT tile;

        if (!mora_workspace_tile_rect(layout, area, slot++, selected, &tile) ||
            items[i].minimum.cx > tile.right - tile.left ||
            items[i].minimum.cy > tile.bottom - tile.top) {
            return FALSE;
        }
    }

    return TRUE;
}

size_t mora_layout_select(mora_layout_config_t layout, RECT area,
    mora_layout_item_t* items, size_t count, size_t preferred, BOOL* oversized)
{
    if (!oversized || (count && !items)) {
        return 0;
    }

    *oversized = FALSE;

    for (size_t i = 0; i < count; ++i) {
        items[i].selected = FALSE;
    }

    if (!count || !mora_layout_is_valid(layout)) {
        return 0;
    }

    if (preferred >= count) {
        preferred = 0;
    }

    items[preferred].selected = TRUE;

    if (!selected_fit(layout, area, items, count, 1)) {
        RECT tile;

        /* A window that cannot fit must never leave the workspace without an
         * accessible window. */
        if (!mora_workspace_tile_rect(layout, area, 0, 1, &tile)) {
            tile = area;
        }

        tile.right = tile.left + max(1, max(tile.right - tile.left, items[preferred].minimum.cx));
        tile.bottom = tile.top + max(1, max(tile.bottom - tile.top, items[preferred].minimum.cy));
        items[preferred].tile = tile;
        *oversized = TRUE;

        return 1;
    }

    size_t limit = layout.kind == MORA_LAYOUT_MAIN ? 1 + MORA_MAIN_SIDE_TILES : count;
    size_t selected = 1;

    for (size_t i = 0; i < count && selected < limit; ++i) {
        if (i == preferred) {
            continue;
        }

        items[i].selected = TRUE;

        if (selected_fit(layout, area, items, count, selected + 1)) {
            ++selected;
        }
        else {
            items[i].selected = FALSE;
        }
    }

    size_t slot = 0;

    for (size_t i = 0; i < count; ++i) {
        if (items[i].selected) {
            (void)mora_workspace_tile_rect(layout, area, slot++, selected, &items[i].tile);
        }
    }

    return selected;
}
