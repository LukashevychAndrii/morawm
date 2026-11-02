#include "internal.h"

#pragma comment(lib, "user32.lib")

typedef struct {
    mora_window_detect_callback_t on_window;
    void* context;
} mora_detect_windows_context_t;

static BOOL CALLBACK mora_enum_windows_proc(HWND hwnd, LPARAM lParam)
{
    const mora_detect_windows_context_t* context = (const mora_detect_windows_context_t*)lParam;

    if (!mora_is_workspace_candidate(hwnd)) {
        return TRUE;
    }

    return context->on_window(hwnd, context->context);
}

void mora_detect_windows(mora_window_detect_callback_t on_window, void* context)
{
    mora_detect_windows_context_t enum_context = {
        .on_window = on_window,
        .context = context,
    };
    EnumWindows(mora_enum_windows_proc, (LPARAM)&enum_context);
}

mora_window_filter_result_t mora_classify_window(HWND hwnd)
{
    mora_window_snapshot_t snapshot = mora_get_window_snapshot(hwnd);
    mora_window_filter_result_t result = mora_window_filter_snapshot(&snapshot);

    if (result != MORA_WINDOW_FILTER_ACCEPTED) {
        return result;
    }

    mora_read_process_info(hwnd, &snapshot);

    return mora_window_filter_snapshot(&snapshot);
}

BOOL mora_is_tiling_candidate(HWND hwnd)
{
    mora_window_filter_result_t result = mora_classify_window(hwnd);

    return result == MORA_WINDOW_FILTER_ACCEPTED || result == MORA_WINDOW_FILTER_UWP_PENDING;
}

BOOL mora_is_workspace_candidate(HWND hwnd)
{
    mora_window_filter_result_t result = mora_classify_window(hwnd);

    return result == MORA_WINDOW_FILTER_ACCEPTED || result == MORA_WINDOW_FILTER_UWP_PENDING ||
        result == MORA_WINDOW_FILTER_ELEVATED;
}

BOOL mora_is_fullscreen_window(HWND hwnd)
{
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd) || IsZoomed(hwnd) ||
        (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
        return FALSE;
    }

    RECT rect;
    MONITORINFO info = { .cbSize = sizeof(info) };
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);

    if (!monitor || !GetMonitorInfoW(monitor, &info) || !GetWindowRect(hwnd, &rect)) {
        return FALSE;
    }

    if (rect.left > info.rcMonitor.left || rect.top > info.rcMonitor.top ||
        rect.right < info.rcMonitor.right || rect.bottom < info.rcMonitor.bottom) {
        return FALSE;
    }

    wchar_t class_name[MORA_CLASS_NAME_CAPACITY];

    return !GetClassNameW(hwnd, class_name, _countof(class_name)) ||
        !mora_is_class_name_blacklisted(class_name);
}
