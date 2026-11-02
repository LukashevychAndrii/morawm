#pragma once

#include "common/dpi.h"
#include "common/geometry.h"
#include "common/limits.h"
#include "manager/manager.h"

#define MORA_REJECTION_LOG_CAPACITY 32
#define MORA_PICK_CANDIDATES 64
#define MORA_PICK_NAME_CAPACITY 64

typedef struct {
    HWND hwnd;
    int attempts;
} mora_pending_window_t;

typedef struct {
    mora_pending_window_t* entries;
    size_t count;
    size_t capacity;
} mora_pending_list_t;

extern mora_monitor_t g_monitors[MORA_WORKSPACE_COUNT];
extern mora_workspace_state_t g_state;
extern mora_tiling_layout_t g_layout;

/* Out-of-memory in the window manager is unrecoverable. */
__declspec(noreturn) void mora_die_out_of_memory(const wchar_t* what);
tracked_window_t* mora_window(mora_index_t index);

extern const wchar_t MORA_PROP_PARKED_FROM[];
extern const wchar_t MORA_PROP_HIDDEN[];

HANDLE mora_encode_prop(LONG value);
LONG mora_decode_prop(HANDLE prop);
/* Remove the parked/hidden props and restore the window; returns the hidden
 * prop so a caller refused by the window can record it again. */
HANDLE mora_undo_placement_props(HWND hwnd);
void mora_style_window(HWND hwnd);
void mora_release_window(HWND hwnd);

extern mora_pending_list_t g_pending;
void mora_forget_pending(HWND hwnd);

BOOL mora_discover_window(HWND hwnd, BOOL new_window);
BOOL mora_add_window(HWND hwnd, BOOL new_window);
void mora_describe_window(HWND hwnd, wchar_t* out, size_t capacity);
void mora_log_window(HWND hwnd, const wchar_t* action,
    _Printf_format_string_ const wchar_t* detail, ...);
mora_index_t mora_monitor_at_point(POINT point);
mora_index_t mora_monitor_index(HMONITOR monitor);

void mora_move_window_left_edge(HWND hwnd, LONG left);
void mora_sync_workspace_visibility(void);
void mora_set_window_overflow(tracked_window_t* window, BOOL overflow);
void mora_verify_overflow_parking(tracked_window_t* window);

void mora_repark_windows(void);

void mora_reset_arrangement(mora_index_t workspace);

void mora_reset_homes(void);
void mora_remember_homes(void);
void mora_remember_home(mora_index_t workspace);

void mora_focus_workspace(mora_index_t workspace_index);
void mora_move_cursor_to_workspace(mora_index_t workspace_index);
BOOL mora_focus_window(HWND hwnd);
void mora_record_foreground(HWND hwnd);
mora_index_t mora_work_workspace(void);
void mora_reset_focus_guard(void);

void mora_pick_window_event(DWORD event, HWND hwnd);

/* Size limits of one window, outer size in monitor pixels. minimum is what
 * the layout must give it; maximum is the app's cap on that axis, 0 meaning
 * uncapped, so a capped window never gets a size it cannot take. */
typedef struct {
    SIZE minimum;
    SIZE maximum;
} mora_window_constraints_t;

mora_window_constraints_t mora_window_constraints(tracked_window_t* window, HMONITOR monitor);
void mora_forget_placement_constraints(tracked_window_t* window);
