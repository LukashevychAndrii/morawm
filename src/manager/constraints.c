#include "internal.h"

#include <shellscalingapi.h>

#pragma comment(lib, "Shcore.lib")

/* Bounded so a slow app cannot stall every retile on the UI thread. */
#define MORA_MINIMUM_QUERY_MS 20

void mora_forget_placement_constraints(tracked_window_t* window)
{
    mora_placement_t* placement = &window->placement;

    placement->observed_minimum = (SIZE){ 0 };
    placement->corrections = 0;
    placement->failed = FALSE;
}

/* Outer size limits of the window in `dpi` pixels, as the app reports them.
 * A window without WS_SIZEBOX is fixed-size: its current size is both its
 * minimum and its maximum. Returns FALSE, with the current size as minimum
 * and no cap, when the app did not answer WM_GETMINMAXINFO. */
static BOOL mora_query_size(HWND hwnd, UINT dpi, SIZE* minimum, SIZE* maximum)
{
    RECT rect = { 0 };
    BOOL have_rect = GetWindowRect(hwnd, &rect);

    *minimum = (SIZE){ max(1, mora_rect_width(rect)), max(1, mora_rect_height(rect)) };
    *maximum = (SIZE){ 0 };

    if (have_rect && !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_SIZEBOX)) {
        *maximum = *minimum;
        return TRUE;
    }

    /* WM_GETMINMAXINFO is a system message: Windows marshals the structure.
     * Seed the documented defaults, since some apps only override one axis.
     * The reply is in the target app's coordinate space, not the caller's. */
    UINT window_dpi = mora_dpi_of_window(hwnd);

    MINMAXINFO info = { 0 };

    info.ptMinTrackSize.x = GetSystemMetricsForDpi(SM_CXMINTRACK, window_dpi);
    info.ptMinTrackSize.y = GetSystemMetricsForDpi(SM_CYMINTRACK, window_dpi);
    info.ptMaxTrackSize.x = GetSystemMetricsForDpi(SM_CXMAXTRACK, window_dpi);
    info.ptMaxTrackSize.y = GetSystemMetricsForDpi(SM_CYMAXTRACK, window_dpi);
    DWORD_PTR result;

    if (!SendMessageTimeoutW(hwnd, WM_GETMINMAXINFO, 0, (LPARAM)&info,
        SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, MORA_MINIMUM_QUERY_MS, &result)) {
        return FALSE;
    }

    POINT reported_min = info.ptMinTrackSize;

    if (reported_min.x > 0 && reported_min.y > 0 && reported_min.x <= INT16_MAX && reported_min.y <= INT16_MAX) {
        minimum->cx = max(1, MulDiv(reported_min.x, (int)dpi, (int)window_dpi));
        minimum->cy = max(1, MulDiv(reported_min.y, (int)dpi, (int)window_dpi));
    }

    /* The app may cap its own size, such as a Bitvise terminal whose grid
     * fixes the width. A cap only counts on an axis the app changed: an
     * untouched ptMaxTrackSize axis is the system default, far beyond any
     * tile, and must not shrink a window that can actually fill its tile. */
    POINT reported_max = info.ptMaxTrackSize;
    BOOL sane = reported_max.x > 0 && reported_max.y > 0 &&
        reported_max.x <= INT16_MAX && reported_max.y <= INT16_MAX;

    if (sane) {
        LONG default_cx = GetSystemMetricsForDpi(SM_CXMAXTRACK, window_dpi);
        LONG default_cy = GetSystemMetricsForDpi(SM_CYMAXTRACK, window_dpi);

        if (reported_max.x != default_cx) {
            maximum->cx = max(minimum->cx, MulDiv(reported_max.x, (int)dpi, (int)window_dpi));
        }

        if (reported_max.y != default_cy) {
            maximum->cy = max(minimum->cy, MulDiv(reported_max.y, (int)dpi, (int)window_dpi));
        }
    }

    return TRUE;
}

mora_window_constraints_t mora_window_constraints(tracked_window_t* window, HMONITOR monitor)
{
    mora_placement_t* placement = &window->placement;
    UINT dpi = mora_dpi_of_window(window->hwnd), dpi_y;

    if (monitor) {
        (void)GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &dpi_y);
    }

    /* Queried on every layout: apps can change their limits without resizing. */
    SIZE reported_min, reported_max;
    BOOL answered = mora_query_size(window->hwnd, dpi, &reported_min, &reported_max);
    BOOL same_dpi = placement->constraints_dpi == dpi;

    if (!answered && same_dpi && placement->constraints_known) {
        /* Once tiled, the current size is ours, not the app's: keep the last
         * answer so a slow app cannot ratchet its minimum up to its tile. */
        reported_min = placement->reported_minimum;
        reported_max = placement->reported_maximum;
    }

    if (!same_dpi || reported_min.cx != placement->reported_minimum.cx ||
        reported_min.cy != placement->reported_minimum.cy ||
        reported_max.cx != placement->reported_maximum.cx ||
        reported_max.cy != placement->reported_maximum.cy) {
        mora_forget_placement_constraints(window);
    }

    placement->reported_minimum = reported_min;
    placement->reported_maximum = reported_max;
    placement->constraints_dpi = dpi;
    placement->constraints_known |= answered;

    mora_window_constraints_t constraints = {
        .minimum = (SIZE){ max(reported_min.cx, placement->observed_minimum.cx),
            max(reported_min.cy, placement->observed_minimum.cy) },
        .maximum = reported_max,
    };

    /* The cap only ever shrinks a window to a size it can take; an observed
     * size above the reported cap wins, since the app really took it. */
    if (constraints.maximum.cx > 0) {
        constraints.maximum.cx = max(constraints.maximum.cx, constraints.minimum.cx);
    }

    if (constraints.maximum.cy > 0) {
        constraints.maximum.cy = max(constraints.maximum.cy, constraints.minimum.cy);
    }

    return constraints;
}
