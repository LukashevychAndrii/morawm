#include "internal.h"

#include "detection/window_detection.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "user32.lib")

mora_bar_t g_bars[MORA_WORKSPACE_COUNT];
size_t g_bar_count;
mora_bar_message_handler_t g_bar_handler;
mora_media_t g_bar_media;
static HINSTANCE g_instance;

static UINT mora_monitor_dpi(const mora_monitor_t* monitor)
{
    return monitor->dpi ? monitor->dpi : mora_dpi_of_monitor(monitor->hmonitor);
}

static void mora_free_fonts(mora_bar_t* bar)
{
    if (bar->label_font) {
        DeleteObject(bar->label_font);
    }

    if (bar->title_font) {
        DeleteObject(bar->title_font);
    }

    if (bar->symbol_font) {
        DeleteObject(bar->symbol_font);
    }

    bar->label_font = bar->title_font = bar->symbol_font = NULL;
}

/* Metrics and fonts for the monitor's scale, rebuilt only when it changed. */
static BOOL mora_set_bar_dpi(mora_bar_t* bar, UINT dpi)
{
    if (bar->label_font && bar->dpi == dpi) {
        return TRUE;
    }

    mora_free_fonts(bar);
    bar->dpi = dpi;
    bar->metrics = mora_bar_metrics(dpi);
    bar->label_font = mora_create_font(bar->metrics.font_height, FW_NORMAL);
    bar->title_font = mora_create_font(bar->metrics.title_height, FW_LIGHT);
    bar->symbol_font = mora_create_symbol_font(bar->metrics.button_glyph);

    return bar->label_font && bar->title_font && bar->symbol_font;
}

static void mora_apply_window_style(HWND hwnd)
{
    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;

    (void)DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    COLORREF border = DWMWA_COLOR_NONE;

    (void)DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

static BOOL mora_create_bar(mora_bar_t* bar, mora_monitor_t* monitor)
{
    if (!mora_set_bar_dpi(bar, mora_monitor_dpi(monitor))) {
        return FALSE;
    }

    const RECT* area = &monitor->work_area;
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        MORA_BAR_CLASS_NAME, L"MoraWM bar", WS_POPUP,
        area->left, area->top, area->right - area->left, bar->metrics.height,
        NULL, NULL, g_instance, bar);

    if (!hwnd) {
        return FALSE;
    }

    mora_apply_window_style(hwnd);
    monitor->bar_hwnd = hwnd;
    monitor->reserved_top = bar->metrics.height;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    return TRUE;
}

static void mora_destroy_bar(mora_bar_t* bar)
{
    if (bar->hwnd) {
        DestroyWindow(bar->hwnd);
    }

    mora_free_fonts(bar);
    *bar = (mora_bar_t){ 0 };
}

static BOOL mora_add_bar(mora_monitor_t* monitor)
{
    mora_bar_t* bar = &g_bars[g_bar_count];

    *bar = (mora_bar_t){ .monitor = (mora_index_t)g_bar_count, .hot_workspace = MORA_INVALID_INDEX,
        .hot_button = -1 };
    ++g_bar_count;

    if (mora_create_bar(bar, monitor)) {
        return TRUE;
    }

    mora_destroy_bar(bar);
    --g_bar_count;

    return FALSE;
}

BOOL mora_bars_create(HINSTANCE instance, mora_monitor_t* monitors, size_t count,
    mora_bar_message_handler_t handler)
{
    if (!monitors || count == 0 || count > _countof(g_bars) || g_bar_count) {
        return FALSE;
    }

    g_instance = instance;
    g_bar_handler = handler;
    mora_bar_fonts_init();

    WNDCLASSW wc = {
        .lpfnWndProc = mora_bar_wnd_proc,
        .hInstance = instance,
        .lpszClassName = MORA_BAR_CLASS_NAME,
        .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
    };

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return FALSE;
    }

    for (size_t i = 0; i < count; ++i) {
        if (!mora_add_bar(&monitors[i])) {
            mora_bars_destroy();

            return FALSE;
        }
    }

    mora_schedule_clock();
    mora_media_start(g_bars[0].hwnd, MORA_BAR_WM_MEDIA);

    return TRUE;
}

void mora_bars_destroy(void)
{
    mora_pick_overlays_destroy();
    mora_media_stop();
    g_bar_media = (mora_media_t){ 0 };
    mora_icons_clear();

    for (size_t i = 0; i < g_bar_count; ++i) {
        mora_destroy_bar(&g_bars[i]);
    }

    g_bar_count = 0;
    g_bar_handler = NULL;

    if (g_instance) {
        UnregisterClassW(MORA_BAR_CLASS_NAME, g_instance);
        g_instance = NULL;
    }
}

BOOL mora_bars_set_monitors(mora_monitor_t* monitors, size_t count)
{
    if (!monitors || count == 0 || count > _countof(g_bars) || g_bar_count == 0) {
        return FALSE;
    }

    while (g_bar_count > count) {
        mora_destroy_bar(&g_bars[--g_bar_count]);
    }

    BOOL complete = TRUE;

    for (size_t i = 0; i < count; ++i) {
        mora_monitor_t* monitor = &monitors[i];

        if (i >= g_bar_count) {
            if (i > g_bar_count || !mora_add_bar(monitor)) {
                complete = FALSE;
            }
            continue;
        }

        mora_bar_t* bar = &g_bars[i];

        (void)mora_set_bar_dpi(bar, mora_monitor_dpi(monitor));
        const RECT* area = &monitor->work_area;

        SetWindowPos(bar->hwnd, HWND_TOPMOST, area->left, area->top, area->right - area->left,
            bar->metrics.height, SWP_NOACTIVATE);
        monitor->bar_hwnd = bar->hwnd;
        monitor->reserved_top = bar->metrics.height;
        InvalidateRect(bar->hwnd, NULL, FALSE);
    }

    return complete;
}

void mora_bars_sync_fullscreen(void)
{
    HWND foreground = GetForegroundWindow();
    HMONITOR fullscreen = mora_is_fullscreen_window(foreground) ?
        MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) : NULL;

    for (size_t i = 0; i < g_bar_count; ++i) {
        HWND hwnd = g_bars[i].hwnd;
        BOOL hide = fullscreen && MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) == fullscreen;

        if (hide == !IsWindowVisible(hwnd)) {
            continue;
        }

        if (hide) {
            ShowWindow(hwnd, SW_HIDE);
        }
        else {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
}

void mora_bars_refresh(void)
{
    mora_pick_overlays_sync(g_instance);
    mora_bars_sync_fullscreen();

    for (size_t i = 0; i < g_bar_count; ++i) {
        InvalidateRect(g_bars[i].hwnd, NULL, FALSE);
    }
}

void mora_bars_refresh_title(HWND hwnd)
{
    const mora_workspace_state_t* state = mora_manager_state();
    mora_index_t index = mora_manager_find_window(hwnd);

    if (index == MORA_INVALID_INDEX) {
        return;
    }

    mora_index_t workspace = state->windows.entries[index].workspace_index;

    if (!mora_workspace_is_active(state, workspace)) {
        return;
    }

    for (size_t i = 0; i < g_bar_count; ++i) {
        if (g_bars[i].monitor == state->workspaces[workspace].monitor_index) {
            InvalidateRect(g_bars[i].hwnd, NULL, FALSE);
        }
    }
}
