#include "internal.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include "detection/window_detection.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "user32.lib")

#define MORA_PICK_CLASS_NAME L"MORA_PICK_CLASS"
#define MORA_PICK_BADGE 44
#define MORA_PICK_FRAME 2
#define MORA_PICK_LIP_DIVISOR 14

typedef struct {
    HWND hwnd;
    mora_pick_target_t target;
} mora_overlay_t;

static mora_overlay_t g_overlays[MORA_PICK_CAPACITY + 1];
static size_t g_overlay_count;
static HWND g_overlay_held;
static unsigned g_overlay_serial;
static BOOL g_class_registered;

static BOOL mora_visible_bounds(HWND hwnd, RECT* bounds)
{
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return FALSE;
    }

    if (!mora_window_bounds(hwnd, bounds)) {
        return FALSE;
    }

    return MonitorFromRect(bounds, MONITOR_DEFAULTTONULL) != NULL;
}

static int mora_scale_for(RECT bounds, int value)
{
    HMONITOR monitor = MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST);

    return mora_scale(value, mora_dpi_of_monitor(monitor));
}

static void mora_paint_badge(HWND hwnd, const mora_overlay_t* overlay)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;

    GetClientRect(hwnd, &client);

    if (overlay && overlay->target.hwnd) {
        int lip = max(2, client.bottom / MORA_PICK_LIP_DIVISOR);
        RECT face = { 0, 0, client.right, client.bottom - lip };

        mora_fill_rect(dc, client, THEME.key_edge);
        mora_fill_rect(dc, face, THEME.key);
        HFONT font = mora_create_font(client.bottom / 2, FW_SEMIBOLD);
        HGDIOBJ old_font = SelectObject(dc, font);

        SetBkMode(dc, TRANSPARENT);
        wchar_t text[2] = { overlay->target.key, L'\0' };

        mora_draw_text(dc, text, face, THEME.text, DT_CENTER);
        SelectObject(dc, old_font);

        if (font) {
            DeleteObject(font);
        }
    } else {
        mora_fill_rect(dc, client, THEME.frame);
    }

    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK mora_pick_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const mora_overlay_t* overlay = NULL;

    for (size_t i = 0; i < g_overlay_count; ++i) {
        if (g_overlays[i].hwnd == hwnd) {
            overlay = &g_overlays[i];
        }
    }

    switch (msg) {
    case WM_PAINT:
        mora_paint_badge(hwnd, overlay);

        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONUP:
        if (overlay && overlay->target.hwnd) {
            (void)mora_manager_drop_on_window(overlay->target.hwnd);
            mora_bars_refresh();
        }

        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND mora_create_overlay(HINSTANCE instance, RECT rect, BOOL click_through)
{
    DWORD style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    if (click_through) {
        style |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    }

    HWND hwnd = CreateWindowExW(style, MORA_PICK_CLASS_NAME, L"MoraWM pick", WS_POPUP,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, instance, NULL);

    if (!hwnd) {
        return NULL;
    }

    if (click_through) {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }

    BOOL no_transitions = TRUE;

    (void)DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &no_transitions, sizeof(no_transitions));

    return hwnd;
}

static void mora_add_frame(HINSTANCE instance, HWND held)
{
    RECT bounds;

    if (!mora_visible_bounds(held, &bounds)) {
        return;
    }

    HWND hwnd = mora_create_overlay(instance, bounds, TRUE);

    if (!hwnd) {
        return;
    }

    int width = mora_scale_for(bounds, MORA_PICK_FRAME);
    LONG w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
    HRGN outer = CreateRectRgn(0, 0, w, h);
    HRGN inner = CreateRectRgn(width, width, w - width, h - width);

    if (outer && inner && CombineRgn(outer, outer, inner, RGN_DIFF) != ERROR &&
        SetWindowRgn(hwnd, outer, FALSE)) {
        outer = NULL;
    }

    /* The window owns it now. */
    if (outer) {
        DeleteObject(outer);
    }

    if (inner) {
        DeleteObject(inner);
    }

    g_overlays[g_overlay_count++] = (mora_overlay_t){ .hwnd = hwnd };
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
}

static void mora_add_badge(HINSTANCE instance, HWND target, wchar_t key)
{
    RECT bounds;

    if (!mora_visible_bounds(target, &bounds)) {
        return;
    }

    int size = mora_scale_for(bounds, MORA_PICK_BADGE);
    POINT center = mora_rect_center(bounds);
    RECT rect = { center.x - size / 2, center.y - size / 2, center.x - size / 2 + size, center.y - size / 2 + size };
    HWND hwnd = mora_create_overlay(instance, rect, FALSE);

    if (!hwnd) {
        return;
    }

    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;

    (void)DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    g_overlays[g_overlay_count++] = (mora_overlay_t){
        .hwnd = hwnd, .target = { .hwnd = target, .key = key } };
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
}

void mora_pick_overlays_destroy(void)
{
    for (size_t i = 0; i < g_overlay_count; ++i) {
        DestroyWindow(g_overlays[i].hwnd);
    }

    g_overlay_count = 0;
    g_overlay_held = NULL;
}

void mora_pick_overlays_sync(HINSTANCE instance)
{
    const mora_pick_t* pick = mora_manager_pick();

    if (pick->held == g_overlay_held && pick->serial == g_overlay_serial) {
        return;
    }

    mora_pick_overlays_destroy();

    if (!pick->held || !instance) {
        return;
    }

    if (!g_class_registered) {
        WNDCLASSW wc = {
            .lpfnWndProc = mora_pick_wnd_proc,
            .hInstance = instance,
            .lpszClassName = MORA_PICK_CLASS_NAME,
            .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND),
        };
        g_class_registered = RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

        if (!g_class_registered) {
            return;
        }
    }

    g_overlay_held = pick->held;
    g_overlay_serial = pick->serial;
    mora_add_frame(instance, pick->held);

    for (size_t i = 0; i < pick->target_count; ++i) {
        mora_add_badge(instance, pick->targets[i].hwnd, pick->targets[i].key);
    }
}
