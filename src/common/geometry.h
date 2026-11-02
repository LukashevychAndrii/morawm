#pragma once

#include <Windows.h>

static inline POINT mora_rect_center(RECT rect)
{
    return (POINT){ (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
}

static inline LONG mora_rect_width(RECT rect)
{
    return rect.right - rect.left;
}

static inline LONG mora_rect_height(RECT rect)
{
    return rect.bottom - rect.top;
}
