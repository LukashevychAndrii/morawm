#include "app.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "bar/bar.h"
#include "common/debug.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "user32.lib")

/* Monitors beyond capacity are only counted, so too many can be reported. */
static mora_monitor_t g_monitors[MORA_WORKSPACE_COUNT];
static size_t g_monitor_count = 0;

mora_monitor_t* mora_monitors(size_t* count)
{
    *count = g_monitor_count;

    return g_monitors;
}

void mora_forget_monitors(void)
{
    g_monitor_count = 0;
}

HWND mora_main_hwnd(void)
{
    return g_monitor_count ? g_monitors[0].bar_hwnd : NULL;
}

static const wchar_t* const MORA_TASKBAR_CLASSES[] = {
    L"Shell_TrayWnd",
    L"Shell_SecondaryTrayWnd",
};

BOOL mora_is_taskbar(HWND hwnd)
{
    wchar_t name[MORA_CLASS_NAME_CAPACITY];

    if (!GetClassNameW(hwnd, name, _countof(name))) {
        return FALSE;
    }

    for (size_t i = 0; i < _countof(MORA_TASKBAR_CLASSES); ++i) {
        if (wcscmp(name, MORA_TASKBAR_CLASSES[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL mora_monitor_has_visible_taskbar(HMONITOR hmonitor)
{
    for (size_t c = 0; c < _countof(MORA_TASKBAR_CLASSES); ++c) {
        for (HWND taskbar = FindWindowExW(NULL, NULL, MORA_TASKBAR_CLASSES[c], NULL); taskbar;
            taskbar = FindWindowExW(NULL, taskbar, MORA_TASKBAR_CLASSES[c], NULL)) {
            BOOL cloaked = FALSE;

            (void)DwmGetWindowAttribute(taskbar, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));

            if (IsWindowVisible(taskbar) && !cloaked &&
                MonitorFromWindow(taskbar, MONITOR_DEFAULTTONULL) == hmonitor) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* The work area already leaves out a visible taskbar and includes an
 * auto-hidden one. A taskbar hidden some other way (by a tool, or with none on
 * this monitor) can still hold its space, so the whole monitor is used then. */
static RECT mora_usable_area(HMONITOR hmonitor)
{
    MONITORINFO info = { .cbSize = sizeof(info) };

    if (!GetMonitorInfoW(hmonitor, &info)) {
        return (RECT){ 0 };
    }

    return mora_monitor_has_visible_taskbar(hmonitor) ? info.rcWork : info.rcMonitor;
}

typedef struct {
    mora_monitor_t* monitors;
    size_t count;
} mora_monitor_list_t;

static BOOL CALLBACK mora_monitor_enum_proc(HMONITOR hmonitor, HDC hdc, LPRECT rect, LPARAM data)
{
    (void)hdc; (void)rect;
    mora_monitor_list_t* list = (mora_monitor_list_t*)data;
    MONITORINFOEXW info = { .cbSize = sizeof(info) };
    RECT area = mora_usable_area(hmonitor);

    if (IsRectEmpty(&area) || !GetMonitorInfoW(hmonitor, (MONITORINFO*)&info)) {
        return TRUE;
    }

    if (list->count < MORA_WORKSPACE_COUNT) {
        UINT dpi = mora_dpi_of_monitor(hmonitor);

        mora_monitor_t* monitor = &list->monitors[list->count];

        *monitor = (mora_monitor_t){ .hmonitor = hmonitor, .work_area = area, .dpi = dpi,
            .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0 };
        wcscpy_s(monitor->device, _countof(monitor->device), info.szDevice);
    }

    ++list->count;

    return TRUE;
}

static int mora_compare_monitors(const void* a, const void* b)
{
    const RECT* first = &((const mora_monitor_t*)a)->work_area;
    const RECT* second = &((const mora_monitor_t*)b)->work_area;

    if (first->left != second->left) {
        return first->left < second->left ? -1 : 1;
    }

    return (first->top > second->top) - (first->top < second->top);
}

static size_t mora_enumerate_monitors(mora_monitor_t monitors[MORA_WORKSPACE_COUNT])
{
    mora_monitor_list_t list = { .monitors = monitors };

    EnumDisplayMonitors(NULL, NULL, mora_monitor_enum_proc, (LPARAM)&list);

    if (list.count <= MORA_WORKSPACE_COUNT) {
        qsort(monitors, list.count, sizeof(*monitors), mora_compare_monitors);
    }

    return list.count;
}

static void mora_log_monitors(void)
{
    for (size_t i = 0; i < g_monitor_count; i++) {
        const RECT* area = &g_monitors[i].work_area;

        mora_log(L"monitor %zu %ls%ls: usable %ld,%ld to %ld,%ld at %u DPI", i + 1, g_monitors[i].device,
            g_monitors[i].primary ? L" (primary)" : L"", area->left, area->top, area->right, area->bottom,
            g_monitors[i].dpi);
    }
}

BOOL mora_detect_monitors(void)
{
    g_monitor_count = mora_enumerate_monitors(g_monitors);

    if (g_monitor_count == 0) {
        mora_show_error(L"No monitors detected", L"MoraWM could not detect any monitors on your system.");

        return FALSE;
    }

    if (g_monitor_count > _countof(g_monitors)) {
        mora_show_error(L"Too many monitors", L"MoraWM supports one to nine monitors.");
        g_monitor_count = 0;

        return FALSE;
    }

    mora_log_monitors();

    return TRUE;
}

static BOOL mora_same_monitor(const mora_monitor_t* a, const mora_monitor_t* b)
{
    return a->hmonitor == b->hmonitor && a->dpi == b->dpi && a->primary == b->primary &&
        wcscmp(a->device, b->device) == 0 && EqualRect(&a->work_area, &b->work_area);
}

void mora_refresh_monitors(void)
{
    mora_monitor_t found[MORA_WORKSPACE_COUNT];
    size_t count = mora_enumerate_monitors(found);

    /* None while every display is off or the session is switching: keep the
     * current layout until displays return. More than supported: ignore. */
    if (count == 0 || count > _countof(found) || !g_manager_initialized) {
        return;
    }

    BOOL changed = count != g_monitor_count;

    for (size_t i = 0; i < count && !changed; ++i) {
        changed = !mora_same_monitor(&found[i], &g_monitors[i]);
    }

    if (!changed) {
        return;
    }

    mora_log(L"displays changed: %zu monitor%ls", count, count == 1 ? L"" : L"s");

    ZeroMemory(g_monitors, sizeof(g_monitors));
    memcpy(g_monitors, found, count * sizeof(*found));
    g_monitor_count = count;
    mora_log_monitors();
    /* Bars first: the manager keeps their space free and focuses them. */
    (void)mora_bars_set_monitors(g_monitors, count);
    (void)mora_manager_set_monitors(g_monitors, count);
    mora_bars_refresh();
}
