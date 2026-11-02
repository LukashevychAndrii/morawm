#include "internal.h"

#include <wchar.h>

#include "detection/window_detection.h"
#include "keyboard/keyboard.h"
#include "log/log.h"

#pragma comment(lib, "user32.lib")

static mora_pick_t g_pick;
static unsigned g_pick_serial;

const mora_pick_t* mora_manager_pick(void)
{
    return &g_pick;
}

wchar_t mora_manager_pick_key_of(HWND hwnd)
{
    for (size_t i = 0; hwnd && i < g_pick.target_count; ++i) {
        if (g_pick.targets[i].hwnd == hwnd) {
            return g_pick.targets[i].key;
        }
    }

    return 0;
}

BOOL mora_manager_window_reachable(const tracked_window_t* window)
{
    return window->tileable &&
        ((window->overflow && window->hidden_by_workspace) || mora_is_workspace_candidate(window->hwnd));
}

static void mora_window_name(const tracked_window_t* window, wchar_t* name, size_t capacity)
{
    wchar_t path[MAX_PATH] = L"";

    name[0] = L'\0';

    if (mora_process_image_path(window->pid, path, _countof(path)) && path[0] &&
        !mora_is_app_frame_host_path(path)) {
        wcsncpy_s(name, capacity, mora_file_name(path), _TRUNCATE);
    }
    else {
        GetWindowTextW(window->hwnd, name, (int)capacity);
    }
}

static BOOL mora_is_pick_target(const tracked_window_t* window, HWND held)
{
    return window->hwnd != held && !window->elevated && mora_manager_window_reachable(window);
}

static void mora_collect_targets(HWND held, mora_index_t first_monitor)
{
    HWND candidates[MORA_PICK_CANDIDATES];

    wchar_t names[_countof(candidates)][MORA_PICK_NAME_CAPACITY];
    const wchar_t* name_list[_countof(candidates)];
    size_t count = 0;

    for (mora_index_t m = 0; m < g_state.monitor_count; ++m) {
        mora_index_t workspace = g_state.active_workspace[(first_monitor + m) % g_state.monitor_count];

        for (mora_index_t i = 0; i < g_state.windows.count && count < _countof(candidates); ++i) {
            const tracked_window_t* window = mora_window(i);

            if (window->workspace_index != workspace || !mora_is_pick_target(window, held)) {
                continue;
            }

            candidates[count] = window->hwnd;
            mora_window_name(window, names[count], _countof(names[count]));
            name_list[count] = names[count];
            ++count;
        }
    }

    wchar_t keys[_countof(candidates)];

    mora_keyboard_assign_keys(name_list, count, keys);

    for (size_t i = 0; i < count && g_pick.target_count < MORA_PICK_CAPACITY; ++i) {
        if (keys[i]) {
            g_pick.targets[g_pick.target_count++] = (mora_pick_target_t){ candidates[i], keys[i] };
        }
    }
}

BOOL mora_manager_pick_up(HWND hwnd)
{
    mora_manager_cancel_pick();
    mora_index_t index = mora_manager_find_window(hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    const tracked_window_t* window = mora_window(index);

    if (!window->tileable || window->elevated || !mora_workspace_is_active(&g_state, window->workspace_index) ||
        !mora_is_tiling_candidate(hwnd)) {
        return FALSE;
    }

    g_pick.held = hwnd;
    g_pick.serial = ++g_pick_serial;
    mora_collect_targets(hwnd, g_state.workspaces[window->workspace_index].monitor_index);
    wchar_t keys[MORA_PICK_KEY_CAPACITY];

    for (mora_index_t i = 0; i < MORA_WORKSPACE_COUNT; ++i) {
        keys[i] = (wchar_t)(L'1' + i);
    }

    for (size_t i = 0; i < g_pick.target_count; ++i) {
        keys[MORA_WORKSPACE_COUNT + i] = g_pick.targets[i].key;
    }

    keys[MORA_WORKSPACE_COUNT + g_pick.target_count] = L'\0';

    if (g_monitors[0].bar_hwnd) {
        mora_keyboard_register_pick(g_monitors[0].bar_hwnd, keys);
    }

    mora_log(L"pick: holding %p, %zu targets", (void*)hwnd, g_pick.target_count);

    return TRUE;
}

void mora_manager_cancel_pick(void)
{
    if (!g_pick.held) {
        return;
    }

    g_pick = (mora_pick_t){ 0 };

    if (g_monitors[0].bar_hwnd) {
        mora_keyboard_unregister_pick(g_monitors[0].bar_hwnd);
    }
}

static size_t mora_window_rank(HWND hwnd)
{
    mora_index_t index = mora_manager_find_window(hwnd);
    size_t rank = 0;

    for (mora_index_t i = 0; i < index; ++i) {
        rank += mora_window(i)->workspace_index == mora_window(index)->workspace_index;
    }

    return rank;
}

BOOL mora_manager_drop_on_window(HWND target)
{
    HWND held = g_pick.held;
    BOOL known = mora_manager_pick_key_of(target) != 0;

    mora_manager_cancel_pick();

    if (!held || !known || !mora_workspace_exchange_windows(&g_state, held, target)) {
        return FALSE;
    }

    mora_window(mora_manager_find_window(held))->floating = FALSE;
    mora_window(mora_manager_find_window(target))->floating = FALSE;
    HWND focus = mora_window_rank(target) <= mora_window_rank(held) ? target : held;

    mora_workspace_remember_focus(&g_state, focus);
    mora_sync_workspace_visibility();
    mora_manager_tile();
    (void)mora_focus_window(focus);
    mora_move_cursor_to_workspace(mora_window(mora_manager_find_window(focus))->workspace_index);

    return TRUE;
}

BOOL mora_manager_drop_on_workspace(mora_index_t workspace)
{
    HWND held = g_pick.held;

    mora_manager_cancel_pick();

    return held && mora_manager_move_window_to_workspace(held, workspace);
}

BOOL mora_manager_pick_key(wchar_t key)
{
    if (!g_pick.held) {
        return FALSE;
    }

    if (key >= L'1' && key < L'1' + MORA_WORKSPACE_COUNT) {
        return mora_manager_drop_on_workspace((mora_index_t)(key - L'1'));
    }

    for (size_t i = 0; key && i < g_pick.target_count; ++i) {
        if (g_pick.targets[i].key == key) {
            return mora_manager_drop_on_window(g_pick.targets[i].hwnd);
        }
    }

    mora_manager_cancel_pick();

    return TRUE;
}

void mora_pick_window_event(DWORD event, HWND hwnd)
{
    if (!g_pick.held) {
        return;
    }

    if ((event == EVENT_SYSTEM_FOREGROUND && hwnd != g_pick.held) ||
        (event == EVENT_OBJECT_DESTROY && (hwnd == g_pick.held || mora_manager_pick_key_of(hwnd)))) {
        mora_manager_cancel_pick();
    }
}
