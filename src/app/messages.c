#include "app.h"

#include "bar/bar.h"

#pragma comment(lib, "user32.lib")

LRESULT mora_handle_bar_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, BOOL* handled)
{
    (void)lParam;
    *handled = TRUE;

    switch (msg) {
    case WM_HOTKEY: {
        HWND foreground = GetForegroundWindow();

        (void)mora_manager_dispatch_hotkey((size_t)wParam, foreground, mora_manager_hotkey_monitor(foreground));
        mora_bars_refresh();

        return 0;
    }

    case WM_CLOSE:
        /* Sent by `taskkill` without /F: exit through the normal cleanup. */
        mora_log(L"quit: asked to close");
        PostQuitMessage(0);

        return 0;

    case WM_ENDSESSION:
        if (wParam) {
            mora_log(L"quit: session ending");
            mora_restore_desktop();
        }

        return 0;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA) {
            mora_request_monitor_refresh();
        }

        *handled = FALSE;

        return 0;

    case WM_DISPLAYCHANGE:
        mora_request_monitor_refresh();

        return 0;

    case WM_DPICHANGED:
        /* Bars are resized with their monitor's new scale instead. */
        mora_request_monitor_refresh();

        return 0;

    case MORA_WM_RETILE:
        mora_events_on_retile(hwnd);

        return 0;

    case WM_TIMER:
        if (mora_events_on_timer(hwnd, wParam)) {
            return 0;
        }
        break;
    }

    *handled = FALSE;

    return 0;
}
