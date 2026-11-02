#include "internal.h"

#include <math.h>
#include <wchar.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "user32.lib")

#define MORA_DIVIDER_LENGTH_NUMERATOR 2
#define MORA_DIVIDER_LENGTH_DENOMINATOR 5

void mora_fill_rect(HDC dc, RECT rect, COLORREF color)
{
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, (HBRUSH)GetStockObject(DC_BRUSH));
}

/* GDI has no anti-aliasing, so coverage comes from each pixel's distance to
 * the rounded rectangle's edge and the result is alpha-blended. */
void mora_fill_round_rect(HDC dc, RECT rect, int radius, COLORREF color)
{
    int width = rect.right - rect.left, height = rect.bottom - rect.top;

    if (width <= 0 || height <= 0) {
        return;
    }

    BITMAPINFO info = { .bmiHeader = { .biSize = sizeof(BITMAPINFOHEADER), .biWidth = width,
        .biHeight = -height, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB } };
    DWORD* pixels = NULL;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);

    if (!bitmap) {
        return;
    }

    float r = (float)min(radius, min(width, height) / 2);
    float half_w = width / 2.0f, half_h = height / 2.0f;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float qx = fabsf(x + 0.5f - half_w) - (half_w - r);
            float qy = fabsf(y + 0.5f - half_h) - (half_h - r);
            float ox = max(qx, 0.0f), oy = max(qy, 0.0f);
            float distance = sqrtf(ox * ox + oy * oy) + min(max(qx, qy), 0.0f) - r;
            float coverage = min(max(0.5f - distance, 0.0f), 1.0f);
            BYTE alpha = (BYTE)(coverage * 255.0f + 0.5f);

            pixels[y * width + x] = (DWORD)alpha << 24 | (DWORD)(GetRValue(color) * alpha / 255) << 16 |
                (DWORD)(GetGValue(color) * alpha / 255) << 8 | (DWORD)(GetBValue(color) * alpha / 255);
        }
    }

    HDC memory = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    AlphaBlend(dc, rect.left, rect.top, width, height, memory, 0, 0, width, height, blend);
    SelectObject(memory, old);
    DeleteDC(memory);
    DeleteObject(bitmap);
}

void mora_draw_divider(const mora_bar_t* bar, HDC dc, int x)
{
    int length = bar->metrics.height * MORA_DIVIDER_LENGTH_NUMERATOR / MORA_DIVIDER_LENGTH_DENOMINATOR;
    int top = (bar->metrics.height - length) / 2;

    mora_fill_rect(dc, (RECT){ x, top, x + bar->metrics.hairline, top + length }, THEME.divider);
}

void mora_draw_text(HDC dc, const wchar_t* text, RECT rect, COLORREF color, UINT format)
{
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

int mora_text_width(HDC dc, const wchar_t* text)
{
    SIZE size = { 0 };

    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);

    return size.cx;
}
