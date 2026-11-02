#include "log/log.h"

#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#include "common/paths.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

#define MORA_LOG_LINE_CAPACITY 1024
#define MORA_LOG_OLD_SUFFIX L".old"

static HANDLE g_file = INVALID_HANDLE_VALUE;
static wchar_t g_path[MAX_PATH];
static DWORD g_limit;
static ULONGLONG g_size;

static BOOL mora_log_create(void)
{
    g_file = CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (g_file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    LARGE_INTEGER size;

    g_size = GetFileSizeEx(g_file, &size) ? (ULONGLONG)size.QuadPart : 0;

    return TRUE;
}

static void mora_log_roll_over(void)
{
    wchar_t old_path[MAX_PATH + ARRAYSIZE(MORA_LOG_OLD_SUFFIX)];

    swprintf_s(old_path, _countof(old_path), L"%ls" MORA_LOG_OLD_SUFFIX, g_path);
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
    (void)MoveFileExW(g_path, old_path, MOVEFILE_REPLACE_EXISTING);
    (void)mora_log_create();
}

BOOL mora_log_open_path(const wchar_t* path, DWORD limit)
{
    mora_log_close();

    if (wcscpy_s(g_path, _countof(g_path), path) != 0) {
        return FALSE;
    }

    g_limit = limit;

    if (!mora_log_create()) {
        return FALSE;
    }

    if (g_size >= g_limit) {
        mora_log_roll_over();
    }

    return g_file != INVALID_HANDLE_VALUE;
}

BOOL mora_log_open(void)
{
    wchar_t path[MAX_PATH];

    if (!mora_app_data_path(L"morawm.log", path, _countof(path))) {
        return FALSE;
    }

    return mora_log_open_path(path, MORA_LOG_LIMIT);
}

void mora_log_close(void)
{
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
    }

    g_file = INVALID_HANDLE_VALUE;
}

void mora_log(_Printf_format_string_ const wchar_t* format, ...)
{
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now;

    GetLocalTime(&now);
    wchar_t line[MORA_LOG_LINE_CAPACITY];
    int prefix = swprintf_s(line, _countof(line), L"%04u-%02u-%02u %02u:%02u:%02u.%03u  ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    va_list args;

    va_start(args, format);
    /* Leaves room for the line break; a longer message is cut short. */
    (void)_vsnwprintf_s(line + prefix, _countof(line) - prefix - 2, _TRUNCATE, format, args);
    va_end(args);
    wcscat_s(line, _countof(line), L"\r\n");

    char utf8[sizeof(line) * 3 / sizeof(wchar_t)];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), NULL, NULL);

    if (bytes <= 1) {
        return;
    }

    DWORD written = 0;

    if (WriteFile(g_file, utf8, (DWORD)(bytes - 1), &written, NULL)) {
        g_size += written;
    }

    if (g_size >= g_limit) {
        mora_log_roll_over();
    }
}
