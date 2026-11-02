#include "internal.h"

#include "common/array.h"
#include "detection/window_detection.h"

/* A freshly created UWP frame may not host its app yet, so it is classified
 * again every MORA_PENDING_RETRY_MS until it resolves or the attempts run out. */

mora_pending_list_t g_pending;

static size_t mora_find_pending(HWND hwnd)
{
    for (size_t i = 0; i < g_pending.count; ++i) {
        if (g_pending.entries[i].hwnd == hwnd) {
            return i;
        }
    }

    return SIZE_MAX;
}

void mora_forget_pending(HWND hwnd)
{
    size_t i = mora_find_pending(hwnd);

    if (i != SIZE_MAX) {
        g_pending.entries[i] = g_pending.entries[--g_pending.count];
    }
}

void mora_manager_defer_window(HWND hwnd)
{
    if (!hwnd || mora_find_pending(hwnd) != SIZE_MAX) {
        return;
    }

    mora_pending_window_t* entries = mora_array_reserve(g_pending.entries, &g_pending.capacity,
        g_pending.count + 1, sizeof(*entries));

    if (!entries) {
        mora_die_out_of_memory(L"Could not allocate pending windows.");
    }

    g_pending.entries = entries;
    g_pending.entries[g_pending.count++] = (mora_pending_window_t){ .hwnd = hwnd };
}

BOOL mora_manager_has_pending_windows(void)
{
    return g_pending.count != 0;
}

BOOL mora_manager_retry_pending_windows(void)
{
    BOOL added = FALSE;
    size_t kept = 0;

    for (size_t i = 0; i < g_pending.count; ++i) {
        mora_pending_window_t pending = g_pending.entries[i];
        mora_window_filter_result_t result = mora_classify_window(pending.hwnd);
        BOOL gave_up = ++pending.attempts >= MORA_PENDING_MAX_ATTEMPTS;

        if (result == MORA_WINDOW_FILTER_UWP_PENDING && !gave_up) {
            g_pending.entries[kept++] = pending;
            continue;
        }

        if (result == MORA_WINDOW_FILTER_ACCEPTED || result == MORA_WINDOW_FILTER_UWP_PENDING) {
            added |= mora_add_window(pending.hwnd, TRUE);
        }
    }

    g_pending.count = kept;

    if (added) {
        mora_sync_workspace_visibility();
    }

    return added;
}
