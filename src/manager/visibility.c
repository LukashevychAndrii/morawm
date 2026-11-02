#include "internal.h"

#include "detection/window_detection.h"
#include "log/log.h"

#pragma comment(lib, "user32.lib")

/* Windows on inactive workspaces are parked: moved off-screen at their current
 * size instead of hidden. A hidden window loses its DWM surface and must
 * repaint when shown again, which lags; a parked one only moves back, so the
 * switch is instant. Minimized windows have nothing on screen and are hidden. */

#define MORA_PARKING_MARGIN 100
/* Windows clamps window coordinates to 16 bits. */
#define MORA_PARKING_MAX_LEFT 30000

static LONG mora_parking_left(void)
{
    LONG right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);

    for (mora_index_t m = 0; m < g_state.monitor_count; ++m) {
        if (g_monitors[m].work_area.right > right) {
            right = g_monitors[m].work_area.right;
        }
    }

    right += MORA_PARKING_MARGIN;

    return right < MORA_PARKING_MAX_LEFT ? right : MORA_PARKING_MAX_LEFT;
}

void mora_move_window_left_edge(HWND hwnd, LONG left)
{
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT placement = { .length = sizeof(placement) };

        if (!GetWindowPlacement(hwnd, &placement)) {
            return;
        }

        RECT* normal = &placement.rcNormalPosition;

        OffsetRect(normal, left - normal->left, 0);
        placement.showCmd = SW_SHOWMINNOACTIVE;
        placement.flags |= WPF_ASYNCWINDOWPLACEMENT;
        SetWindowPlacement(hwnd, &placement);

        return;
    }

    RECT rect;

    if (!GetWindowRect(hwnd, &rect)) {
        return;
    }

    SetWindowPos(hwnd, NULL, left, rect.top, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

/* Parking is only done when it can be recorded, so it can always be undone. */
static void mora_park_window(tracked_window_t* window)
{
    RECT rect;

    if (!GetWindowRect(window->hwnd, &rect) ||
        !SetPropW(window->hwnd, MORA_PROP_PARKED_FROM, mora_encode_prop(rect.left))) {
        return;
    }

    window->parked = TRUE;
    mora_move_window_left_edge(window->hwnd, mora_parking_left());
}

static void mora_hide_window(tracked_window_t* window)
{
    if (!SetPropW(window->hwnd, MORA_PROP_HIDDEN, mora_encode_prop(window->minimized))) {
        return;
    }

    ShowWindow(window->hwnd, SW_HIDE);
    window->hidden_by_workspace = !IsWindowVisible(window->hwnd);

    if (!window->hidden_by_workspace) {
        RemovePropW(window->hwnd, MORA_PROP_HIDDEN);
    }
}

/* UIPI denies moving or showing an elevated window, but lets a posted
 * WM_SYSCOMMAND through: it is minimized instead of parked. Nothing is recorded
 * on the window, which UIPI also denies, so a crash leaves it minimized. */
static void mora_minimize_elevated_window(tracked_window_t* window)
{
    if (window->minimized_by_workspace || !IsWindowVisible(window->hwnd) || IsIconic(window->hwnd)) {
        return;
    }

    window->minimized_by_workspace = PostMessageW(window->hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

static void mora_restore_elevated_window(tracked_window_t* window)
{
    if (!window->minimized_by_workspace) {
        return;
    }

    window->minimized_by_workspace = FALSE;
    /* Restored by the user meanwhile, e.g. through Alt+Tab. */
    if (!IsIconic(window->hwnd)) {
        return;
    }

    if (PostMessageW(window->hwnd, WM_SYSCOMMAND, SC_RESTORE, 0)) {
        window->restored_at = GetTickCount64();
    }
}

static void mora_sync_inactive_window(tracked_window_t* window)
{
    window->tileable = FALSE;

    if (window->elevated) {
        mora_minimize_elevated_window(window);

        return;
    }

    if (window->parked || !IsWindowVisible(window->hwnd)) {
        return;
    }

    window->minimized = IsIconic(window->hwnd);

    if (window->minimized) {
        mora_hide_window(window);
    }
    else {
        mora_park_window(window);
    }
}

/* Unparking returns the window to where it was parked from, which is usually
 * its tile, so the following retile has nothing left to move. Styling stays. */
static void mora_bring_back_window(tracked_window_t* window)
{
    HANDLE hidden = mora_undo_placement_props(window->hwnd);

    window->parked = FALSE;

    if (hidden) {
        window->hidden_by_workspace = !IsWindowVisible(window->hwnd);

        if (window->hidden_by_workspace) {
            (void)SetPropW(window->hwnd, MORA_PROP_HIDDEN, hidden);
        }
    }
}

static void mora_sync_active_window(tracked_window_t* window)
{
    if (window->elevated) {
        mora_restore_elevated_window(window);
        window->tileable = mora_is_workspace_candidate(window->hwnd);

        return;
    }

    if (window->overflow && window->hidden_by_workspace && !window->minimized) {
        window->tileable = !IsIconic(window->hwnd);

        return;
    }

    if ((window->parked && !window->overflow) || window->hidden_by_workspace) {
        mora_bring_back_window(window);
    }

    if (!window->hidden_by_workspace) {
        window->minimized = IsIconic(window->hwnd);
    }

    window->tileable = !window->hidden_by_workspace && mora_is_tiling_candidate(window->hwnd);
}

void mora_repark_windows(void)
{
    LONG left = mora_parking_left();

    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        tracked_window_t* window = mora_window(i);

        if (window->parked) {
            mora_move_window_left_edge(window->hwnd, left);
        }
    }
}

void mora_set_window_overflow(tracked_window_t* window, BOOL overflow)
{
    window->overflow = overflow;

    if (overflow) {
        if (!window->parked) {
            mora_park_window(window);
        }

        window->placement.verify = !window->hidden_by_workspace;
    } else if ((window->parked || window->hidden_by_workspace) &&
        mora_workspace_is_active(&g_state, window->workspace_index)) {
        mora_bring_back_window(window);
    }
}

void mora_verify_overflow_parking(tracked_window_t* window)
{
    if (!IsWindowVisible(window->hwnd) || IsIconic(window->hwnd)) {
        return;
    }

    if (MonitorFromWindow(window->hwnd, MONITOR_DEFAULTTONULL)) {
        /* Some apps reject position changes as well as sizes. Hiding is a
         * recoverable last resort; the stack indicator and cycling still work.
         * Elevated windows never get here; they are never in the layout. */
        mora_log_window(window->hwnd, L"overflow", L"refused to move off-screen; hidden");
        mora_hide_window(window);
    }
}

/* Reveal the windows we moved away on active workspaces, park or hide windows
 * on inactive ones, and re-read native state instead of trusting delayed
 * events. Revealing first means the desktop never shows through a switch. */
void mora_sync_workspace_visibility(void)
{
    for (mora_index_t i = 0; i < g_state.windows.count;) {
        tracked_window_t* window = mora_window(i);

        if (!IsWindow(window->hwnd)) {
            mora_log(L"untrack %p: gone", (void*)window->hwnd);
            mora_workspace_remove_window(&g_state, window->hwnd);
            continue;
        }

        if (mora_workspace_is_active(&g_state, window->workspace_index)) {
            mora_sync_active_window(window);
        }

        ++i;
    }

    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        tracked_window_t* window = mora_window(i);

        if (!mora_workspace_is_active(&g_state, window->workspace_index)) {
            mora_sync_inactive_window(window);
        }
    }
}

void mora_manager_restore_windows(void)
{
    for (mora_index_t i = 0; i < g_state.windows.count; ++i) {
        tracked_window_t* window = mora_window(i);

        mora_restore_elevated_window(window);
        mora_release_window(window->hwnd);
        window->parked = FALSE;
        window->overflow = FALSE;
        window->placement.verify = FALSE;
        window->hidden_by_workspace = FALSE;
    }
}
