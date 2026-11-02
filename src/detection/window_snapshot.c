#define COBJMACROS
#include "internal.h"

#include <cguid.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wchar.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

static const wchar_t MORA_CORE_WINDOW_CLASS[] = L"Windows.UI.Core.CoreWindow";

static BOOL mora_is_cloaked(HWND hwnd)
{
    int cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));

    return SUCCEEDED(hr) && cloaked != 0;
}

static BOOL mora_has_visible_owner(HWND hwnd)
{
    HWND owner = GetWindow(hwnd, GW_OWNER);

    if (!owner || !IsWindowVisible(owner) || mora_is_cloaked(owner)) {
        return FALSE;
    }

    RECT rect;

    if (!GetWindowRect(owner, &rect)) {
        return TRUE;
    }

    return rect.left != rect.right && rect.top != rect.bottom;
}

static BOOL mora_is_window_on_current_desktop(HWND hwnd)
{
    static IVirtualDesktopManager* manager = NULL;
    static BOOL initialized = FALSE;

    if (!initialized) {
        initialized = TRUE;
        (void)CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        (void)CoCreateInstance(&CLSID_VirtualDesktopManager, NULL, CLSCTX_ALL,
            &IID_IVirtualDesktopManager, (void**)&manager);
    }

    if (!manager) {
        return TRUE;
    }

    BOOL on_desktop = TRUE;

    if (FAILED(IVirtualDesktopManager_IsWindowOnCurrentVirtualDesktop(manager, hwnd, &on_desktop)) || on_desktop) {
        return TRUE;
    }

    /* Explorer assigns a shown window to a desktop asynchronously (tens of ms).
     * Until then its desktop ID is GUID_NULL and the query above reports FALSE. */
    GUID desktop;

    if (FAILED(IVirtualDesktopManager_GetWindowDesktopId(manager, hwnd, &desktop)) ||
        IsEqualGUID(&desktop, &GUID_NULL)) {
        return TRUE;
    }

    /* The ID may have been assigned between the two calls, so ask again. */
    if (FAILED(IVirtualDesktopManager_IsWindowOnCurrentVirtualDesktop(manager, hwnd, &on_desktop))) {
        return TRUE;
    }

    return on_desktop;
}

BOOL mora_window_bounds(HWND hwnd, RECT* bounds)
{
    if (!bounds) {
        return FALSE;
    }

    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, bounds, sizeof(*bounds)))) {
        return TRUE;
    }

    return GetWindowRect(hwnd, bounds);
}

mora_window_snapshot_t mora_get_window_snapshot(HWND hwnd)
{
    mora_window_snapshot_t snapshot = {
        .is_visible = IsWindowVisible(hwnd),
        .is_hung = IsHungAppWindow(hwnd),
        .is_minimized = IsIconic(hwnd),
        .is_system_window = hwnd == GetDesktopWindow() || hwnd == GetShellWindow(),
        .is_root_window = GetAncestor(hwnd, GA_ROOT) == hwnd,
        .has_visible_owner = mora_has_visible_owner(hwnd),
        .is_cloaked = mora_is_cloaked(hwnd),
        .is_on_current_desktop = mora_is_window_on_current_desktop(hwnd),
        .style = GetWindowLongPtrW(hwnd, GWL_STYLE),
        .ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE),
    };
    snapshot.has_rect = GetWindowRect(hwnd, &snapshot.rect);

    wchar_t class_name[MORA_CLASS_NAME_CAPACITY];

    if (GetClassNameW(hwnd, class_name, _countof(class_name)) != 0) {
        snapshot.is_class_blacklisted = mora_is_class_name_blacklisted(class_name);
        snapshot.is_core_window = wcscmp(class_name, MORA_CORE_WINDOW_CLASS) == 0;
    }

    return snapshot;
}
