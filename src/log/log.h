#pragma once

#include <Windows.h>

/* Always-on log for bug reports from release builds, in
 * %LOCALAPPDATA%\MoraWM\morawm.log. Every line is written through at once, so a
 * crash loses nothing. Logging while the log is closed does nothing, so any
 * module can log freely; only the entry point opens it. */

#define MORA_LOG_LIMIT (1024 * 1024)

BOOL mora_log_open(void);
BOOL mora_log_open_path(const wchar_t* path, DWORD limit);
void mora_log_close(void);
void mora_log(_Printf_format_string_ const wchar_t* format, ...);
