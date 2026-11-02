#include "internal.h"

#include <wchar.h>

#define MORA_BAR_MEDIA_TEXT_CAPACITY 300
#define MORA_GLYPH_PREVIOUS 0xE892
#define MORA_GLYPH_PLAY 0xE768
#define MORA_GLYPH_PAUSE 0xE769
#define MORA_GLYPH_NEXT 0xE893

static int mora_paint_media_buttons(mora_bar_t* bar, HDC dc, int right)
{
    /* Segoe Fluent Icons / MDL2 Assets: Previous, Play, Pause, Next. */
    const wchar_t symbols[MORA_MEDIA_BUTTON_COUNT] = { MORA_GLYPH_PREVIOUS,
        g_bar_media.playing ? MORA_GLYPH_PAUSE : MORA_GLYPH_PLAY, MORA_GLYPH_NEXT };
    HGDIOBJ old_font = SelectObject(dc, bar->symbol_font);
    int left = right - MORA_MEDIA_BUTTON_COUNT * bar->metrics.button;

    for (int i = 0; i < MORA_MEDIA_BUTTON_COUNT; ++i) {
        RECT rect = { left + i * bar->metrics.button, 0, left + (i + 1) * bar->metrics.button, bar->metrics.height };

        bar->media_buttons[i] = rect;
        wchar_t symbol[2] = { symbols[i], L'\0' };

        mora_draw_text(dc, symbol, rect, bar->hot_button == i ? THEME.text : THEME.subtext, DT_CENTER);
    }

    SelectObject(dc, old_font);

    return left;
}

int mora_paint_media(mora_bar_t* bar, HDC dc, int right)
{
    const mora_bar_metrics_t* metrics = &bar->metrics;

    ZeroMemory(bar->media_buttons, sizeof(bar->media_buttons));

    if (!g_bar_media.active) {
        return right;
    }

    right = mora_paint_media_buttons(bar, dc, right) - metrics->spacing / 2;
    wchar_t text[MORA_BAR_MEDIA_TEXT_CAPACITY];

    if (g_bar_media.artist[0]) {
        swprintf(text, _countof(text), L"%ls - %ls", g_bar_media.artist, g_bar_media.title);
    }
    else {
        swprintf(text, _countof(text), L"%ls", g_bar_media.title);
    }

    int width = min(mora_text_width(dc, text), metrics->media);
    RECT rect = { right - width, 0, right, metrics->height };

    mora_draw_text(dc, text, rect, g_bar_media.playing ? THEME.subtext : THEME.dim, DT_LEFT | DT_END_ELLIPSIS);

    return rect.left;
}

int mora_bar_media_button_hit(const mora_bar_t* bar, int x, int y)
{
    POINT point = { x, y };

    for (int i = 0; i < MORA_MEDIA_BUTTON_COUNT; ++i) {
        if (PtInRect(&bar->media_buttons[i], point)) {
            return i;
        }
    }

    return -1;
}
