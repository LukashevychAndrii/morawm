#include "internal.h"

#pragma comment(lib, "user32.lib")

static mora_index_t mora_bar_hit(const mora_bar_t* bar, int x, int y)
{
    return mora_bar_hit_workspace(&bar->metrics, bar->slot_icons, x, y);
}

static void mora_set_hot_workspace(mora_bar_t* bar, mora_index_t workspace)
{
    if (bar->hot_workspace == workspace) {
        return;
    }

    bar->hot_workspace = workspace;
    InvalidateRect(bar->hwnd, NULL, FALSE);
}

static void mora_set_hot_chip(mora_bar_t* bar, HWND hwnd)
{
    if (bar->hot_chip == hwnd) {
        return;
    }

    bar->hot_chip = hwnd;
    InvalidateRect(bar->hwnd, NULL, FALSE);
}

static void mora_bar_click_while_holding(mora_bar_t* bar, int x, int y)
{
    HWND chip = mora_bar_chip_hit(bar, x, y);
    mora_index_t workspace = mora_bar_hit(bar, x, y);

    if (chip && mora_manager_pick_key_of(chip)) {
        (void)mora_manager_drop_on_window(chip);
    }
    else if (workspace != MORA_INVALID_INDEX) {
        (void)mora_manager_drop_on_workspace(workspace);
    }
    else {
        mora_manager_cancel_pick();
    }

    mora_bars_refresh();
}

static void mora_set_hot_button(mora_bar_t* bar, int button)
{
    if (bar->hot_button == button) {
        return;
    }

    bar->hot_button = button;
    InvalidateRect(bar->hwnd, NULL, FALSE);
}

LRESULT CALLBACK mora_bar_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        mora_bar_t* created = (mora_bar_t*)((const CREATESTRUCTW*)lParam)->lpCreateParams;

        created->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created);
    }

    mora_bar_t* bar = (mora_bar_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (!bar) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_PAINT:
        /* Painting classifies windows, and a COM call in there pumps this
         * thread's queue: the same bar can get a second WM_PAINT while it
         * paints. The outer paint covers the whole bar, so the inner one is
         * dropped rather than resuming with the chip list half met. */
        if (bar->painting) {
            ValidateRect(hwnd, NULL);

            return 0;
        }

        bar->painting = TRUE;
        mora_bar_on_paint(bar);
        bar->painting = FALSE;

        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_MOUSEMOVE:
        if (!bar->tracking_mouse) {
            TRACKMOUSEEVENT track = { .cbSize = sizeof(track), .dwFlags = TME_LEAVE, .hwndTrack = hwnd };

            bar->tracking_mouse = TrackMouseEvent(&track);
        }

        mora_set_hot_workspace(bar, mora_bar_hit(bar, (short)LOWORD(lParam), (short)HIWORD(lParam)));
        mora_set_hot_button(bar, mora_bar_media_button_hit(bar, (short)LOWORD(lParam), (short)HIWORD(lParam)));
        mora_set_hot_chip(bar, mora_bar_chip_hit(bar, (short)LOWORD(lParam), (short)HIWORD(lParam)));

        return 0;

    case WM_MOUSELEAVE:
        bar->tracking_mouse = FALSE;
        mora_set_hot_chip(bar, NULL);
        mora_set_hot_workspace(bar, MORA_INVALID_INDEX);
        mora_set_hot_button(bar, -1);

        return 0;

    case WM_LBUTTONUP: {
        int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);

        if (mora_manager_pick()->held) {
            mora_bar_click_while_holding(bar, x, y);

            return 0;
        }

        int button = mora_bar_media_button_hit(bar, x, y);

        if (button >= 0) {
            mora_media_command((mora_media_command_t)button);

            return 0;
        }

        HWND chip = mora_bar_chip_hit(bar, x, y);

        if (chip) {
            (void)mora_manager_focus_window(chip);
            mora_bars_refresh();

            return 0;
        }

        mora_index_t workspace = mora_bar_hit(bar, x, y);

        if (workspace != MORA_INVALID_INDEX && mora_manager_show_workspace(bar->monitor, workspace)) {
            mora_bars_refresh();
        }

        return 0;
    }

    case MORA_BAR_WM_MEDIA:
        mora_media_get(&g_bar_media);
        mora_bars_refresh();

        return 0;

    case WM_TIMER:
        if (wParam != MORA_BAR_TIMER_CLOCK) {
            break;
        }

        mora_bars_refresh();
        mora_schedule_clock();

        return 0;

    case WM_SETTINGCHANGE:
        mora_bars_refresh();
        break;
    }

    if (g_bar_handler) {
        BOOL handled = FALSE;
        LRESULT result = g_bar_handler(hwnd, msg, wParam, lParam, &handled);

        if (handled) {
            return result;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
