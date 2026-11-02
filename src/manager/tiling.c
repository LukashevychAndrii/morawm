#include "internal.h"

#include <dwmapi.h>
#include <stdlib.h>

#include "common/array.h"
#include "common/debug.h"
#include "detection/window_detection.h"
#include "log/log.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

#define MORA_PLACEMENT_TOLERANCE 2
#define MORA_MAX_PLACEMENT_CORRECTIONS 2
#define MORA_DWM_BORDER_BASE 64
#define MORA_FLOAT_SIZE_NUMERATOR 2
#define MORA_FLOAT_SIZE_DENOMINATOR 3

static ULONGLONG g_layout_applied_at;

static void mora_push_tiled_window(mora_tiled_window_t window)
{
    mora_tiled_window_t* entries = mora_array_reserve(g_layout.entries, &g_layout.capacity,
        g_layout.count + 1, sizeof(*entries));

    if (!entries) {
        mora_die_out_of_memory(L"Could not allocate tiling layout.");
    }

    g_layout.entries = entries;
    g_layout.entries[g_layout.count++] = window;
}

static BOOL mora_in_layout(const workspace_t* workspace, const tracked_window_t* window)
{
    return window->tileable && !window->floating && !window->elevated && !workspace->manual;
}

static size_t mora_count_layout_windows(mora_index_t workspace_index)
{
    workspace_t* workspace = &g_state.workspaces[workspace_index];
    size_t count = 0;

    workspace->floating_count = 0;
    workspace->elevated_count = 0;

    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        tracked_window_t* window = mora_window(i);

        if (window->workspace_index != workspace_index || !window->tileable) {
            continue;
        }

        window->fullscreen = mora_is_fullscreen_window(window->hwnd);

        if (window->elevated) {
            ++workspace->elevated_count;
        }
        else if (window->floating) {
            ++workspace->floating_count;
        }

        if (mora_in_layout(workspace, window)) {
            ++count;
        }
        else {
            window->overflow = FALSE;
        }
    }

    return count;
}

static RECT mora_get_invisible_border(HWND hwnd)
{
    RECT frame, visible;

    if (!GetWindowRect(hwnd, &frame) || !mora_window_bounds(hwnd, &visible)) {
        return (RECT){ 0 };
    }

    RECT border = {
        .left = visible.left - frame.left,
        .top = visible.top - frame.top,
        .right = frame.right - visible.right,
        .bottom = frame.bottom - visible.bottom,
    };
    LONG limit = mora_scale(MORA_DWM_BORDER_BASE, mora_dpi_of_window(hwnd));

    if (border.left < 0 || border.top < 0 || border.right < 0 || border.bottom < 0 ||
        border.left > limit || border.top > limit || border.right > limit || border.bottom > limit) {
        return (RECT){ 0 };
    }

    return border;
}

/* Shrink `size` to `cap`, moving half the freed space in front of the window,
 * when the cap exists and is smaller. */
static void mora_shrink_centered(int* size, int* position, LONG cap)
{
    if (cap > 0 && cap < *size) {
        *position += (*size - cap) / 2;
        *size = (int)cap;
    }
}

static mora_tile_pos_t mora_position_for_tile(HWND hwnd, RECT tile, SIZE maximum)
{
    RECT border = mora_get_invisible_border(hwnd);
    mora_tile_pos_t pos = {
        .x = tile.left - border.left,
        .y = tile.top - border.top,
        .width = (tile.right - tile.left) + border.left + border.right,
        .height = (tile.bottom - tile.top) + border.top + border.bottom,
    };

    mora_shrink_centered(&pos.width, &pos.x, maximum.cx);
    mora_shrink_centered(&pos.height, &pos.y, maximum.cy);

    if (maximum.cx <= 0) {
        --pos.x;
        pos.width += 2;
    }

    if (maximum.cy <= 0) {
        --pos.y;
        pos.height += 2;
    }

    if (pos.width <= 0) {
        pos.width = 1;
    }

    if (pos.height <= 0) {
        pos.height = 1;
    }

    return pos;
}

static void mora_calculate_workspace_layout(const mora_monitor_t* monitor, mora_index_t workspace_index)
{
    workspace_t* workspace = &g_state.workspaces[workspace_index];
    size_t tile_count = mora_count_layout_windows(workspace_index);

    workspace->overflow_count = 0;
    workspace->oversized = FALSE;

    if (tile_count == 0) {
        return;
    }

    mora_layout_item_t* items = calloc(tile_count, sizeof(*items));
    mora_index_t* indices = calloc(tile_count, sizeof(*indices));

    if (!items || !indices) {
        mora_die_out_of_memory(L"Could not allocate minimum-size layout.");
    }

    RECT area = monitor->work_area;

    area.top += monitor->reserved_top;

    size_t tile_index = 0, preferred = 0;

    for (mora_index_t index = 0; index < g_state.windows.count; ++index) {
        tracked_window_t* window = mora_window(index);

        if (window->workspace_index != workspace_index || !mora_in_layout(workspace, window)) {
            continue;
        }

        const mora_placement_t* known = &window->placement;
        mora_window_constraints_t constraints = window->fullscreen ?
            (mora_window_constraints_t){ .minimum = { max(known->reported_minimum.cx, known->observed_minimum.cx),
                max(known->reported_minimum.cy, known->observed_minimum.cy) } } :
            mora_window_constraints(window, monitor->hmonitor);
        RECT border = mora_get_invisible_border(window->hwnd);

        items[tile_index].minimum = (SIZE){ max(1, constraints.minimum.cx - border.left - border.right),
            max(1, constraints.minimum.cy - border.top - border.bottom) };
        indices[tile_index] = index;

        if (window->hwnd == workspace->focused_window) {
            preferred = tile_index;
        }

        ++tile_index;
    }

    size_t selected;

    if (mora_window(indices[preferred])->placement.failed) {
        items[preferred] = (mora_layout_item_t){ .selected = TRUE, .tile = area };
        selected = 1;
        workspace->oversized = TRUE;
    } else {
        for (size_t i = 0; i < tile_count; ++i) {
            if (mora_window(indices[i])->placement.failed) {
                items[i].minimum = (SIZE){ INT32_MAX, INT32_MAX };
            }
        }

        selected = mora_layout_select(workspace->layout, area, items, tile_count, preferred, &workspace->oversized);
    }

    workspace->overflow_count = tile_count - selected;

    for (size_t i = 0; i < tile_count; ++i) {
        tracked_window_t* window = mora_window(indices[i]);

        window->overflow = !items[i].selected;

        if (window->overflow) {
            continue;
        }

        /* reported_maximum was refreshed by mora_window_constraints in the
         * loop above, so it matches the tile decisions of this layout. */
        mora_push_tiled_window((mora_tiled_window_t){
            .hwnd = window->hwnd,
            .pos = mora_position_for_tile(window->hwnd, items[i].tile, window->placement.reported_maximum),
        });
    }

    free(indices);
    free(items);
}

void mora_manager_calculate_layout(void)
{
    g_layout.count = 0;

    for (mora_index_t m = 0; m < g_state.monitor_count; m++) {
        mora_calculate_workspace_layout(&g_monitors[m], g_state.active_workspace[m]);
    }
}

/* A window without WS_SIZEBOX keeps its current size; center it in the
 * requested rect so the space it cannot fill splits evenly. */
static void mora_keep_size_if_not_resizable(HWND hwnd, RECT* rect)
{
    RECT current;

    if (!GetWindowRect(hwnd, &current)) {
        return;
    }

    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_SIZEBOX) {
        return;
    }

    LONG width = mora_rect_width(current), height = mora_rect_height(current);

    rect->left += max(0, mora_rect_width(*rect) - width) / 2;
    rect->top += max(0, mora_rect_height(*rect) - height) / 2;
    rect->right = rect->left + width;
    rect->bottom = rect->top + height;
}

static void mora_screen_to_workspace_rect(RECT* rect)
{
    HMONITOR monitor = MonitorFromRect(rect, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = { .cbSize = sizeof(info) };

    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }

    OffsetRect(rect, -(info.rcWork.left - info.rcMonitor.left), -(info.rcWork.top - info.rcMonitor.top));
}

static BOOL mora_place_window(HWND hwnd, RECT rect)
{
    WINDOWPLACEMENT placement = { .length = sizeof(placement) };

    if (!GetWindowPlacement(hwnd, &placement)) {
        return FALSE;
    }

    mora_keep_size_if_not_resizable(hwnd, &rect);

    BOOL visible = IsWindowVisible(hwnd);
    RECT current;

    if (visible && placement.showCmd == SW_SHOWNORMAL &&
        GetWindowRect(hwnd, &current) && EqualRect(&current, &rect)) {
        return TRUE;
    }

    if (!visible) {
        placement.showCmd = SW_HIDE;
    } else if (placement.showCmd != SW_SHOWMINIMIZED && placement.showCmd != SW_MINIMIZE) {
        if (placement.showCmd == SW_SHOWMAXIMIZED) {
            placement.flags &= ~WPF_RESTORETOMAXIMIZED;
        }

        placement.showCmd = SW_RESTORE;
    }

    mora_screen_to_workspace_rect(&rect);
    placement.rcNormalPosition = rect;
    placement.flags |= WPF_ASYNCWINDOWPLACEMENT;

    return SetWindowPlacement(hwnd, &placement);
}

static void mora_log_placement_failure(HWND hwnd, const wchar_t* reason)
{
    mora_log_window(hwnd, L"placement failed", L"%ls; shown alone from now on", reason);
}

static void mora_apply_layout(void)
{
    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        tracked_window_t* window = mora_window(i);

        window->placement.verify = FALSE;

        if (window->tileable) {
            mora_set_window_overflow(window, window->overflow);
        }
    }

    for (size_t i = 0; i < g_layout.count; i++) {
        const mora_tiled_window_t* tile = &g_layout.entries[i];
        tracked_window_t* window = mora_window(mora_manager_find_window(tile->hwnd));

        mora_set_window_overflow(window, FALSE);

        if (window->placement.failed || window->fullscreen) {
            continue;
        }

        const mora_tile_pos_t* pos = &tile->pos;

        mora_debug_log(L"Tiling HWND %p: X=%d Y=%d Width=%d Height=%d\n",
            tile->hwnd, pos->x, pos->y, pos->width, pos->height);

        RECT target = mora_tile_pos_rect(*pos);

        if (!mora_place_window(tile->hwnd, target) &&
            !SetWindowPos(tile->hwnd, HWND_NOTOPMOST, pos->x, pos->y, pos->width, pos->height,
                SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS)) {
            mora_log_placement_failure(tile->hwnd, L"move rejected");
            window->placement.failed = TRUE;
        }

        window->placement.target = target;
        window->placement.verify = TRUE;
    }
}

void mora_manager_tile(void)
{
    mora_sync_workspace_visibility();
    mora_manager_calculate_layout();
    mora_apply_layout();
    g_layout_applied_at = GetTickCount64();
}

BOOL mora_manager_layout_pending(void)
{
    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        if (mora_window(i)->placement.verify) {
            return TRUE;
        }
    }

    return FALSE;
}

static void mora_remember_geometry(tracked_window_t* window, RECT actual)
{
    window->placement.geometry = actual;
    window->placement.geometry_dpi = GetDpiForWindow(window->hwnd);
}

static BOOL mora_verify_tile(tracked_window_t* window)
{
    mora_placement_t* placement = &window->placement;

    if (placement->failed) {
        return TRUE;
    }

    RECT actual;

    if (!window->tileable || IsIconic(window->hwnd) || !GetWindowRect(window->hwnd, &actual)) {
        return FALSE;
    }

    mora_remember_geometry(window, actual);

    if (mora_is_fullscreen_window(window->hwnd)) {
        return TRUE;
    }

    if (IsZoomed(window->hwnd)) {
        window->floating = TRUE;

        return TRUE;
    }

    RECT target = placement->target;
    LONG width = mora_rect_width(actual), height = mora_rect_height(actual);
    BOOL too_wide = width > target.right - target.left + MORA_PLACEMENT_TOLERANCE;
    BOOL too_tall = height > target.bottom - target.top + MORA_PLACEMENT_TOLERANCE;

    if (too_wide) {
        placement->observed_minimum.cx = max(placement->observed_minimum.cx, width);
    }

    if (too_tall) {
        placement->observed_minimum.cy = max(placement->observed_minimum.cy, height);
    }

    if (too_wide || too_tall) {
        if (++placement->corrections >= MORA_MAX_PLACEMENT_CORRECTIONS) {
            mora_log_placement_failure(window->hwnd, L"keeps a larger size");
            placement->failed = TRUE;
        }

        return TRUE;
    }

    if (abs(actual.left - target.left) > MORA_PLACEMENT_TOLERANCE ||
        abs(actual.top - target.top) > MORA_PLACEMENT_TOLERANCE) {
        mora_log_placement_failure(window->hwnd, L"stayed in another position");
        placement->failed = TRUE;

        return TRUE;
    }

    return FALSE;
}

BOOL mora_manager_verify_layout(void)
{
    if (GetTickCount64() - g_layout_applied_at < MORA_PLACEMENT_SETTLE_MS) {
        return FALSE;
    }

    BOOL changed = FALSE;

    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        tracked_window_t* window = mora_window(i);

        if (!window->placement.verify) {
            continue;
        }

        window->placement.verify = FALSE;

        if (window->overflow) {
            mora_verify_overflow_parking(window);
        }
        else {
            changed |= mora_verify_tile(window);
        }
    }

    if (changed) {
        mora_manager_tile();
    }

    return changed;
}

BOOL mora_manager_window_geometry_changed(HWND hwnd)
{
    mora_index_t index = mora_manager_find_window(hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    tracked_window_t* window = mora_window(index);

    if (window->placement.verify || window->floating || window->elevated || IsIconic(hwnd) ||
        !mora_workspace_is_active(&g_state, window->workspace_index) ||
        g_state.workspaces[window->workspace_index].manual) {
        return FALSE;
    }

    if (mora_is_fullscreen_window(hwnd) != window->fullscreen) {
        window->fullscreen = !window->fullscreen;

        if (GetWindowRect(hwnd, &window->placement.geometry)) {
            window->placement.geometry_dpi = GetDpiForWindow(hwnd);
        }

        return TRUE;
    }

    if (window->fullscreen) {
        return FALSE;
    }

    if (IsZoomed(hwnd) && !IsRectEmpty(&window->placement.target)) {
        window->floating = TRUE;

        return TRUE;
    }

    RECT actual;

    if (!GetWindowRect(hwnd, &actual)) {
        return FALSE;
    }

    const RECT* known = &window->placement.geometry;

    if (actual.right - actual.left == known->right - known->left &&
        actual.bottom - actual.top == known->bottom - known->top &&
        GetDpiForWindow(hwnd) == window->placement.geometry_dpi) {
        return FALSE;
    }

    mora_remember_geometry(window, actual);
    mora_forget_placement_constraints(window);

    return TRUE;
}

static HWND mora_tile_at_drop(mora_index_t workspace, RECT actual, POINT center)
{
    POINT point = center, cursor;

    if (GetCursorPos(&cursor) && PtInRect(&actual, cursor)) {
        point = cursor;
    }

    for (size_t i = 0; i < g_layout.count; ++i) {
        const mora_tile_pos_t* pos = &g_layout.entries[i].pos;
        RECT tile = mora_tile_pos_rect(*pos);
        mora_index_t index = mora_manager_find_window(g_layout.entries[i].hwnd);

        if (index != MORA_INVALID_INDEX && mora_window(index)->workspace_index == workspace &&
            PtInRect(&tile, point)) {
            return g_layout.entries[i].hwnd;
        }
    }

    return NULL;
}

static BOOL mora_adopt_dropped_window(HWND hwnd, mora_index_t source, RECT actual)
{
    POINT center = mora_rect_center(actual);
    mora_index_t monitor = mora_monitor_at_point(center);

    if (monitor == MORA_INVALID_INDEX) {
        return FALSE;
    }

    mora_index_t workspace = g_state.active_workspace[monitor];

    if (workspace == source) {
        return FALSE;
    }

    HWND displaced = mora_tile_at_drop(workspace, actual, center);

    if (!mora_workspace_move_window(&g_state, hwnd, workspace)) {
        return FALSE;
    }

    if (displaced) {
        (void)mora_workspace_swap_windows(&g_state, hwnd, displaced);
    }

    tracked_window_t* window = mora_window(mora_manager_find_window(hwnd));

    window->floating = FALSE;
    window->placement.verify = FALSE;
    mora_remember_geometry(window, actual);
    mora_workspace_remember_focus(&g_state, window->hwnd);

    if (GetForegroundWindow() == window->hwnd) {
        mora_record_foreground(window->hwnd);
    }

    return TRUE;
}

BOOL mora_manager_window_moved_by_user(HWND hwnd)
{
    mora_index_t index = mora_manager_find_window(hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    tracked_window_t* window = mora_window(index);
    RECT actual;

    if (window->fullscreen || !window->tileable || window->overflow ||
        !mora_workspace_is_active(&g_state, window->workspace_index) || !GetWindowRect(hwnd, &actual)) {
        return FALSE;
    }

    if (mora_adopt_dropped_window(hwnd, window->workspace_index, actual)) {
        return TRUE;
    }

    if (window->floating || window->elevated || g_state.workspaces[window->workspace_index].manual) {
        return FALSE;
    }

    const RECT* target = &window->placement.target;

    if (abs(actual.left - target->left) <= MORA_PLACEMENT_TOLERANCE &&
        abs(actual.top - target->top) <= MORA_PLACEMENT_TOLERANCE &&
        abs(actual.right - target->right) <= MORA_PLACEMENT_TOLERANCE &&
        abs(actual.bottom - target->bottom) <= MORA_PLACEMENT_TOLERANCE) {
        return FALSE;
    }

    window->floating = TRUE;
    window->placement.verify = FALSE;
    mora_remember_geometry(window, actual);

    return TRUE;
}

static RECT mora_floating_rect(tracked_window_t* window, const mora_monitor_t* monitor)
{
    RECT area = monitor->work_area;

    area.top += monitor->reserved_top;
    LONG area_width = area.right - area.left, area_height = area.bottom - area.top;
    SIZE size = { area_width * MORA_FLOAT_SIZE_NUMERATOR / MORA_FLOAT_SIZE_DENOMINATOR,
        area_height * MORA_FLOAT_SIZE_NUMERATOR / MORA_FLOAT_SIZE_DENOMINATOR };
    RECT current;

    if ((GetWindowLongPtrW(window->hwnd, GWL_STYLE) & WS_SIZEBOX) == 0 && GetWindowRect(window->hwnd, &current)) {
        size = (SIZE){ current.right - current.left, current.bottom - current.top };
    } else {
        mora_window_constraints_t constraints = mora_window_constraints(window, monitor->hmonitor);

        size = (SIZE){ max(size.cx, constraints.minimum.cx), max(size.cy, constraints.minimum.cy) };

        /* A capped window floats at its cap, not at the area's two thirds. */
        if (constraints.maximum.cx > 0) {
            size.cx = min(size.cx, constraints.maximum.cx);
        }

        if (constraints.maximum.cy > 0) {
            size.cy = min(size.cy, constraints.maximum.cy);
        }
    }

    size = (SIZE){ min(size.cx, area_width), min(size.cy, area_height) };
    LONG left = area.left + (area_width - size.cx) / 2, top = area.top + (area_height - size.cy) / 2;

    return (RECT){ left, top, left + size.cx, top + size.cy };
}

BOOL mora_manager_toggle_floating(HWND hwnd)
{
    mora_index_t index = mora_manager_find_window(hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    tracked_window_t* window = mora_window(index);
    const workspace_t* workspace = &g_state.workspaces[window->workspace_index];

    if (!window->tileable || window->elevated || window->fullscreen || workspace->manual ||
        !mora_workspace_is_active(&g_state, window->workspace_index) ||
        workspace->monitor_index >= g_state.monitor_count) {
        return FALSE;
    }

    window->placement.verify = FALSE;

    if (window->floating) {
        window->floating = FALSE;
    } else {
        mora_set_window_overflow(window, FALSE);
        window->overflow = FALSE;
        window->floating = TRUE;
        RECT rect = mora_floating_rect(window, &g_monitors[workspace->monitor_index]);

        if (mora_place_window(hwnd, rect)) {
            mora_remember_geometry(window, rect);
        }

        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }

    mora_manager_tile();

    return TRUE;
}
