#pragma once

#include <Windows.h>

typedef enum {
    MORA_WINDOW_FILTER_ACCEPTED = 0,
    MORA_WINDOW_FILTER_NOT_VISIBLE,
    MORA_WINDOW_FILTER_HUNG,
    MORA_WINDOW_FILTER_MINIMIZED,
    MORA_WINDOW_FILTER_SYSTEM_WINDOW,
    MORA_WINDOW_FILTER_CLASS_BLACKLISTED,
    MORA_WINDOW_FILTER_NON_ROOT,
    MORA_WINDOW_FILTER_CHILD,
    MORA_WINDOW_FILTER_POPUP,
    MORA_WINDOW_FILTER_OWNED,
    MORA_WINDOW_FILTER_TOOL,
    MORA_WINDOW_FILTER_NOACTIVATE,
    MORA_WINDOW_FILTER_CLOAKED,
    MORA_WINDOW_FILTER_EMPTY_RECT,
    MORA_WINDOW_FILTER_OTHER_DESKTOP,
    MORA_WINDOW_FILTER_PROCESS_BLACKLISTED,
    /* Owned by a process with a higher integrity level than MoraWM, which
     * Windows does not let MoraWM move or hide. */
    MORA_WINDOW_FILTER_ELEVATED,
    /* Accepted so far, but a UWP frame whose hosted app is not attached yet.
     * Classify again shortly; treat as accepted if it never resolves. */
    MORA_WINDOW_FILTER_UWP_PENDING,
} mora_window_filter_result_t;

typedef struct {
    BOOL is_visible;
    BOOL is_hung;
    BOOL is_minimized;
    BOOL is_system_window;
    BOOL is_class_blacklisted;
    BOOL is_process_blacklisted;
    BOOL is_process_pending;
    BOOL is_elevated;
    BOOL is_root_window;
    BOOL has_visible_owner;
    BOOL is_cloaked;
    BOOL is_core_window;
    BOOL is_on_current_desktop;
    BOOL has_rect;
    LONG_PTR style;
    LONG_PTR ex_style;
    RECT rect;
} mora_window_snapshot_t;

typedef BOOL (*mora_window_detect_callback_t)(HWND hwnd, void* context);

mora_window_filter_result_t mora_window_filter_snapshot(const mora_window_snapshot_t* snapshot);
const wchar_t* mora_window_filter_name(mora_window_filter_result_t result);
void mora_detect_windows(mora_window_detect_callback_t on_window, void* context);
BOOL mora_is_tiling_candidate(HWND hwnd);
BOOL mora_is_workspace_candidate(HWND hwnd);
/* Owned by a process with a higher integrity level than MoraWM. */
BOOL mora_is_window_elevated(HWND hwnd);
/* Covers its whole monitor without a caption and is not maximized: F11
 * browsers, video players, borderless games. Shell windows never count. */
BOOL mora_is_fullscreen_window(HWND hwnd);
mora_window_filter_result_t mora_classify_window(HWND hwnd);

BOOL mora_is_class_name_blacklisted(const wchar_t* class_name);
BOOL mora_is_process_path_blacklisted(const wchar_t* process_path);

/* "C:\dir\app.exe" -> "app.exe"; handles both separators. */
const wchar_t* mora_file_name(const wchar_t* path);
BOOL mora_is_app_frame_host_path(const wchar_t* path);
BOOL mora_process_image_path(DWORD pid, wchar_t* path, size_t capacity);
BOOL mora_window_image_path(HWND hwnd, wchar_t* path, size_t capacity);
BOOL mora_window_bounds(HWND hwnd, RECT* bounds);
