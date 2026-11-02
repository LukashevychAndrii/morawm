#pragma once

#include <Windows.h>
#include <shellscalingapi.h>

#include "common/limits.h"

static inline UINT mora_dpi_of_window(HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);

    return dpi ? dpi : MORA_BASE_DPI;
}

static inline UINT mora_dpi_of_monitor(HMONITOR monitor)
{
    UINT dpi_x = MORA_BASE_DPI, dpi_y = MORA_BASE_DPI;

    if (!monitor || FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
        return MORA_BASE_DPI;
    }

    return dpi_x;
}

static inline int mora_scale(int value, UINT dpi)
{
    return MulDiv(value, (int)dpi, MORA_BASE_DPI);
}
