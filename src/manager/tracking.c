#include "internal.h"

#include <dwmapi.h>
#include <stdarg.h>
#include <wchar.h>

#include "common/debug.h"
#include "detection/window_detection.h"
#include "log/log.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

static mora_window_router_t g_window_router;

void mora_manager_set_window_router(mora_window_router_t router)
{
    g_window_router = router;
}

BOOL mora_manager_add_window_to_workspace(mora_index_t workspace, const tracked_window_t* window)
{
    return mora_workspace_add_window(&g_state, workspace, window) != MORA_INVALID_INDEX;
}

mora_index_t mora_monitor_at_point(POINT point)
{
    for (mora_index_t i = 0; i < g_state.monitor_count; i++) {
        if (PtInRect(&g_monitors[i].work_area, point)) {
            return i;
        }
    }

    return MORA_INVALID_INDEX;
}

mora_index_t mora_monitor_index(HMONITOR monitor)
{
    for (mora_index_t i = 0; i < g_state.monitor_count; ++i) {
        if (g_monitors[i].hmonitor == monitor) {
            return i;
        }
    }

    return MORA_INVALID_INDEX;
}

void mora_describe_window(HWND hwnd, wchar_t* out, size_t capacity)
{
    wchar_t path[MAX_PATH];

    if (!mora_window_image_path(hwnd, path, _countof(path))) {
        wcscpy_s(path, _countof(path), L"?");
    }

    wchar_t class_name[MORA_CLASS_NAME_CAPACITY] = L"";

    GetClassNameW(hwnd, class_name, _countof(class_name));
    swprintf_s(out, capacity, L"%p %ls (%ls)", (void*)hwnd, mora_file_name(path), class_name);
}

void mora_log_window(HWND hwnd, const wchar_t* action, const wchar_t* detail_format, ...)
{
    wchar_t description[MORA_WINDOW_DESCRIPTION_CAPACITY];
    wchar_t detail[MORA_WINDOW_DESCRIPTION_CAPACITY];
    va_list args;

    mora_describe_window(hwnd, description, _countof(description));

    va_start(args, detail_format);
    (void)_vsnwprintf_s(detail, _countof(detail), _TRUNCATE, detail_format, args);
    va_end(args);

    if (detail[0]) {
        mora_log(L"%ls %ls: %ls", action, description, detail);
    }
    else {
        mora_log(L"%ls %ls", action, description);
    }
}

static mora_index_t mora_new_window_workspace(HWND hwnd, BOOL new_window)
{
    mora_index_t owner = mora_manager_find_window(GetWindow(hwnd, GW_OWNER));

    if (owner != MORA_INVALID_INDEX) {
        return mora_window(owner)->workspace_index;
    }

    mora_index_t work = new_window ? mora_work_workspace() : MORA_INVALID_INDEX;

    return work != MORA_INVALID_INDEX && !g_state.workspaces[work].manual ? work : MORA_INVALID_INDEX;
}

static BOOL mora_add_tracked_window(const tracked_window_t* window, mora_index_t workspace)
{
    if (!window || g_state.monitor_count == 0) {
        return FALSE;
    }

    mora_index_t routed;

    if (g_window_router && g_window_router(window, &routed) && routed < MORA_WORKSPACE_COUNT) {
        workspace = routed;
    }

    if (workspace != MORA_INVALID_INDEX) {
        return mora_manager_add_window_to_workspace(workspace, window);
    }

    RECT rect;

    if (!GetWindowRect(window->hwnd, &rect)) {
        rect = window->rect;
    }

    POINT center = mora_rect_center(rect);
    mora_index_t monitor = mora_monitor_at_point(center);

    if (monitor == MORA_INVALID_INDEX) {
        monitor = 0;
    }

    return mora_manager_add_window_to_workspace(g_state.active_workspace[monitor], window);
}

BOOL mora_manager_add_tracked_window(const tracked_window_t* window)
{
    return mora_add_tracked_window(window, MORA_INVALID_INDEX);
}

BOOL mora_manager_add_window(HWND hwnd)
{
    return mora_add_window(hwnd, FALSE);
}

BOOL mora_add_window(HWND hwnd, BOOL new_window)
{
    if (mora_manager_find_window(hwnd) != MORA_INVALID_INDEX) {
        return FALSE;
    }

    tracked_window_t window = { .hwnd = hwnd, .tileable = TRUE, .elevated = mora_is_window_elevated(hwnd) };

    if (!mora_window_bounds(hwnd, &window.rect)) {
        return FALSE;
    }

    GetWindowTextW(hwnd, window.title, _countof(window.title));
    GetWindowThreadProcessId(hwnd, &window.pid);

    if (!mora_add_tracked_window(&window, mora_new_window_workspace(hwnd, new_window))) {
        return FALSE;
    }

    if (!window.elevated) {
        mora_style_window(hwnd);
    }

    mora_index_t index = mora_manager_find_window(hwnd);

    mora_log_window(hwnd, L"track", L"on workspace %u%ls", mora_window(index)->workspace_index + 1,
        window.elevated ? L", elevated: not tiled" : L"");

    return TRUE;
}

static BOOL mora_detect_window_callback(HWND hwnd, void* context)
{
    (void)context;
    mora_manager_add_window(hwnd);

    return TRUE;
}

typedef struct {
    HWND hwnd;
    mora_window_filter_result_t result;
} mora_rejection_t;

static void mora_log_rejection(HWND hwnd, mora_window_filter_result_t result)
{
    static mora_rejection_t recent[MORA_REJECTION_LOG_CAPACITY];
    static size_t next;

    switch (result) {
    case MORA_WINDOW_FILTER_ACCEPTED: case MORA_WINDOW_FILTER_UWP_PENDING:
    case MORA_WINDOW_FILTER_NOT_VISIBLE: case MORA_WINDOW_FILTER_MINIMIZED:
    case MORA_WINDOW_FILTER_NON_ROOT: case MORA_WINDOW_FILTER_CHILD:
        return;

    default:
        break;
    }

    DWORD pid = 0;

    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == GetCurrentProcessId() || GetWindowTextLengthW(hwnd) == 0) {
        return;
    }

    for (size_t i = 0; i < _countof(recent); ++i) {
        if (recent[i].hwnd == hwnd && recent[i].result == result) {
            return;
        }
    }

    recent[next] = (mora_rejection_t){ hwnd, result };
    next = (next + 1) % _countof(recent);

    mora_log_window(hwnd, L"reject", L"%ls", mora_window_filter_name(result));
}

static BOOL CALLBACK mora_log_rejection_proc(HWND hwnd, LPARAM data)
{
    (void)data;

    if (IsWindowVisible(hwnd)) {
        mora_log_rejection(hwnd, mora_classify_window(hwnd));
    }

    return TRUE;
}

static void mora_print_windows(void)
{
    mora_debug_log(L"\n    Windows by Monitor:\n");

    for (mora_index_t m = 0; m < g_state.monitor_count; m++) {
        mora_debug_log(L"  Monitor %u:\n", m);
        size_t n = 0;

        for (mora_index_t i = 0; i < g_state.windows.count; i++) {
            const tracked_window_t* window = mora_window(i);

            if (window->workspace_index != g_state.active_workspace[m]) {
                continue;
            }

            mora_debug_log(L"    Window %zu: HWND=%p, Title=%ls, PID=%lu, Rect=(%ld,%ld)-(%ld,%ld)\n",
                n++, window->hwnd, window->title, window->pid,
                window->rect.left, window->rect.top, window->rect.right, window->rect.bottom);
        }
    }
}

void mora_manager_detect_windows(void)
{
    mora_detect_windows(mora_detect_window_callback, NULL);
    EnumWindows(mora_log_rejection_proc, 0);
    mora_workspace_remember_focus(&g_state, GetForegroundWindow());
    mora_record_foreground(GetForegroundWindow());
    mora_print_windows();
}

BOOL mora_discover_window(HWND hwnd, BOOL new_window)
{
    mora_window_filter_result_t result = mora_classify_window(hwnd);

    mora_log_rejection(hwnd, result);

    switch (result) {
    case MORA_WINDOW_FILTER_ACCEPTED:
    case MORA_WINDOW_FILTER_ELEVATED:
        return mora_add_window(hwnd, new_window);

    case MORA_WINDOW_FILTER_UWP_PENDING:
        mora_manager_defer_window(hwnd);

        return FALSE;

    default:
        return FALSE;
    }
}
