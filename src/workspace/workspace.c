#include "workspace/workspace.h"
#include "common/array.h"

#include <string.h>

static BOOL valid_workspace(const mora_workspace_state_t* state, mora_index_t workspace)
{
    return state && state->monitor_count > 0 && workspace < MORA_WORKSPACE_COUNT;
}

BOOL mora_workspaces_init(mora_workspace_state_t* state, size_t monitor_count)
{
    if (!state || monitor_count == 0 || monitor_count > MORA_WORKSPACE_COUNT) {
        return FALSE;
    }

    /* Refuse to silently leak a state that was never disposed. */
    if (state->monitor_count || state->windows.entries) {
        return FALSE;
    }

    *state = (mora_workspace_state_t){ .monitor_count = monitor_count };

    for (mora_index_t i = 0; i < MORA_WORKSPACE_COUNT; ++i) {
        state->workspaces[i] = (workspace_t){
            .monitor_index = i % (mora_index_t)monitor_count,
            .layout = { .kind = MORA_LAYOUT_MAIN },
        };
        state->active_workspace[i] = i < monitor_count ? i : MORA_INVALID_INDEX;
    }

    return TRUE;
}

void mora_workspaces_dispose(mora_workspace_state_t* state)
{
    if (!state) {
        return;
    }

    free(state->windows.entries);
    *state = (mora_workspace_state_t){ 0 };
}

BOOL mora_workspace_is_active(const mora_workspace_state_t* state, mora_index_t workspace)
{
    if (!valid_workspace(state, workspace)) {
        return FALSE;
    }

    mora_index_t monitor = state->workspaces[workspace].monitor_index;

    return monitor < state->monitor_count && state->active_workspace[monitor] == workspace;
}

BOOL mora_workspace_activate(mora_workspace_state_t* state, mora_index_t monitor, mora_index_t workspace)
{
    if (!valid_workspace(state, workspace) || monitor >= state->monitor_count) {
        return FALSE;
    }

    /* A workspace visible on another monitor is not stolen. */
    if (mora_workspace_is_active(state, workspace) && state->workspaces[workspace].monitor_index != monitor) {
        return FALSE;
    }

    state->workspaces[workspace].monitor_index = monitor;
    state->active_workspace[monitor] = workspace;

    return TRUE;
}

BOOL mora_workspace_place(mora_workspace_state_t* state, mora_index_t workspace, mora_index_t monitor)
{
    if (!valid_workspace(state, workspace) || monitor >= state->monitor_count) {
        return FALSE;
    }

    if (state->active_workspace[monitor] == workspace) {
        return TRUE;
    }

    if (mora_workspace_is_active(state, workspace)) {
        mora_index_t other = state->workspaces[workspace].monitor_index;
        mora_index_t displaced = state->active_workspace[monitor];

        state->active_workspace[other] = displaced;

        if (displaced != MORA_INVALID_INDEX) {
            state->workspaces[displaced].monitor_index = other;
        }
    }

    state->workspaces[workspace].monitor_index = monitor;
    state->active_workspace[monitor] = workspace;

    return TRUE;
}

BOOL mora_workspace_swap_monitors(mora_workspace_state_t* state, mora_index_t first, mora_index_t second)
{
    if (!state || first >= state->monitor_count || second >= state->monitor_count) {
        return FALSE;
    }

    mora_index_t a = state->active_workspace[first];
    mora_index_t b = state->active_workspace[second];

    /* Both monitors show a workspace whenever they exist; refuse rather than
     * index workspaces[] with MORA_INVALID_INDEX if that invariant ever breaks. */
    if (a == MORA_INVALID_INDEX || b == MORA_INVALID_INDEX) {
        return FALSE;
    }

    state->workspaces[a].monitor_index = second;
    state->workspaces[b].monitor_index = first;
    state->active_workspace[first] = b;
    state->active_workspace[second] = a;

    return TRUE;
}

BOOL mora_workspaces_set_monitors(mora_workspace_state_t* state, size_t monitor_count,
    const mora_index_t* old_to_new, const mora_index_t target[MORA_WORKSPACE_COUNT],
    const BOOL preferred[MORA_WORKSPACE_COUNT])
{
    if (!state || !state->monitor_count || !old_to_new || !target || !preferred ||
        monitor_count == 0 || monitor_count > MORA_WORKSPACE_COUNT) {
        return FALSE;
    }

    for (mora_index_t w = 0; w < MORA_WORKSPACE_COUNT; ++w) {
        if (target[w] >= monitor_count) {
            return FALSE;
        }
    }

    mora_index_t active[MORA_WORKSPACE_COUNT];
    BOOL shown[MORA_WORKSPACE_COUNT] = { 0 };

    for (mora_index_t m = 0; m < MORA_WORKSPACE_COUNT; ++m) {
        active[m] = MORA_INVALID_INDEX;
    }

    for (mora_index_t m = 0; m < state->monitor_count; ++m) {
        mora_index_t moved = old_to_new[m];

        if (moved >= monitor_count) {
            continue;
        }

        active[moved] = state->active_workspace[m];
        shown[active[moved]] = TRUE;
    }

    /* Every new monitor needs one of the workspaces not already shown. Pick
     * in three widening passes so the obvious candidates win: first the
     * workspaces the configuration prefers for that monitor, then any
     * workspace whose configured monitor is this one, then whatever is left.
     * Surviving monitors (a monitor was unplugged) keep their workspace. */
    for (int pass = 0; pass < 3; ++pass) {
        for (mora_index_t m = 0; m < monitor_count; ++m) {
            for (mora_index_t w = 0; w < MORA_WORKSPACE_COUNT && active[m] == MORA_INVALID_INDEX; ++w) {
                if (shown[w] || (pass < 2 && target[w] != m) || (pass == 0 && !preferred[w])) {
                    continue;
                }

                active[m] = w;
                shown[w] = TRUE;
            }
        }
    }

    for (mora_index_t w = 0; w < MORA_WORKSPACE_COUNT; ++w) {
        state->workspaces[w].monitor_index = target[w];
    }

    for (mora_index_t m = 0; m < MORA_WORKSPACE_COUNT; ++m) {
        state->active_workspace[m] = m < monitor_count ? active[m] : MORA_INVALID_INDEX;

        if (m < monitor_count) {
            state->workspaces[active[m]].monitor_index = m;
        }
    }

    state->monitor_count = monitor_count;

    return TRUE;
}

mora_index_t mora_workspace_find_window(const mora_workspace_state_t* state, HWND hwnd)
{
    if (!state || !hwnd) {
        return MORA_INVALID_INDEX;
    }

    for (mora_index_t i = 0; i < state->windows.count; ++i) {
        if (state->windows.entries[i].hwnd == hwnd) {
            return i;
        }
    }

    return MORA_INVALID_INDEX;
}

static HWND focus_after(const mora_workspace_state_t* state, mora_index_t index)
{
    mora_index_t workspace = state->windows.entries[index].workspace_index;
    HWND first = NULL;

    for (mora_index_t i = 0; i < state->windows.count; ++i) {
        const tracked_window_t* window = &state->windows.entries[i];

        if (i == index || window->workspace_index != workspace) {
            continue;
        }

        if (i > index) {
            return window->hwnd;
        }

        if (!first) {
            first = window->hwnd;
        }
    }

    return first;
}

static tracked_window_t take_window(mora_workspace_state_t* state, mora_index_t index)
{
    window_array_t* windows = &state->windows;
    tracked_window_t window = windows->entries[index];
    workspace_t* workspace = &state->workspaces[window.workspace_index];

    if (workspace->focused_window == window.hwnd) {
        workspace->focused_window = focus_after(state, index);
    }

    --workspace->window_count;
    memmove(&windows->entries[index], &windows->entries[index + 1],
        (windows->count - index - 1) * sizeof(*windows->entries));
    --windows->count;

    return window;
}

static mora_index_t append_window(mora_workspace_state_t* state, mora_index_t workspace_index,
    const tracked_window_t* window)
{
    workspace_t* workspace = &state->workspaces[workspace_index];
    mora_index_t index = (mora_index_t)state->windows.count++;

    state->windows.entries[index] = *window;
    state->windows.entries[index].workspace_index = workspace_index;
    ++workspace->window_count;

    if (!workspace->focused_window) {
        workspace->focused_window = window->hwnd;
    }

    return index;
}

mora_index_t mora_workspace_add_window(mora_workspace_state_t* state, mora_index_t workspace, const tracked_window_t* window)
{
    if (!valid_workspace(state, workspace) || !window || !window->hwnd) {
        return MORA_INVALID_INDEX;
    }

    if (mora_workspace_find_window(state, window->hwnd) != MORA_INVALID_INDEX) {
        return MORA_INVALID_INDEX;
    }

    /* Copy first: `window` may alias array storage that allocation can move. */
    tracked_window_t copy = *window;
    window_array_t* windows = &state->windows;

    if (windows->count >= MORA_INVALID_INDEX) {
        return MORA_INVALID_INDEX;
    }

    tracked_window_t* entries = mora_array_reserve(windows->entries, &windows->capacity,
        windows->count + 1, sizeof(*entries));

    if (!entries) {
        return MORA_INVALID_INDEX;
    }

    windows->entries = entries;

    return append_window(state, workspace, &copy);
}

BOOL mora_workspace_remove_window(mora_workspace_state_t* state, HWND hwnd)
{
    mora_index_t index = mora_workspace_find_window(state, hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    (void)take_window(state, index);

    return TRUE;
}

BOOL mora_workspace_move_window(mora_workspace_state_t* state, HWND hwnd, mora_index_t workspace)
{
    if (!valid_workspace(state, workspace)) {
        return FALSE;
    }

    mora_index_t index = mora_workspace_find_window(state, hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    mora_index_t source = state->windows.entries[index].workspace_index;

    if (source == workspace) {
        return TRUE;
    }

    /* An empty hidden workspace belongs to no monitor in particular: it joins
     * the one its first window came from. */
    workspace_t* destination = &state->workspaces[workspace];

    if (destination->window_count == 0 && !mora_workspace_is_active(state, workspace)) {
        destination->monitor_index = state->workspaces[source].monitor_index;
    }

    tracked_window_t window = take_window(state, index);

    (void)append_window(state, workspace, &window);

    return TRUE;
}

BOOL mora_workspace_remember_focus(mora_workspace_state_t* state, HWND hwnd)
{
    mora_index_t index = mora_workspace_find_window(state, hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    state->workspaces[state->windows.entries[index].workspace_index].focused_window = hwnd;

    return TRUE;
}

BOOL mora_workspace_swap_windows(mora_workspace_state_t* state, HWND first_hwnd, HWND second_hwnd)
{
    mora_index_t a = mora_workspace_find_window(state, first_hwnd);
    mora_index_t b = mora_workspace_find_window(state, second_hwnd);

    if (a == MORA_INVALID_INDEX || b == MORA_INVALID_INDEX) {
        return FALSE;
    }

    tracked_window_t* entries = state->windows.entries;

    if (entries[a].workspace_index != entries[b].workspace_index) {
        return FALSE;
    }

    tracked_window_t first = entries[a];

    entries[a] = entries[b];
    entries[b] = first;

    return TRUE;
}

BOOL mora_workspace_exchange_windows(mora_workspace_state_t* state, HWND first_hwnd, HWND second_hwnd)
{
    mora_index_t a = mora_workspace_find_window(state, first_hwnd);
    mora_index_t b = mora_workspace_find_window(state, second_hwnd);

    if (a == MORA_INVALID_INDEX || b == MORA_INVALID_INDEX) {
        return FALSE;
    }

    tracked_window_t* entries = state->windows.entries;
    mora_index_t first_workspace = entries[a].workspace_index;
    mora_index_t second_workspace = entries[b].workspace_index;

    if (first_workspace == second_workspace) {
        return mora_workspace_swap_windows(state, first_hwnd, second_hwnd);
    }

    tracked_window_t first = entries[a];

    entries[a] = entries[b];
    entries[b] = first;
    entries[a].workspace_index = first_workspace;
    entries[b].workspace_index = second_workspace;
    workspace_t* first_space = &state->workspaces[first_workspace];
    workspace_t* second_space = &state->workspaces[second_workspace];

    if (first_space->focused_window == first_hwnd) {
        first_space->focused_window = second_hwnd;
    }

    if (second_space->focused_window == second_hwnd) {
        second_space->focused_window = first_hwnd;
    }

    return TRUE;
}

BOOL mora_workspace_move_to_front(mora_workspace_state_t* state, HWND hwnd)
{
    mora_index_t index = mora_workspace_find_window(state, hwnd);

    if (index == MORA_INVALID_INDEX) {
        return FALSE;
    }

    tracked_window_t* entries = state->windows.entries;
    mora_index_t front = 0;

    while (entries[front].workspace_index != entries[index].workspace_index) {
        ++front;
    }

    /* Windows of other workspaces in between shift too, keeping their order. */
    tracked_window_t window = entries[index];

    memmove(&entries[front + 1], &entries[front], (index - front) * sizeof(*entries));
    entries[front] = window;

    return TRUE;
}

BOOL mora_workspace_set_layout(mora_workspace_state_t* state, mora_index_t workspace, mora_layout_config_t layout)
{
    if (!valid_workspace(state, workspace) || !mora_layout_is_valid(layout)) {
        return FALSE;
    }

    state->workspaces[workspace].layout = layout;

    return TRUE;
}
