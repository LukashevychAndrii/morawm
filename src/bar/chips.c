#include "internal.h"

#include <stdarg.h>
#include <wchar.h>

#define MORA_BAR_STATUS_CAPACITY 160
#define MORA_CHIP_SHRINK_DIVISOR 8
#define MORA_KEYCAP_RADIUS_NUMERATOR 2
#define MORA_KEYCAP_RADIUS_DENOMINATOR 3

typedef struct {
    HWND hwnd;
    const mora_icon_entry_t* icon;
    wchar_t title[MORA_CHIP_TITLE_CAPACITY];
    int title_width;
    BOOL focused;
    BOOL out_of_view;
    BOOL held;
    wchar_t key;
} mora_chip_t;

static void mora_append_status(wchar_t* status, size_t capacity, const wchar_t* format, ...)
{
    size_t length = wcslen(status);

    if (length && length + 2 < capacity) {
        wcscat_s(status, capacity, L"  ");
        length += 2;
    }

    va_list args;

    va_start(args, format);
    (void)vswprintf(status + length, capacity - length, format, args);
    va_end(args);
}

static void mora_workspace_status(const workspace_t* workspace, wchar_t* status, size_t capacity)
{
    /* Untiled windows are a choice that persists, so always say how to undo it. */
    if (workspace->manual) {
        mora_append_status(status, capacity, L"tiling off  Alt+R to tile");
    }
    else if (workspace->floating_count) {
        mora_append_status(status, capacity, L"%zu untiled  Alt+R to tile", workspace->floating_count);
    }

    if (workspace->elevated_count) {
        mora_append_status(status, capacity, L"%zu admin, not tiled", workspace->elevated_count);
    }

    if (workspace->oversized) {
        mora_append_status(status, capacity, L"[size limited]");
    }
}

static UINT mora_collect_chips(const mora_bar_t* bar, HDC dc, mora_chip_t* chips, UINT* total)
{
    const mora_workspace_state_t* state = mora_manager_state();
    mora_index_t workspace_index = state->active_workspace[bar->monitor];
    const workspace_t* workspace = &state->workspaces[workspace_index];
    const mora_pick_t* pick = mora_manager_pick();
    UINT count = 0;

    *total = 0;

    for (size_t i = 0; i < state->windows.count; ++i) {
        const tracked_window_t* window = &state->windows.entries[i];

        if (window->workspace_index != workspace_index || !mora_manager_window_reachable(window)) {
            continue;
        }

        if (++*total > MORA_BAR_MAX_CHIPS) {
            continue;
        }

        mora_chip_t* chip = &chips[count++];

        *chip = (mora_chip_t){
            .hwnd = window->hwnd,
            .icon = mora_icon_for_window(window, bar->metrics.icon),
            .focused = window->hwnd == workspace->focused_window,
            .out_of_view = window->overflow,
            .held = window->hwnd == pick->held,
            .key = mora_manager_pick_key_of(window->hwnd),
        };
        GetWindowTextW(window->hwnd, chip->title, _countof(chip->title));
        chip->title_width = chip->title[0] ? mora_text_width(dc, chip->title) : 0;
    }

    return count;
}

static int mora_chip_title_width(const mora_chip_t* chip, int limit)
{
    return min(chip->title_width, chip->focused ? 2 * limit : limit);
}

static int mora_chip_width(const mora_bar_metrics_t* metrics, const mora_chip_t* chip, int limit, BOOL titles)
{
    int width = 2 * metrics->chip_padding + metrics->icon;

    if (chip->key) {
        width += metrics->key + metrics->icon_gap;
    }

    int title = titles ? mora_chip_title_width(chip, limit) : 0;

    if (title > 0) {
        width += metrics->icon_gap + title;
    }

    return width;
}

static int mora_chips_width(const mora_bar_metrics_t* metrics, const mora_chip_t* chips, UINT count,
    int limit, BOOL titles)
{
    int width = 0;

    for (UINT i = 0; i < count; ++i) {
        width += mora_chip_width(metrics, &chips[i], limit, titles) + (i ? metrics->chip_gap : 0);
    }

    return width;
}

static int mora_draw_keycap(const mora_bar_t* bar, HDC dc, const wchar_t* text, int left)
{
    const mora_bar_metrics_t* metrics = &bar->metrics;
    HGDIOBJ old_font = SelectObject(dc, bar->label_font);
    int width = max(metrics->key, mora_text_width(dc, text) + metrics->key / 2);
    int top = (metrics->height - metrics->key) / 2;
    RECT box = { left, top, left + width, top + metrics->key };
    int radius = metrics->radius * MORA_KEYCAP_RADIUS_NUMERATOR / MORA_KEYCAP_RADIUS_DENOMINATOR;

    mora_fill_round_rect(dc, (RECT){ box.left, box.top + metrics->hairline, box.right, box.bottom + metrics->hairline },
        radius, THEME.key_edge);
    mora_fill_round_rect(dc, box, radius, THEME.key);
    mora_draw_text(dc, text, box, THEME.text, DT_CENTER);
    SelectObject(dc, old_font);

    return width;
}

static void mora_draw_chip(mora_bar_t* bar, HDC dc, const mora_chip_t* chip, RECT rect, int title)
{
    const mora_bar_metrics_t* metrics = &bar->metrics;
    BOOL hot = bar->hot_chip == chip->hwnd;

    int left = rect.left + metrics->chip_padding;

    if (chip->key) {
        wchar_t key[2] = { chip->key, L'\0' };

        mora_draw_keycap(bar, dc, key, left);
        left += metrics->key + metrics->icon_gap;
    }

    if (chip->icon && chip->icon->image) {
        mora_draw_icon(dc, chip->icon, left, (metrics->height - metrics->icon) / 2,
            chip->focused || hot ? 255 : MORA_ICON_DIM);
    }

    left += metrics->icon;

    if (title <= 0) {
        return;
    }

    left += metrics->icon_gap;
    COLORREF color = chip->focused ? THEME.text : hot ? THEME.hover : chip->out_of_view ? THEME.dim : THEME.title;

    mora_draw_text(dc, chip->title, (RECT){ left, 0, left + title, metrics->height }, color,
        DT_LEFT | DT_END_ELLIPSIS);
}

void mora_paint_chips(mora_bar_t* bar, HDC dc, int left, int center, int right)
{
    const mora_bar_metrics_t* metrics = &bar->metrics;
    const mora_workspace_state_t* state = mora_manager_state();
    const workspace_t* workspace = &state->workspaces[state->active_workspace[bar->monitor]];

    bar->chip_count = 0;

    mora_chip_t chips[MORA_BAR_MAX_CHIPS];
    UINT total;
    UINT count = mora_collect_chips(bar, dc, chips, &total);
    BOOL holding = FALSE;

    for (UINT i = 0; i < count; ++i) {
        holding |= chips[i].held;
    }

    if (holding) {
        static const wchar_t HINT[] = L"pick a window or";
        int width = mora_text_width(dc, HINT);

        mora_draw_text(dc, HINT, (RECT){ left, 0, left + width, metrics->height }, THEME.subtext, DT_LEFT);
        left += width + metrics->icon_gap;
        left += mora_draw_keycap(bar, dc, L"1–9", left) + metrics->spacing;
        left += mora_draw_keycap(bar, dc, L"Esc", left) + 2 * metrics->spacing;
    }

    wchar_t status[MORA_BAR_STATUS_CAPACITY] = L"";

    mora_workspace_status(workspace, status, _countof(status));

    if (status[0] && left < right) {
        RECT rect = { left, 0, min(right, left + mora_text_width(dc, status)), metrics->height };

        mora_draw_text(dc, status, rect, THEME.subtext, DT_LEFT | DT_END_ELLIPSIS);
        left = rect.right + 2 * metrics->spacing;
    }

    int room = right - left;

    /* Shorten every title alike until the chips fit, then drop them. */
    int limit = metrics->chip_text;
    BOOL titles = TRUE;

    while (mora_chips_width(metrics, chips, count, limit, titles) > room) {
        if (limit > metrics->chip_text_min) {
            limit = max(metrics->chip_text_min, limit - max(1, limit / MORA_CHIP_SHRINK_DIVISOR));
        }
        else if (titles) {
            titles = FALSE;
        }
        else {
            break;
        }
    }

    int chips_width = min(room, mora_chips_width(metrics, chips, count, limit, titles));
    int x = max(left, min(center - chips_width / 2, right - chips_width));

    room -= x - left;
    left = x;

    for (UINT i = 0; i < count && bar->chip_count < MORA_BAR_MAX_CHIPS; ++i) {
        int width = mora_chip_width(metrics, &chips[i], limit, titles);

        if (x + width > left + room) {
            break;
        }

        RECT rect = { x, 0, x + width, metrics->height };

        mora_draw_chip(bar, dc, &chips[i], rect, titles ? mora_chip_title_width(&chips[i], limit) : 0);
        bar->chips[bar->chip_count].hwnd = chips[i].hwnd;
        bar->chips[bar->chip_count].rect = (RECT){ x, 0, x + width, metrics->height };
        ++bar->chip_count;
        x += width + metrics->chip_gap;
    }

    if (bar->chip_count < total) {
        wchar_t more[MORA_BAR_MORE_CAPACITY];

        swprintf(more, _countof(more), L"+%u", total - bar->chip_count);
        int width = mora_text_width(dc, more);

        mora_draw_text(dc, more, (RECT){ x, 0, x + width, metrics->height }, THEME.dim, DT_LEFT | DT_NOCLIP);
    }
}

HWND mora_bar_chip_hit(const mora_bar_t* bar, int x, int y)
{
    POINT point = { x, y };

    for (UINT i = 0; i < bar->chip_count; ++i) {
        if (PtInRect(&bar->chips[i].rect, point)) {
            return bar->chips[i].hwnd;
        }
    }

    return NULL;
}
