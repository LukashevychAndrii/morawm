#include "internal.h"

#include "detection/window_detection.h"
#include "keyboard/keyboard.h"
#include "log/log.h"

#pragma comment(lib, "user32.lib")

/* The foreground window's workspace monitor, else the foreground window's
 * native monitor, else the cursor's monitor. */
mora_index_t mora_manager_hotkey_monitor(HWND foreground)
{
    mora_index_t index = mora_manager_find_window(foreground);

    if (index != MORA_INVALID_INDEX) {
        mora_index_t workspace = mora_window(index)->workspace_index;

        if (mora_workspace_is_active(&g_state, workspace)) {
            return g_state.workspaces[workspace].monitor_index;
        }
    }

    HMONITOR native = foreground ? MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) : NULL;
    POINT cursor;

    if (!native && GetCursorPos(&cursor)) {
        native = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    }

    mora_index_t monitor = mora_monitor_index(native);

    return monitor == MORA_INVALID_INDEX ? 0 : monitor;
}

mora_index_t mora_manager_next_window(mora_index_t workspace, mora_index_t current, int step)
{
    mora_index_t count = (mora_index_t)g_state.windows.count;

    if (count == 0) {
        return MORA_INVALID_INDEX;
    }

    /* Without a current window, the first step lands on an end. */
    mora_index_t index = current < count ? current : step < 0 ? 0 : count - 1;

    for (mora_index_t visited = 0; visited < count; ++visited) {
        index = step < 0 ? (index + count - 1) % count : (index + 1) % count;
        const tracked_window_t* window = mora_window(index);

        if (window->workspace_index == workspace && mora_manager_window_reachable(window)) {
            return index;
        }
    }

    return MORA_INVALID_INDEX;
}

static mora_index_t mora_managed_foreground(HWND foreground, mora_index_t workspace, BOOL movable)
{
    mora_index_t index = mora_manager_find_window(foreground);

    if (index == MORA_INVALID_INDEX || mora_window(index)->workspace_index != workspace ||
        !(movable ? mora_is_tiling_candidate(foreground) : mora_is_workspace_candidate(foreground))) {
        return MORA_INVALID_INDEX;
    }

    return index;
}

static mora_index_t mora_adjacent_monitor(mora_index_t monitor, int step)
{
    mora_index_t count = (mora_index_t)g_state.monitor_count;

    return step < 0 ? (monitor + count - 1) % count : (monitor + 1) % count;
}

static mora_index_t mora_adjacent_monitor_workspace(mora_index_t monitor, int step)
{
    return g_state.active_workspace[mora_adjacent_monitor(monitor, step)];
}

static BOOL mora_focus_neighbor(mora_index_t workspace, mora_index_t current, int step)
{
    mora_index_t next = mora_manager_next_window(workspace, current, step);

    return next != MORA_INVALID_INDEX && mora_focus_window(mora_window(next)->hwnd);
}

BOOL mora_manager_dispatch_hotkey(size_t id, HWND foreground, mora_index_t monitor)
{
    wchar_t pick_key = mora_keyboard_pick_key(id);

    if (pick_key) {
        return mora_manager_pick_key(pick_key);
    }

    const mora_key_binding_t* binding = mora_keyboard_binding(id);

    if (!binding) {
        return FALSE;
    }

    if (binding->command == MORA_KEY_QUIT) {
        mora_log(L"quit: Alt+Shift+Q");
        PostQuitMessage(0);

        return TRUE;
    }

    if (mora_manager_pick()->held) {
        /* Alt+letter and Alt+digit name where the held window goes, whether
         * or not Alt is let go first. Alt+M again cancels. */
        if (binding->command == MORA_KEY_PICK) {
            mora_manager_cancel_pick();

            return TRUE;
        }

        return mora_manager_pick_key(mora_keyboard_binding_pick_key(binding));
    }

    if (monitor >= g_state.monitor_count) {
        return FALSE;
    }

    mora_sync_workspace_visibility();
    int argument = binding->argument;
    mora_index_t workspace = g_state.active_workspace[monitor];
    mora_index_t current = mora_managed_foreground(foreground, workspace, TRUE);
    BOOL has_current = current != MORA_INVALID_INDEX;
    mora_layout_config_t layout = g_state.workspaces[workspace].layout;

    switch (binding->command) {
    case MORA_KEY_WORKSPACE:
        return mora_manager_show_workspace(monitor, (mora_index_t)argument);

    case MORA_KEY_MOVE_WORKSPACE:
        return has_current && mora_manager_move_window_to_workspace(foreground, (mora_index_t)argument);

    case MORA_KEY_FOCUS:
        return mora_focus_neighbor(workspace, mora_managed_foreground(foreground, workspace, FALSE), argument);

    case MORA_KEY_MONITOR:
        workspace = mora_adjacent_monitor_workspace(monitor, argument);
        mora_focus_workspace(workspace);
        mora_move_cursor_to_workspace(workspace);

        return TRUE;

    case MORA_KEY_PICK:
        return has_current && mora_manager_pick_up(foreground);

    case MORA_KEY_SWAP_MONITOR:
        return mora_manager_swap_monitors(monitor, mora_adjacent_monitor(monitor, argument));

    case MORA_KEY_LAYOUT:
        layout.kind = layout.kind == MORA_LAYOUT_MAIN ? MORA_LAYOUT_COLUMNS :
            layout.kind == MORA_LAYOUT_COLUMNS ? MORA_LAYOUT_ROWS : MORA_LAYOUT_MAIN;

        return mora_manager_set_workspace_layout(workspace, layout);

    case MORA_KEY_RETILE:
        return mora_manager_retile_workspace(workspace);

    case MORA_KEY_MANUAL:
        return mora_manager_toggle_manual(workspace);

    case MORA_KEY_CLOSE:
        /* As if its close button was clicked, so the app can still ask to
         * save. Unlike WM_CLOSE, this also reaches elevated windows. */
        return mora_managed_foreground(foreground, workspace, FALSE) != MORA_INVALID_INDEX &&
            PostMessageW(foreground, WM_SYSCOMMAND, SC_CLOSE, 0);

    case MORA_KEY_FLOAT:
        return has_current && mora_manager_toggle_floating(foreground);

    default:
        return FALSE;
    }
}
