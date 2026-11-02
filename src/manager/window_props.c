#include "internal.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

/* Every change MoraWM makes to another application's window is recorded in a
 * window property. Properties outlive MoraWM, so after a crash or a forced kill
 * the next start finds and undoes them (mora_manager_recover_windows). */

static const wchar_t MORA_PROP_STYLED[] = L"MoraWM.Styled";
const wchar_t MORA_PROP_PARKED_FROM[] = L"MoraWM.ParkedFrom";
const wchar_t MORA_PROP_HIDDEN[] = L"MoraWM.Hidden";

/* A NULL property means "absent", so values are shifted and tagged with 1. */
HANDLE mora_encode_prop(LONG value)
{
    return (HANDLE)(((INT_PTR)value << 1) | 1);
}

LONG mora_decode_prop(HANDLE prop)
{
    return (LONG)((INT_PTR)prop >> 1);
}

static void mora_set_window_style(HWND hwnd, BOOL managed)
{
    BOOL no_transitions = managed;
    DWM_WINDOW_CORNER_PREFERENCE corners = managed ? DWMWCP_DONOTROUND : DWMWCP_DEFAULT;

    (void)DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &no_transitions, sizeof(no_transitions));
    (void)DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    /* The 1px border would show as a seam between tiles; tiling grows each
     * window by that pixel so the content meets its neighbour instead. */
    COLORREF border = managed ? DWMWA_COLOR_NONE : DWMWA_COLOR_DEFAULT;

    (void)DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

/* DWM fades windows in and out when they are shown and hidden; managed windows
 * skip that so workspace switches are instant. Tiles are square. */
void mora_style_window(HWND hwnd)
{
    mora_set_window_style(hwnd, TRUE);
    (void)SetPropW(hwnd, MORA_PROP_STYLED, mora_encode_prop(TRUE));
}

HANDLE mora_undo_placement_props(HWND hwnd)
{
    HANDLE parked_from = RemovePropW(hwnd, MORA_PROP_PARKED_FROM);

    if (parked_from) {
        mora_move_window_left_edge(hwnd, mora_decode_prop(parked_from));
    }

    HANDLE hidden = RemovePropW(hwnd, MORA_PROP_HIDDEN);

    if (hidden) {
        ShowWindow(hwnd, mora_decode_prop(hidden) ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
    }

    return hidden;
}

void mora_release_window(HWND hwnd)
{
    (void)mora_undo_placement_props(hwnd);

    if (RemovePropW(hwnd, MORA_PROP_STYLED)) {
        mora_set_window_style(hwnd, FALSE);
    }
}

static BOOL CALLBACK mora_recover_window_proc(HWND hwnd, LPARAM data)
{
    (void)data;
    mora_release_window(hwnd);

    return TRUE;
}

void mora_manager_recover_windows(void)
{
    EnumWindows(mora_recover_window_proc, 0);
}
