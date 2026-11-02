#include "bar/bar.h"

#include "common/dpi.h"

mora_bar_metrics_t mora_bar_metrics(UINT dpi)
{
#define MORA_SCALE(value) mora_scale((value), dpi)
    mora_bar_metrics_t metrics = {
        .height = MORA_SCALE(28),
        .inset = MORA_SCALE(14),
        .mark = MORA_SCALE(28),
        .spacing = MORA_SCALE(14),
        .cell = MORA_SCALE(24),
        .slot_icon = MORA_SCALE(16),
        .hairline = max(1, MORA_SCALE(1)),
        .radius = MORA_SCALE(6),
        .key = MORA_SCALE(16),
        .font_height = MORA_SCALE(11),
        .title_height = MORA_SCALE(12),
        .media = MORA_SCALE(280),
        .button = MORA_SCALE(22),
        .button_glyph = MORA_SCALE(10),
        .icon = MORA_SCALE(18),
        .icon_gap = MORA_SCALE(5),
        .tracking = max(1, MORA_SCALE(1)),
        .chip_padding = MORA_SCALE(7),
        .chip_gap = MORA_SCALE(2),
        .chip_text = MORA_SCALE(150),
        .chip_text_min = MORA_SCALE(36),
    };
#undef MORA_SCALE
    metrics.origin = metrics.inset + metrics.mark + 2 * metrics.spacing;

    return metrics;
}

mora_bar_slot_t mora_bar_slot(const mora_workspace_state_t* state, mora_index_t monitor, mora_index_t workspace)
{
    if (monitor >= state->monitor_count || workspace >= MORA_WORKSPACE_COUNT) {
        return MORA_SLOT_EMPTY;
    }

    if (state->active_workspace[monitor] == workspace) {
        return MORA_SLOT_SHOWN;
    }

    const workspace_t* entry = &state->workspaces[workspace];

    if (mora_workspace_is_active(state, workspace)) {
        return MORA_SLOT_ELSEWHERE;
    }

    if (entry->window_count == 0) {
        return MORA_SLOT_EMPTY;
    }

    return entry->monitor_index == monitor ? MORA_SLOT_OCCUPIED : MORA_SLOT_ELSEWHERE;
}

int mora_bar_slot_width(const mora_bar_metrics_t* metrics, UINT icons)
{
    return metrics->cell + (int)icons * (metrics->slot_icon + metrics->icon_gap) + (icons ? metrics->icon_gap : 0);
}

int mora_bar_workspaces_right(const mora_bar_metrics_t* metrics, const UINT icons[MORA_WORKSPACE_COUNT])
{
    int right = metrics->origin;

    for (mora_index_t i = 0; i < MORA_WORKSPACE_COUNT; ++i) {
        right += mora_bar_slot_width(metrics, icons[i]);
    }

    return right;
}

mora_index_t mora_bar_hit_workspace(const mora_bar_metrics_t* metrics, const UINT icons[MORA_WORKSPACE_COUNT],
    int x, int y)
{
    if (x < metrics->origin || y < 0 || y >= metrics->height) {
        return MORA_INVALID_INDEX;
    }

    int left = metrics->origin;

    for (mora_index_t i = 0; i < MORA_WORKSPACE_COUNT; ++i) {
        left += mora_bar_slot_width(metrics, icons[i]);

        if (x < left) {
            return i;
        }
    }

    return MORA_INVALID_INDEX;
}
