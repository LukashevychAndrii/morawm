#include "internal.h"

#pragma comment(lib, "user32.lib")

/* Windows lets only the process that received the last input take the
 * foreground. After a click closes or minimizes a window that is another
 * app, so Windows' own choice would stand, which can be a parked window of
 * another workspace. Sharing the foreground thread's input state for the
 * call lifts that restriction. A hung foreground thread is left alone. */
static BOOL mora_set_foreground(HWND hwnd)
{
    if (SetForegroundWindow(hwnd)) {
        return TRUE;
    }

    HWND foreground = GetForegroundWindow();
    DWORD current = GetCurrentThreadId();
    DWORD thread = foreground && !IsHungAppWindow(foreground) ? GetWindowThreadProcessId(foreground, NULL) : 0;

    if (!thread || thread == current || !AttachThreadInput(current, thread, TRUE)) {
        return FALSE;
    }

    BOOL focused = SetForegroundWindow(hwnd);

    AttachThreadInput(current, thread, FALSE);

    return focused;
}

static mora_index_t mora_first_tileable_window(mora_index_t workspace)
{
    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        const tracked_window_t* window = mora_window(i);

        if (window->workspace_index == workspace && window->tileable) {
            return i;
        }
    }

    return MORA_INVALID_INDEX;
}

void mora_focus_workspace(mora_index_t workspace_index)
{
    const workspace_t* workspace = &g_state.workspaces[workspace_index];
    mora_index_t focus = mora_manager_find_window(workspace->focused_window);

    if (focus == MORA_INVALID_INDEX || !mora_window(focus)->tileable) {
        focus = mora_first_tileable_window(workspace_index);
    }

    if (focus != MORA_INVALID_INDEX) {
        HWND hwnd = mora_window(focus)->hwnd;

        (void)mora_focus_window(hwnd);

        return;
    }

    /* Give an empty workspace a foreground target for subsequent hotkeys. */
    mora_index_t monitor = workspace->monitor_index;

    if (monitor < g_state.monitor_count && g_monitors[monitor].bar_hwnd) {
        HWND bar = g_monitors[monitor].bar_hwnd;

        if (!IsWindowVisible(bar)) {
            ShowWindow(bar, SW_SHOWNOACTIVATE);
        }

        if (mora_set_foreground(bar)) {
            mora_record_foreground(bar);
        }
    }
}

void mora_move_cursor_to_workspace(mora_index_t workspace_index)
{
    if (!mora_workspace_is_active(&g_state, workspace_index)) {
        return;
    }

    const workspace_t* workspace = &g_state.workspaces[workspace_index];
    const RECT* area = &g_monitors[workspace->monitor_index].work_area;
    POINT target = mora_rect_center(*area);

    /* Windows coming back from parking move asynchronously, so their tile is
     * where they are about to be. */
    HWND focused = workspace->focused_window;

    for (size_t i = 0; focused && i < g_layout.count; ++i) {
        const mora_tile_pos_t* pos = &g_layout.entries[i].pos;

        if (g_layout.entries[i].hwnd != focused) {
            continue;
        }

        target = mora_tile_pos_center(*pos);
        break;
    }

    POINT cursor;
    HMONITOR destination = MonitorFromPoint(target, MONITOR_DEFAULTTONULL);

    if (destination && GetCursorPos(&cursor) &&
        MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST) != destination) {
        SetCursorPos(target.x, target.y);
    }
}

BOOL mora_focus_window(HWND hwnd)
{
    mora_index_t index = mora_manager_find_window(hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    if (mora_window(index)->overflow) {
        /* Reveal before activation; otherwise focus would land off-screen. */
        mora_workspace_remember_focus(&g_state, hwnd);
        mora_manager_tile();
    }

    if (!mora_set_foreground(hwnd)) {
        return FALSE;
    }

    mora_workspace_remember_focus(&g_state, hwnd);
    mora_record_foreground(hwnd);

    return TRUE;
}

BOOL mora_manager_focus_window(HWND hwnd)
{
    return mora_focus_window(hwnd);
}
