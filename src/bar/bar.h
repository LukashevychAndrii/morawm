#pragma once

#include "common/limits.h"
#include "manager/manager.h"

/* Messages the bar does not consume are offered to this handler first.
 * Set *handled to TRUE to skip DefWindowProc. */
typedef LRESULT (*mora_bar_message_handler_t)(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, BOOL* handled);

/* Creates one bar per monitor and fills in each monitor's bar_hwnd and
 * reserved_top. Call before mora_manager_init so the manager sees both. */
BOOL mora_bars_create(HINSTANCE instance, mora_monitor_t* monitors, size_t count,
    mora_bar_message_handler_t handler);
void mora_bars_destroy(void);
/* Matches the bars to a new set of monitors after displays changed: moves and
 * rescales existing bars, creates or destroys the difference, and fills in
 * bar_hwnd and reserved_top. The first bar is kept, since it owns the hotkeys. */
BOOL mora_bars_set_monitors(mora_monitor_t* monitors, size_t count);

void mora_bars_refresh(void);
void mora_bars_sync_fullscreen(void);
void mora_bars_refresh_title(HWND hwnd);

typedef struct {
    int height;
    int inset;
    int mark;
    int spacing;
    int origin;
    int cell;
    int slot_icon;
    int hairline;
    int radius;
    int key;
    int font_height;
    int title_height;
    int media;
    int button;
    int button_glyph;
    int icon;
    int icon_gap;
    int tracking;
    int chip_padding;
    int chip_gap;
    int chip_text;
    int chip_text_min;
} mora_bar_metrics_t;

mora_bar_metrics_t mora_bar_metrics(UINT dpi);

typedef enum {
    MORA_SLOT_EMPTY,
    MORA_SLOT_ELSEWHERE,
    MORA_SLOT_OCCUPIED,
    MORA_SLOT_SHOWN,
} mora_bar_slot_t;

mora_bar_slot_t mora_bar_slot(const mora_workspace_state_t* state, mora_index_t monitor, mora_index_t workspace);

int mora_bar_slot_width(const mora_bar_metrics_t* metrics, UINT icons);
int mora_bar_workspaces_right(const mora_bar_metrics_t* metrics, const UINT icons[MORA_WORKSPACE_COUNT]);
mora_index_t mora_bar_hit_workspace(const mora_bar_metrics_t* metrics, const UINT icons[MORA_WORKSPACE_COUNT],
    int x, int y);
