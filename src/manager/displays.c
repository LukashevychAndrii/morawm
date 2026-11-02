#include "internal.h"

#include <wchar.h>

#pragma comment(lib, "user32.lib")

/* The display each workspace belongs to. It survives the display being
 * unplugged, so the workspace can return when it is plugged in again. */
static wchar_t g_home[MORA_WORKSPACE_COUNT][CCHDEVICENAME];
/* Was shown on its display when that display was removed: shown there again
 * when it returns. */
static BOOL g_return_visible[MORA_WORKSPACE_COUNT];

void mora_remember_home(mora_index_t workspace)
{
    wcscpy_s(g_home[workspace], CCHDEVICENAME, g_monitors[g_state.workspaces[workspace].monitor_index].device);
    g_return_visible[workspace] = FALSE;
}

void mora_reset_homes(void)
{
    for (mora_index_t w = 0; w < MORA_WORKSPACE_COUNT; ++w) {
        mora_remember_home(w);
    }
}

void mora_remember_homes(void)
{
    for (mora_index_t m = 0; m < g_state.monitor_count; ++m) {
        mora_remember_home(g_state.active_workspace[m]);
    }
}

static mora_index_t mora_find_device(const mora_monitor_t* monitors, size_t count, const wchar_t* device)
{
    for (mora_index_t m = 0; device[0] && m < count; ++m) {
        if (wcscmp(monitors[m].device, device) == 0) {
            return m;
        }
    }

    return MORA_INVALID_INDEX;
}

BOOL mora_manager_set_monitors(const mora_monitor_t* monitors, size_t count)
{
    if (!monitors || count == 0 || count > MORA_WORKSPACE_COUNT || !g_state.monitor_count) {
        return FALSE;
    }

    mora_manager_cancel_pick();

    mora_index_t old_to_new[MORA_WORKSPACE_COUNT];

    for (mora_index_t m = 0; m < g_state.monitor_count; ++m) {
        old_to_new[m] = mora_find_device(monitors, count, g_monitors[m].device);
    }

    mora_index_t fallback = 0;

    for (mora_index_t m = 0; m < count; ++m) {
        if (monitors[m].primary) {
            fallback = m;
        }
    }

    mora_index_t target[MORA_WORKSPACE_COUNT];

    wchar_t before[MORA_WORKSPACE_COUNT][CCHDEVICENAME];

    for (mora_index_t w = 0; w < MORA_WORKSPACE_COUNT; ++w) {
        mora_index_t monitor = g_state.workspaces[w].monitor_index;

        wcscpy_s(before[w], CCHDEVICENAME, g_monitors[monitor].device);
        target[w] = mora_find_device(monitors, count, g_home[w]);

        if (target[w] != MORA_INVALID_INDEX) {
            continue;
        }

        target[w] = fallback;

        if (mora_workspace_is_active(&g_state, w) && old_to_new[monitor] == MORA_INVALID_INDEX) {
            g_return_visible[w] = TRUE;
        }
    }

    if (!mora_workspaces_set_monitors(&g_state, count, old_to_new, target, g_return_visible)) {
        return FALSE;
    }

    ZeroMemory(g_monitors, sizeof(g_monitors));

    for (size_t m = 0; m < count; ++m) {
        g_monitors[m] = monitors[m];
    }

    mora_remember_homes();

    for (mora_index_t w = 0; w < MORA_WORKSPACE_COUNT; ++w) {
        if (wcscmp(before[w], g_monitors[g_state.workspaces[w].monitor_index].device) != 0) {
            mora_reset_arrangement(w);
        }
    }

    /* A monitor added to the right can reach where windows were parked. */
    mora_repark_windows();
    mora_reset_focus_guard();
    mora_sync_workspace_visibility();
    mora_manager_tile();

    return TRUE;
}

/* Keep floating and manually arranged windows where they were relative to
 * their monitor. Tiles are placed by the next layout. */
static void mora_translate_arrangement(mora_index_t workspace, const RECT* from, const RECT* to)
{
    LONG dx = to->left - from->left, dy = to->top - from->top;
    const workspace_t* state = &g_state.workspaces[workspace];

    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        const tracked_window_t* window = mora_window(i);

        if (window->workspace_index != workspace) {
            continue;
        }

        if (!(state->manual || window->floating || window->fullscreen) || window->parked ||
            window->hidden_by_workspace || !IsWindowVisible(window->hwnd)) {
            continue;
        }

        /* The restore position moves a maximized window to the monitor it
         * lands on, and a minimized one returns there. */
        WINDOWPLACEMENT placement = { .length = sizeof(placement) };

        if (!GetWindowPlacement(window->hwnd, &placement)) {
            continue;
        }

        OffsetRect(&placement.rcNormalPosition, dx, dy);
        placement.showCmd = IsIconic(window->hwnd) ? SW_SHOWMINNOACTIVE :
            IsZoomed(window->hwnd) ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE;
        placement.flags |= WPF_ASYNCWINDOWPLACEMENT;
        SetWindowPlacement(window->hwnd, &placement);
    }
}

BOOL mora_manager_swap_monitors(mora_index_t monitor, mora_index_t other)
{
    if (monitor == other || monitor >= g_state.monitor_count || other >= g_state.monitor_count) {
        return FALSE;
    }

    mora_index_t moving = g_state.active_workspace[monitor];
    mora_index_t coming = g_state.active_workspace[other];

    if (!mora_workspace_swap_monitors(&g_state, monitor, other)) {
        return FALSE;
    }

    mora_remember_homes();
    mora_translate_arrangement(moving, &g_monitors[monitor].work_area, &g_monitors[other].work_area);
    mora_translate_arrangement(coming, &g_monitors[other].work_area, &g_monitors[monitor].work_area);
    mora_reset_focus_guard();
    mora_manager_tile();
    mora_focus_workspace(moving);
    mora_move_cursor_to_workspace(moving);

    return TRUE;
}
