#include "internal.h"

#include <wchar.h>

int mora_paint_workspaces(mora_bar_t* bar, HDC dc, const mora_workspace_state_t* state)
{
    static const COLORREF* const COLORS[] = {
        [MORA_SLOT_EMPTY] = &THEME.faint,
        [MORA_SLOT_ELSEWHERE] = &THEME.dim,
        [MORA_SLOT_OCCUPIED] = &THEME.subtext,
        [MORA_SLOT_SHOWN] = &THEME.text,
    };
    const mora_bar_metrics_t* metrics = &bar->metrics;
    int icon_top = (metrics->height - metrics->slot_icon) / 2;
    int left = metrics->origin;

    for (mora_index_t i = 0; i < MORA_WORKSPACE_COUNT; ++i) {
        const mora_icon_entry_t* icons[MORA_BAR_MAX_ICONS];
        UINT total = mora_bar_workspace_icons(state, i, metrics->slot_icon, icons, MORA_BAR_MAX_ICONS);
        UINT shown = min(total, MORA_BAR_MAX_ICONS);

        if (total > MORA_BAR_MAX_ICONS) {
            --shown;
        }

        bar->slot_icons[i] = shown + (total > shown);

        RECT cell = { left, 0, left + metrics->cell, metrics->height };

        left += mora_bar_slot_width(metrics, bar->slot_icons[i]);

        mora_bar_slot_t slot = mora_bar_slot(state, bar->monitor, i);
        BOOL current = slot == MORA_SLOT_SHOWN;
        BOOL hot = bar->hot_workspace == i && !current;
        COLORREF color = hot ? THEME.hover : *COLORS[slot];
        wchar_t label[2] = { (wchar_t)(L'1' + i), L'\0' };

        mora_draw_text(dc, label, cell, color, DT_CENTER);

        int icon_left = cell.right;

        for (UINT k = 0; k < shown; ++k) {
            mora_draw_icon(dc, icons[k], icon_left, icon_top, current || hot ? 255 : MORA_ICON_DIM);
            icon_left += metrics->slot_icon + metrics->icon_gap;
        }

        if (total > shown) {
            wchar_t more[MORA_BAR_MORE_CAPACITY];

            swprintf(more, _countof(more), L"+%u", total - shown);
            RECT rect = { icon_left, 0, icon_left + metrics->slot_icon + metrics->icon_gap, metrics->height };

            mora_draw_text(dc, more, rect, *COLORS[slot], DT_LEFT | DT_NOCLIP);
        }
    }

    return left;
}
