#include "app.h"

#include "bar/bar.h"

#pragma comment(lib, "user32.lib")

#define MORA_TIMER_RETILE 100
#define MORA_RETILE_DELAY_MS 50
#define MORA_TIMER_PENDING 101
#define MORA_TIMER_VERIFY 102
#define MORA_TIMER_MONITORS 103
#define MORA_MONITOR_SETTLE_MS 300

static const struct { DWORD min; DWORD max; } MORA_HOOKED_EVENTS[] = {
    { EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW },
    { EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE },
    { EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY },
    { EVENT_OBJECT_CLOAKED, EVENT_OBJECT_CLOAKED },
    { EVENT_OBJECT_UNCLOAKED, EVENT_OBJECT_UNCLOAKED },
    { EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND },
    { EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND },
    { EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE },
    { EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND },
    { EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE },
};

static HWINEVENTHOOK g_event_hooks[_countof(MORA_HOOKED_EVENTS)];
static BOOL g_pending_timer_active = FALSE;
static BOOL g_verify_timer_active = FALSE;

void mora_events_schedule_verification(void)
{
    if (!g_verify_timer_active && mora_manager_layout_pending() && mora_main_hwnd()) {
        g_verify_timer_active = SetTimer(mora_main_hwnd(), MORA_TIMER_VERIFY, MORA_PLACEMENT_SETTLE_MS, NULL) != 0;
    }
}

void mora_request_retile(void)
{
    HWND main_hwnd = mora_main_hwnd();

    if (main_hwnd) {
        PostMessageW(main_hwnd, MORA_WM_RETILE, 0, 0);
    }
}

void mora_request_monitor_refresh(void)
{
    HWND main_hwnd = mora_main_hwnd();

    if (main_hwnd) {
        SetTimer(main_hwnd, MORA_TIMER_MONITORS, MORA_MONITOR_SETTLE_MS, NULL);
    }
}

/* Keep polling deferred windows while any remain, without resetting the
 * interval on every event. */
static void mora_schedule_pending_retry(void)
{
    if (g_pending_timer_active || !mora_manager_has_pending_windows() || !mora_main_hwnd()) {
        return;
    }

    g_pending_timer_active = SetTimer(mora_main_hwnd(), MORA_TIMER_PENDING, MORA_PENDING_RETRY_MS, NULL) != 0;
}

static void CALLBACK mora_win_event_proc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    (void)hook; (void)dwEventThread; (void)dwmsEventTime;

    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
        return;
    }

    if (event == EVENT_OBJECT_NAMECHANGE) {
        mora_bars_refresh_title(hwnd);

        return;
    }

    if ((event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_HIDE) && mora_is_taskbar(hwnd)) {
        mora_request_monitor_refresh();

        return;
    }

    BOOL changed = mora_manager_handle_event(event, hwnd);

    if (event == EVENT_OBJECT_LOCATIONCHANGE && !changed) {
        if (hwnd == GetForegroundWindow()) {
            mora_bars_sync_fullscreen();
        }

        return;
    }

    if (changed) {
        mora_request_retile();
    }

    mora_schedule_pending_retry();
    mora_bars_refresh();
}

void mora_event_hooks_shutdown(void)
{
    for (size_t i = 0; i < _countof(g_event_hooks); ++i) {
        if (g_event_hooks[i]) {
            UnhookWinEvent(g_event_hooks[i]);
            g_event_hooks[i] = NULL;
        }
    }
}

BOOL mora_event_hooks_init(void)
{
    for (size_t i = 0; i < _countof(MORA_HOOKED_EVENTS); ++i) {
        g_event_hooks[i] = SetWinEventHook(MORA_HOOKED_EVENTS[i].min, MORA_HOOKED_EVENTS[i].max,
            NULL, mora_win_event_proc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

        if (!g_event_hooks[i]) {
            mora_show_error(L"Window event setup failed", L"MoraWM could not install the Windows event hooks.");

            return FALSE;
        }
    }

    return TRUE;
}

void mora_events_on_retile(HWND hwnd)
{
    SetTimer(hwnd, MORA_TIMER_RETILE, MORA_RETILE_DELAY_MS, NULL);
}

BOOL mora_events_on_timer(HWND hwnd, WPARAM id)
{
    if (id == MORA_TIMER_VERIFY) {
        if (mora_manager_verify_layout()) {
            mora_bars_refresh();
        }

        if (!mora_manager_layout_pending()) {
            KillTimer(hwnd, MORA_TIMER_VERIFY);
            g_verify_timer_active = FALSE;
        }

        return TRUE;
    }

    if (id == MORA_TIMER_RETILE) {
        KillTimer(hwnd, MORA_TIMER_RETILE);
        mora_manager_tile();
        mora_bars_refresh();

        return TRUE;
    }

    if (id == MORA_TIMER_MONITORS) {
        KillTimer(hwnd, MORA_TIMER_MONITORS);
        mora_refresh_monitors();

        return TRUE;
    }

    if (id == MORA_TIMER_PENDING) {
        if (mora_manager_retry_pending_windows()) {
            mora_request_retile();
        }

        if (!mora_manager_has_pending_windows()) {
            KillTimer(hwnd, MORA_TIMER_PENDING);
            g_pending_timer_active = FALSE;
        }

        return TRUE;
    }

    return FALSE;
}
