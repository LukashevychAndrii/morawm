#include "internal.h"

#include <stdlib.h>

#pragma comment(lib, "user32.lib")

mora_monitor_t g_monitors[MORA_WORKSPACE_COUNT];
mora_workspace_state_t g_state;
mora_tiling_layout_t g_layout;

/* Out-of-memory in the window manager is unrecoverable. */
__declspec(noreturn) void mora_die_out_of_memory(const wchar_t* what)
{
    MessageBoxW(NULL, what, L"Memory allocation failed", MB_ICONERROR | MB_OK);
    exit(EXIT_FAILURE);
}

tracked_window_t* mora_window(mora_index_t index)
{
    return &g_state.windows.entries[index];
}

BOOL mora_manager_init(const mora_monitor_t* monitors, size_t count)
{
    if (!monitors || !mora_workspaces_init(&g_state, count)) {
        return FALSE;
    }

    for (size_t i = 0; i < count; ++i) {
        g_monitors[i] = monitors[i];
    }

    mora_reset_homes();
    mora_reset_focus_guard();

    return TRUE;
}

void mora_manager_dispose(void)
{
    mora_manager_cancel_pick();
    mora_manager_restore_windows();
    free(g_layout.entries);
    g_layout = (mora_tiling_layout_t){ 0 };
    free(g_pending.entries);
    g_pending = (mora_pending_list_t){ 0 };
    mora_workspaces_dispose(&g_state);
    ZeroMemory(g_monitors, sizeof(g_monitors));
    mora_reset_focus_guard();
}

const mora_workspace_state_t* mora_manager_state(void)
{
    return &g_state;
}

void mora_manager_set_work_area(mora_index_t monitor, RECT area)
{
    if (monitor < g_state.monitor_count) {
        g_monitors[monitor].work_area = area;
    }
}

POINT mora_manager_window_origin(HWND hwnd)
{
    RECT rect = { 0 };

    GetWindowRect(hwnd, &rect);
    POINT origin = { rect.left, rect.top };

    /* Parking moves only the left edge, and records where it was. */
    HANDLE parked_from = GetPropW(hwnd, MORA_PROP_PARKED_FROM);

    if (parked_from) {
        origin.x = mora_decode_prop(parked_from);
    }

    return origin;
}

const mora_tiling_layout_t* mora_manager_layout(void)
{
    return &g_layout;
}

mora_index_t mora_manager_find_window(HWND hwnd)
{
    return mora_workspace_find_window(&g_state, hwnd);
}
