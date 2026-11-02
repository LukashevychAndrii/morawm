#include "internal.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

void mora_paint_background(const mora_bar_t* bar, HDC dc, RECT client)
{
    (void)bar;
    mora_fill_rect(dc, client, THEME.background);
}

static void mora_paint_bar(mora_bar_t* bar, HDC dc, RECT client)
{
    const mora_bar_metrics_t* metrics = &bar->metrics;
    const mora_workspace_state_t* state = mora_manager_state();

    mora_paint_background(bar, dc, client);
    SetBkMode(dc, TRANSPARENT);
    mora_icons_prune();

    mora_draw_mark(dc, metrics->inset, (metrics->height - metrics->mark) / 2, metrics->mark);
    mora_draw_divider(bar, dc, metrics->inset + metrics->mark + metrics->spacing);

    HGDIOBJ old_font = SelectObject(dc, bar->label_font);

    SetTextCharacterExtra(dc, metrics->tracking);
    int clock = mora_paint_clock(bar, dc, client.right - metrics->inset);

    mora_draw_divider(bar, dc, clock - metrics->spacing);

    SelectObject(dc, bar->title_font);
    SetTextCharacterExtra(dc, 0);
    int media = mora_paint_media(bar, dc, clock - 2 * metrics->spacing);

    if (media != clock - 2 * metrics->spacing) {
        mora_draw_divider(bar, dc, media - metrics->spacing);
        clock = media;
    }

    bar->chip_count = 0;

    if (bar->monitor < state->monitor_count) {
        SelectObject(dc, bar->label_font);
        SetTextCharacterExtra(dc, metrics->tracking);
        int workspaces = mora_paint_workspaces(bar, dc, state);

        SelectObject(dc, bar->title_font);
        SetTextCharacterExtra(dc, 0);
        mora_paint_chips(bar, dc, workspaces + metrics->spacing, client.right / 2, clock - 2 * metrics->spacing);
    }

    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, old_font);
}

void mora_bar_on_paint(mora_bar_t* bar)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(bar->hwnd, &ps);
    RECT client;

    GetClientRect(bar->hwnd, &client);
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);

    if (buffer && bitmap) {
        HGDIOBJ old_bitmap = SelectObject(buffer, bitmap);

        mora_paint_bar(bar, buffer, client);
        BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, old_bitmap);
    }

    if (bitmap) {
        DeleteObject(bitmap);
    }

    if (buffer) {
        DeleteDC(buffer);
    }

    EndPaint(bar->hwnd, &ps);
}
