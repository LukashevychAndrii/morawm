#pragma once

/* Shared between the application's source files: the pieces src/mora.c wires
 * together at startup. Call only from the thread running the message loop. */

#include <Windows.h>

#include "common/dpi.h"
#include "common/geometry.h"
#include "common/limits.h"
#include "log/log.h"
#include "manager/manager.h"

/* Posted to the main bar; retiling is debounced so bursts of events coalesce. */
#define MORA_WM_RETILE (WM_USER + 1)

static inline void mora_show_error(const wchar_t* title, const wchar_t* message)
{
    mora_log(L"error: %ls: %ls", title, message);
    MessageBoxW(NULL, message, title, MB_ICONERROR | MB_OK);
}

extern BOOL g_manager_initialized;

BOOL mora_detect_monitors(void);
mora_monitor_t* mora_monitors(size_t* count);
void mora_forget_monitors(void);
/* The first bar owns hotkeys and timers. */
HWND mora_main_hwnd(void);
BOOL mora_is_taskbar(HWND hwnd);
void mora_refresh_monitors(void);

void mora_disable_animations(void);
void mora_restore_animations(void);

void mora_install_recovery(void);
/* Put the desktop back without touching MoraWM's own resources. */
void mora_restore_desktop(void);

BOOL mora_register_hotkeys(void);
void mora_unregister_hotkeys(void);

BOOL mora_event_hooks_init(void);
void mora_event_hooks_shutdown(void);
void mora_request_retile(void);
/* Displays report changes in bursts; refresh monitors once they settle. */
void mora_request_monitor_refresh(void);
void mora_events_on_retile(HWND hwnd);
void mora_events_schedule_verification(void);
BOOL mora_events_on_timer(HWND hwnd, WPARAM id);

LRESULT mora_handle_bar_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, BOOL* handled);
