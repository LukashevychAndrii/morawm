#pragma once

#include <Windows.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t mora_index_t;
#define MORA_INVALID_INDEX UINT32_MAX
#define MORA_WORKSPACE_COUNT 9
#define MORA_TITLE_CAPACITY 256

typedef enum {
    MORA_LAYOUT_COLUMNS,
    MORA_LAYOUT_ROWS,
    MORA_LAYOUT_MAIN
} mora_layout_kind_t;

#define MORA_MAIN_PERCENT 60
#define MORA_MAIN_SIDE_TILES 2

typedef struct {
    mora_layout_kind_t kind;
} mora_layout_config_t;

typedef struct {
    SIZE reported_minimum;
    /* Size cap from the app (WM_GETMINMAXINFO ptMaxTrackSize), 0 per axis =
     * no cap. A width-capped window, such as a Bitvise terminal, cannot grow
     * to a wider tile no matter which tile it is given. */
    SIZE reported_maximum;
    UINT constraints_dpi;
    BOOL constraints_known;
    SIZE observed_minimum;
    unsigned corrections;

    /* The app rejects placement: show it alone and stop moving it. */
    BOOL failed;
    BOOL verify;
    RECT target;

    /* Last verified outer bounds, to tell external size/DPI changes from ours. */
    RECT geometry;
    UINT geometry_dpi;
} mora_placement_t;

typedef struct {
    HWND hwnd;
    RECT rect;
    wchar_t title[MORA_TITLE_CAPACITY];
    DWORD pid;
    mora_index_t workspace_index;
    BOOL tileable;
    BOOL hidden_by_workspace;
    BOOL minimized;

    /* Moved off-screen while its workspace is inactive. */
    BOOL parked;
    BOOL overflow;

    /* Moved, resized or maximized by the user: never placed by the layout until
     * the workspace is retiled explicitly. */
    BOOL floating;

    /* Covers its monitor (F11, video): keeps its tile but is not placed, and
     * returns to it on its own when it leaves fullscreen. */
    BOOL fullscreen;

    /* Owned by a more privileged process, which Windows does not let MoraWM move
     * or hide: never placed, but minimized while its workspace is inactive. */
    BOOL elevated;
    BOOL minimized_by_workspace;

    /* When MoraWM restored it; the restore activates it, which is not the user's
     * choice of focus. */
    ULONGLONG restored_at;
    mora_placement_t placement;
} tracked_window_t;

typedef struct {
    tracked_window_t* entries;
    size_t count;
    size_t capacity;
} window_array_t;

typedef struct {
    size_t window_count;
    mora_index_t monitor_index;
    HWND focused_window;
    mora_layout_config_t layout;
    size_t overflow_count;
    BOOL oversized;
    size_t floating_count;
    size_t elevated_count;
    BOOL manual;
} workspace_t;

typedef struct {
    workspace_t workspaces[MORA_WORKSPACE_COUNT];
    mora_index_t active_workspace[MORA_WORKSPACE_COUNT];
    size_t monitor_count;
    window_array_t windows;
} mora_workspace_state_t;

/* Single-threaded state API. It never calls Win32 or changes native windows.
 * Initialize a zeroed/disposed state with 1..9 monitors. Dispose before reinit.
 * A window index is its position in the array: removing or moving a window
 * shifts the windows after it, so look windows up again by HWND afterwards.
 * Newly added/moved windows are appended to their workspace's tiling order. */
BOOL mora_workspaces_init(mora_workspace_state_t* state, size_t monitor_count);
void mora_workspaces_dispose(mora_workspace_state_t* state);
BOOL mora_workspace_is_active(const mora_workspace_state_t* state, mora_index_t workspace);
BOOL mora_workspace_activate(mora_workspace_state_t* state, mora_index_t monitor, mora_index_t workspace);
BOOL mora_workspace_place(mora_workspace_state_t* state, mora_index_t workspace, mora_index_t monitor);
BOOL mora_workspace_swap_monitors(mora_workspace_state_t* state, mora_index_t first, mora_index_t second);
BOOL mora_workspaces_set_monitors(mora_workspace_state_t* state, size_t monitor_count,
    const mora_index_t* old_to_new, const mora_index_t target[MORA_WORKSPACE_COUNT],
    const BOOL preferred[MORA_WORKSPACE_COUNT]);
mora_index_t mora_workspace_find_window(const mora_workspace_state_t* state, HWND hwnd);
mora_index_t mora_workspace_add_window(mora_workspace_state_t* state, mora_index_t workspace, const tracked_window_t* window);
BOOL mora_workspace_remove_window(mora_workspace_state_t* state, HWND hwnd);
/* A hidden, empty destination is reassigned to the monitor of the window's
 * current workspace. */
BOOL mora_workspace_move_window(mora_workspace_state_t* state, HWND hwnd, mora_index_t workspace);
BOOL mora_workspace_remember_focus(mora_workspace_state_t* state, HWND hwnd);
BOOL mora_workspace_swap_windows(mora_workspace_state_t* state, HWND first, HWND second);
/* Exchange tiling positions, also across workspaces: each window takes the
 * other's place and workspace. A workspace focused on one of the windows is
 * focused on the one now in its place. */
BOOL mora_workspace_exchange_windows(mora_workspace_state_t* state, HWND first, HWND second);
BOOL mora_workspace_move_to_front(mora_workspace_state_t* state, HWND hwnd);
BOOL mora_layout_is_valid(mora_layout_config_t layout);
BOOL mora_workspace_set_layout(mora_workspace_state_t* state, mora_index_t workspace, mora_layout_config_t layout);
/* Pure layout calculation. Area is the monitor work area with the bar removed.
 * Fails if even one pixel per tile will not fit. */
BOOL mora_workspace_tile_rect(mora_layout_config_t layout, RECT area, size_t index, size_t count, RECT* tile);

typedef struct {
    SIZE minimum;
    BOOL selected;
    RECT tile;
} mora_layout_item_t;

/* Keep the preferred window, then admit windows in list order only when all
 * selected windows fit their tiles, up to 1 + MORA_MAIN_SIDE_TILES for
 * MORA_LAYOUT_MAIN. Selected windows take tiles in list order. An oversized preferred window gets
 * the area to itself, anchored at its top left; its excess may extend beyond it. */
size_t mora_layout_select(mora_layout_config_t layout, RECT area,
    mora_layout_item_t* items, size_t count, size_t preferred, BOOL* oversized);
