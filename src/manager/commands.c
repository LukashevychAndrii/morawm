#include "internal.h"

#pragma comment(lib, "user32.lib")

BOOL mora_manager_activate_workspace(mora_index_t monitor, mora_index_t workspace)
{
    if (!mora_workspace_activate(&g_state, monitor, workspace)) {
        return FALSE;
    }

    mora_remember_homes();
    mora_reset_focus_guard();
    mora_workspace_remember_focus(&g_state, GetForegroundWindow());
    mora_sync_workspace_visibility();
    mora_manager_tile();
    mora_focus_workspace(workspace);

    return TRUE;
}

BOOL mora_manager_configure_workspace(mora_index_t workspace, mora_index_t monitor)
{
    if (!mora_workspace_place(&g_state, workspace, monitor)) {
        return FALSE;
    }

    mora_remember_homes();
    mora_sync_workspace_visibility();
    mora_manager_tile();

    return TRUE;
}

BOOL mora_manager_move_window_to_workspace(HWND hwnd, mora_index_t workspace)
{
    mora_index_t index = mora_manager_find_window(hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    mora_index_t source = mora_window(index)->workspace_index;
    BOOL was_foreground = GetForegroundWindow() == hwnd;
    mora_index_t monitor = workspace < MORA_WORKSPACE_COUNT ? g_state.workspaces[workspace].monitor_index : 0;

    if (!mora_workspace_move_window(&g_state, hwnd, workspace)) {
        return FALSE;
    }

    /* Arriving last would leave it out of view on a full main layout. */
    (void)mora_workspace_move_to_front(&g_state, hwnd);

    if (g_state.workspaces[workspace].monitor_index != monitor) {
        mora_remember_home(workspace);
    }

    /* Its position belonged to the source workspace's arrangement. Moving
     * changed its index. */
    if (source != workspace) {
        mora_window(mora_manager_find_window(hwnd))->floating = FALSE;
    }

    mora_sync_workspace_visibility();
    mora_manager_tile();
    /* Keep focus on the visible source workspace instead of an arbitrary window. */
    if (was_foreground && source != workspace && mora_workspace_is_active(&g_state, source)) {
        mora_focus_workspace(source);
    }

    return TRUE;
}

void mora_reset_arrangement(mora_index_t workspace)
{
    g_state.workspaces[workspace].manual = FALSE;

    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        tracked_window_t* window = mora_window(i);

        if (window->workspace_index != workspace) {
            continue;
        }

        window->floating = FALSE;
        /* A fresh start: apps that rejected placement get another chance. */
        mora_forget_placement_constraints(window);
    }
}

BOOL mora_manager_retile_workspace(mora_index_t workspace)
{
    if (workspace >= MORA_WORKSPACE_COUNT) {
        return FALSE;
    }

    mora_reset_arrangement(workspace);
    mora_manager_tile();

    return TRUE;
}

BOOL mora_manager_toggle_manual(mora_index_t workspace)
{
    if (workspace >= MORA_WORKSPACE_COUNT) {
        return FALSE;
    }

    if (g_state.workspaces[workspace].manual) {
        return mora_manager_retile_workspace(workspace);
    }

    g_state.workspaces[workspace].manual = TRUE;
    /* Brings stacked windows back where they were parked from. */
    mora_manager_tile();

    return TRUE;
}

BOOL mora_manager_set_workspace_layout(mora_index_t workspace, mora_layout_config_t layout)
{
    if (!mora_workspace_set_layout(&g_state, workspace, layout)) {
        return FALSE;
    }

    mora_manager_tile();

    return TRUE;
}

BOOL mora_manager_show_workspace(mora_index_t monitor, mora_index_t workspace)
{
    if (monitor >= g_state.monitor_count || workspace >= MORA_WORKSPACE_COUNT) {
        return FALSE;
    }

    mora_reset_focus_guard();

    if (!mora_workspace_is_active(&g_state, workspace)) {
        const workspace_t* hidden = &g_state.workspaces[workspace];

        if (hidden->window_count > 0 && hidden->monitor_index < g_state.monitor_count) {
            monitor = hidden->monitor_index;
        }

        if (!mora_manager_activate_workspace(monitor, workspace)) {
            return FALSE;
        }
    } else {
        mora_focus_workspace(workspace);
    }

    mora_move_cursor_to_workspace(workspace);

    return TRUE;
}
