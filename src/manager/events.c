#include "internal.h"

#include "log/log.h"

#pragma comment(lib, "user32.lib")

#define MORA_FOCUS_GUARD_MS 150
#define MORA_RESTORE_GUARD_MS 500

static HWND g_last_foreground;
static HWND g_user_moving;
static mora_index_t g_last_workspace = MORA_INVALID_INDEX;
static mora_index_t g_guard_workspace = MORA_INVALID_INDEX;
static ULONGLONG g_guard_until;
static mora_index_t g_work_workspace = MORA_INVALID_INDEX;

void mora_reset_focus_guard(void)
{
    g_last_foreground = NULL;
    g_last_workspace = g_guard_workspace = g_work_workspace = MORA_INVALID_INDEX;
    g_guard_until = 0;
}

void mora_record_foreground(HWND hwnd)
{
    mora_index_t index = mora_manager_find_window(hwnd);

    g_last_foreground = hwnd;
    g_last_workspace = index == MORA_INVALID_INDEX ? MORA_INVALID_INDEX : mora_window(index)->workspace_index;

    if (g_last_workspace != MORA_INVALID_INDEX && mora_workspace_is_active(&g_state, g_last_workspace)) {
        g_work_workspace = g_last_workspace;
    }

    for (mora_index_t m = 0; hwnd && m < g_state.monitor_count; ++m) {
        if (g_monitors[m].bar_hwnd == hwnd) {
            g_work_workspace = g_state.active_workspace[m];
        }
    }
}

mora_index_t mora_work_workspace(void)
{
    return g_work_workspace < MORA_WORKSPACE_COUNT && mora_workspace_is_active(&g_state, g_work_workspace) ?
        g_work_workspace : MORA_INVALID_INDEX;
}

static void mora_guard_workspace(mora_index_t workspace)
{
    g_guard_workspace = workspace;
    g_guard_until = GetTickCount64() + MORA_FOCUS_GUARD_MS;
    g_last_foreground = NULL;
    g_last_workspace = MORA_INVALID_INDEX;
    mora_sync_workspace_visibility();
    mora_focus_workspace(workspace);
}

static BOOL mora_previous_foreground_disappeared(void)
{
    return g_last_workspace != MORA_INVALID_INDEX &&
        mora_workspace_is_active(&g_state, g_last_workspace) && g_last_foreground &&
        (!IsWindow(g_last_foreground) || IsIconic(g_last_foreground) || !IsWindowVisible(g_last_foreground));
}

static BOOL mora_recover_lost_foreground(BOOL parked_window)
{
    if (mora_previous_foreground_disappeared()) {
        mora_guard_workspace(g_last_workspace);

        return TRUE;
    }

    if (parked_window && g_guard_workspace != MORA_INVALID_INDEX && GetTickCount64() < g_guard_until &&
        mora_workspace_is_active(&g_state, g_guard_workspace)) {
        mora_focus_workspace(g_guard_workspace);

        return TRUE;
    }

    return FALSE;
}

BOOL mora_manager_handle_event(DWORD event, HWND hwnd)
{
    if (!hwnd) {
        return FALSE;
    }

    mora_index_t index = mora_manager_find_window(hwnd);
    BOOL tracked = index != MORA_INVALID_INDEX;
    mora_index_t workspace = tracked ? mora_window(index)->workspace_index : MORA_INVALID_INDEX;
    BOOL lost_focus = tracked && mora_workspace_is_active(&g_state, workspace) &&
        (hwnd == GetForegroundWindow() || hwnd == g_last_foreground);

    if (event == EVENT_OBJECT_DESTROY) {
        mora_pick_window_event(event, hwnd);
        mora_forget_pending(hwnd);
        BOOL removed = mora_workspace_remove_window(&g_state, hwnd);

        if (removed) {
            mora_log(L"untrack %p: destroyed", (void*)hwnd);
        }

        if (lost_focus) {
            mora_guard_workspace(workspace);
        }

        return removed;
    }

    if (GetAncestor(hwnd, GA_ROOT) != hwnd) {
        return FALSE;
    }

    switch (event) {
    case EVENT_SYSTEM_FOREGROUND: {
        if (GetForegroundWindow() != hwnd) {
            return FALSE;
        }

        mora_pick_window_event(event, hwnd);

        if (tracked && mora_workspace_is_active(&g_state, workspace)) {
            tracked_window_t* window = mora_window(index);

            if (window->restored_at && GetTickCount64() - window->restored_at < MORA_RESTORE_GUARD_MS) {
                window->restored_at = 0;

                if (g_state.workspaces[workspace].focused_window != hwnd) {
                    mora_focus_workspace(workspace);

                    return FALSE;
                }
            }

            window->restored_at = 0;
            mora_workspace_remember_focus(&g_state, hwnd);
            mora_record_foreground(hwnd);

            if (mora_window(index)->overflow) {
                mora_manager_tile();
            }

            return FALSE;
        }

        if (mora_recover_lost_foreground(tracked)) {
            return TRUE;
        }

        if (!tracked) {
            mora_record_foreground(hwnd);

            return FALSE;
        }

        mora_workspace_remember_focus(&g_state, hwnd);
        (void)mora_manager_activate_workspace(g_state.workspaces[workspace].monitor_index, workspace);

        return FALSE;
    }

    case EVENT_OBJECT_LOCATIONCHANGE:
        return tracked && hwnd != g_user_moving && mora_manager_window_geometry_changed(hwnd);

    case EVENT_SYSTEM_MOVESIZESTART:
        g_user_moving = hwnd;

        return FALSE;

    case EVENT_SYSTEM_MOVESIZEEND:
        g_user_moving = NULL;

        return tracked && mora_manager_window_moved_by_user(hwnd);

    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_UNCLOAKED:
    case EVENT_SYSTEM_MINIMIZEEND:
        if (!tracked && !mora_discover_window(hwnd, event != EVENT_SYSTEM_MINIMIZEEND)) {
            return FALSE;
        }
        break;

    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_CLOAKED:
    case EVENT_SYSTEM_MINIMIZESTART:
        if (!tracked) {
            return FALSE;
        }
        break;

    default:
        return FALSE;
    }

    mora_sync_workspace_visibility();

    if (event == EVENT_SYSTEM_MINIMIZESTART && lost_focus && IsIconic(hwnd)) {
        mora_guard_workspace(workspace);
    }

    return TRUE;
}
