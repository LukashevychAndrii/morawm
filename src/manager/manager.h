#pragma once

#include "workspace/workspace.h"

/* Native window-manager core: tracks windows per workspace, applies tiling,
 * workspace visibility and focus, and executes keyboard commands.
 * Single-threaded: call only from the thread running MoraWM's message loop.
 * All workspace and monitor IDs are zero-based. */

#define MORA_PENDING_RETRY_MS 10
#define MORA_PENDING_MAX_ATTEMPTS 15

typedef struct {
    HMONITOR hmonitor;
    wchar_t device[CCHDEVICENAME];
    BOOL primary;
    UINT dpi;
    RECT work_area;
    HWND bar_hwnd;
    int reserved_top;
} mora_monitor_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} mora_tile_pos_t;

static inline RECT mora_tile_pos_rect(mora_tile_pos_t pos)
{
    return (RECT){ pos.x, pos.y, pos.x + pos.width, pos.y + pos.height };
}

static inline POINT mora_tile_pos_center(mora_tile_pos_t pos)
{
    return (POINT){ pos.x + pos.width / 2, pos.y + pos.height / 2 };
}

typedef struct {
    HWND hwnd;
    mora_tile_pos_t pos;
} mora_tiled_window_t;

typedef struct {
    mora_tiled_window_t* entries;
    size_t count;
    size_t capacity;
} mora_tiling_layout_t;

BOOL mora_manager_init(const mora_monitor_t* monitors, size_t count);
void mora_manager_dispose(void);
void mora_manager_recover_windows(void);

const mora_workspace_state_t* mora_manager_state(void);
void mora_manager_set_work_area(mora_index_t monitor, RECT area);
BOOL mora_manager_set_monitors(const mora_monitor_t* monitors, size_t count);
const mora_tiling_layout_t* mora_manager_layout(void);
mora_index_t mora_manager_find_window(HWND hwnd);
POINT mora_manager_window_origin(HWND hwnd);

void mora_manager_detect_windows(void);
BOOL mora_manager_add_window(HWND hwnd);
BOOL mora_manager_add_tracked_window(const tracked_window_t* window);
BOOL mora_manager_add_window_to_workspace(mora_index_t workspace, const tracked_window_t* window);
typedef BOOL (*mora_window_router_t)(const tracked_window_t* window, mora_index_t* workspace);
void mora_manager_set_window_router(mora_window_router_t router);
void mora_manager_defer_window(HWND hwnd);
BOOL mora_manager_has_pending_windows(void);
BOOL mora_manager_retry_pending_windows(void);

BOOL mora_manager_handle_event(DWORD event, HWND hwnd);

void mora_manager_calculate_layout(void);
void mora_manager_tile(void);
#define MORA_PLACEMENT_SETTLE_MS 150
BOOL mora_manager_verify_layout(void);
BOOL mora_manager_layout_pending(void);
BOOL mora_manager_window_geometry_changed(HWND hwnd);
BOOL mora_manager_window_moved_by_user(HWND hwnd);
void mora_manager_restore_windows(void);

BOOL mora_manager_show_workspace(mora_index_t monitor, mora_index_t workspace);
BOOL mora_manager_activate_workspace(mora_index_t monitor, mora_index_t workspace);
BOOL mora_manager_configure_workspace(mora_index_t workspace, mora_index_t monitor);
BOOL mora_manager_move_window_to_workspace(HWND hwnd, mora_index_t workspace);
BOOL mora_manager_set_workspace_layout(mora_index_t workspace, mora_layout_config_t layout);
BOOL mora_manager_retile_workspace(mora_index_t workspace);
BOOL mora_manager_toggle_manual(mora_index_t workspace);
BOOL mora_manager_toggle_floating(HWND hwnd);
BOOL mora_manager_swap_monitors(mora_index_t monitor, mora_index_t other);

BOOL mora_manager_focus_window(HWND hwnd);
BOOL mora_manager_window_reachable(const tracked_window_t* window);

#define MORA_PICK_CAPACITY 26
/* Keys handed to the keyboard: one digit per workspace, then the letters, then NUL. */
#define MORA_PICK_KEY_CAPACITY (MORA_PICK_CAPACITY + MORA_WORKSPACE_COUNT + 1)

typedef struct {
    HWND hwnd;
    wchar_t key;
} mora_pick_target_t;

typedef struct {
    HWND held;
    unsigned serial;
    mora_pick_target_t targets[MORA_PICK_CAPACITY];
    size_t target_count;
} mora_pick_t;

const mora_pick_t* mora_manager_pick(void);
wchar_t mora_manager_pick_key_of(HWND hwnd);
BOOL mora_manager_pick_up(HWND hwnd);
BOOL mora_manager_pick_key(wchar_t key);
BOOL mora_manager_drop_on_window(HWND target);
BOOL mora_manager_drop_on_workspace(mora_index_t workspace);
void mora_manager_cancel_pick(void);

mora_index_t mora_manager_hotkey_monitor(HWND foreground);
mora_index_t mora_manager_next_window(mora_index_t workspace, mora_index_t current, int step);
BOOL mora_manager_dispatch_hotkey(size_t id, HWND foreground, mora_index_t monitor);
