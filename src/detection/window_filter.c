#include "detection/window_detection.h"

#include <wchar.h>

/* Pure classification: decides from facts already read, never calls Win32. */

static const wchar_t* const MORA_BLACKLIST_CLASSES[] = {
    L"Shell_TrayWnd",
    L"Shell_SecondaryTrayWnd",
    L"Progman",
    L"WorkerW",
    L"SysListView32",
    L"MsoSplash",
    L"MultitaskingViewFrame",
    L"TaskManagerWindow",
    L"InputSwitchToastHandler",
    L"MSCTFIME UI",
    L"EdgeUiInputTopWndClass",
    L"Internet Explorer_Hidden",
    L"Chrome_RenderWidgetHostHWND",
    L"OPContainerClass",
    L"IHWindowClass",
};

/* Matched as substrings of the upper-cased process image path. */
static const wchar_t* const MORA_BLACKLIST_PROCESSES[] = {
    L"SEARCHUI.EXE",
    L"SHELLEXPERIENCEHOST.EXE",
    L"LOCKAPP.EXE",
    L"PEOPLEEXPERIENCEHOST.EXE",
    L"STARTMENUEXPERIENCEHOST.EXE",
    L"TEXTINPUTHOST.EXE",
    L"CORTANA.EXE",
};

static BOOL mora_is_popup(const mora_window_snapshot_t* snapshot)
{
    /* Win11 Settings and other UWP apps are CoreWindow + WS_POPUP without a Win32 caption. */
    if (!(snapshot->style & WS_POPUP) || snapshot->is_core_window) {
        return FALSE;
    }

    BOOL has_frame = (snapshot->style & WS_THICKFRAME) != 0;
    BOOL has_caption = (snapshot->style & WS_CAPTION) != 0;
    BOOL has_min_max = (snapshot->style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) != 0;

    return !(has_frame && (has_caption || has_min_max));
}

static BOOL mora_has_empty_rect(const mora_window_snapshot_t* snapshot)
{
    if (!snapshot->has_rect) {
        return FALSE;
    }

    return snapshot->rect.right <= snapshot->rect.left || snapshot->rect.bottom <= snapshot->rect.top;
}

mora_window_filter_result_t mora_window_filter_snapshot(const mora_window_snapshot_t* snapshot)
{
    if (!snapshot || !snapshot->is_visible) {
        return MORA_WINDOW_FILTER_NOT_VISIBLE;
    }

    if (snapshot->is_hung) {
        return MORA_WINDOW_FILTER_HUNG;
    }

    if (snapshot->is_minimized) {
        return MORA_WINDOW_FILTER_MINIMIZED;
    }

    if (snapshot->is_system_window) {
        return MORA_WINDOW_FILTER_SYSTEM_WINDOW;
    }

    if (snapshot->is_class_blacklisted) {
        return MORA_WINDOW_FILTER_CLASS_BLACKLISTED;
    }

    if (!snapshot->is_root_window) {
        return MORA_WINDOW_FILTER_NON_ROOT;
    }

    if (snapshot->style & WS_CHILD) {
        return MORA_WINDOW_FILTER_CHILD;
    }

    if (mora_is_popup(snapshot)) {
        return MORA_WINDOW_FILTER_POPUP;
    }

    if (snapshot->has_visible_owner && !(snapshot->ex_style & WS_EX_APPWINDOW)) {
        return MORA_WINDOW_FILTER_OWNED;
    }

    if (snapshot->ex_style & WS_EX_TOOLWINDOW) {
        return MORA_WINDOW_FILTER_TOOL;
    }

    if (snapshot->ex_style & WS_EX_NOACTIVATE) {
        return MORA_WINDOW_FILTER_NOACTIVATE;
    }

    if (snapshot->is_cloaked) {
        return MORA_WINDOW_FILTER_CLOAKED;
    }

    if (mora_has_empty_rect(snapshot)) {
        return MORA_WINDOW_FILTER_EMPTY_RECT;
    }

    if (!snapshot->is_on_current_desktop) {
        return MORA_WINDOW_FILTER_OTHER_DESKTOP;
    }

    if (snapshot->is_process_blacklisted) {
        return MORA_WINDOW_FILTER_PROCESS_BLACKLISTED;
    }

    if (snapshot->is_elevated) {
        return MORA_WINDOW_FILTER_ELEVATED;
    }

    if (snapshot->is_process_pending) {
        return MORA_WINDOW_FILTER_UWP_PENDING;
    }

    return MORA_WINDOW_FILTER_ACCEPTED;
}

BOOL mora_is_class_name_blacklisted(const wchar_t* class_name)
{
    if (!class_name) {
        return FALSE;
    }

    for (size_t i = 0; i < _countof(MORA_BLACKLIST_CLASSES); i++) {
        if (wcscmp(class_name, MORA_BLACKLIST_CLASSES[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL mora_contains_ci(const wchar_t* text, const wchar_t* part)
{
    size_t length = wcslen(part);

    for (; *text; ++text) {
        if (_wcsnicmp(text, part, length) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

BOOL mora_is_process_path_blacklisted(const wchar_t* process_path)
{
    if (!process_path) {
        return FALSE;
    }

    if (mora_contains_ci(process_path, L"\\SYSTEMAPPS\\")) {
        return TRUE;
    }

    for (size_t i = 0; i < _countof(MORA_BLACKLIST_PROCESSES); i++) {
        if (mora_contains_ci(process_path, MORA_BLACKLIST_PROCESSES[i])) {
            return TRUE;
        }
    }

    return FALSE;
}

const wchar_t* mora_window_filter_name(mora_window_filter_result_t result)
{
    switch (result) {
    case MORA_WINDOW_FILTER_ACCEPTED: return L"ACCEPTED";

    case MORA_WINDOW_FILTER_NOT_VISIBLE: return L"NOT_VISIBLE";

    case MORA_WINDOW_FILTER_HUNG: return L"HUNG";

    case MORA_WINDOW_FILTER_MINIMIZED: return L"MINIMIZED";

    case MORA_WINDOW_FILTER_SYSTEM_WINDOW: return L"SYSTEM_WINDOW";

    case MORA_WINDOW_FILTER_CLASS_BLACKLISTED: return L"CLASS_BLACKLISTED";

    case MORA_WINDOW_FILTER_NON_ROOT: return L"NON_ROOT";

    case MORA_WINDOW_FILTER_CHILD: return L"CHILD";

    case MORA_WINDOW_FILTER_POPUP: return L"POPUP";

    case MORA_WINDOW_FILTER_OWNED: return L"OWNED";

    case MORA_WINDOW_FILTER_TOOL: return L"TOOL";

    case MORA_WINDOW_FILTER_NOACTIVATE: return L"NOACTIVATE";

    case MORA_WINDOW_FILTER_CLOAKED: return L"CLOAKED";

    case MORA_WINDOW_FILTER_EMPTY_RECT: return L"EMPTY_RECT";

    case MORA_WINDOW_FILTER_OTHER_DESKTOP: return L"OTHER_DESKTOP";

    case MORA_WINDOW_FILTER_PROCESS_BLACKLISTED: return L"PROCESS_BLACKLISTED";

    case MORA_WINDOW_FILTER_ELEVATED: return L"ELEVATED";

    case MORA_WINDOW_FILTER_UWP_PENDING: return L"UWP_PENDING";

    default: return L"UNKNOWN";
    }
}
