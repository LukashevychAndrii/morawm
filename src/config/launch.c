#include "internal.h"

#include <shellapi.h>
#include <stdlib.h>
#include <tlhelp32.h>
#include <wchar.h>

#include "log/log.h"

#include "detection/window_detection.h"

#pragma comment(lib, "shell32.lib")

#define MORA_PARENT_WALK_DEPTH 8
#define MORA_SYSTEM_PROCESS_MAX_PID 4

static void mora_split_command(const wchar_t* command, wchar_t* file, size_t file_capacity,
    const wchar_t** parameters)
{
    const wchar_t* at = command;

    while (*at == L' ' || *at == L'\t') {
        ++at;
    }

    const wchar_t* start = at;
    const wchar_t* stop;

    if (*at == L'"') {
        start = ++at;
        const wchar_t* closing = wcschr(at, L'"');

        stop = closing ? closing : at + wcslen(at);
        at = *stop ? stop + 1 : stop;
    } else {
        stop = at;

        while (*stop && *stop != L' ' && *stop != L'\t') {
            ++stop;
        }
    }

    size_t length = (size_t)(stop - start);

    if (length >= file_capacity) {
        length = file_capacity - 1;
    }

    wmemcpy(file, start, length);
    file[length] = L'\0';

    while (*at == L' ' || *at == L'\t') {
        ++at;
    }

    *parameters = *at ? at : NULL;
}

BOOL mora_config_spawn(const wchar_t* command, DWORD* pid)
{
    if (!command || !command[0]) {
        return FALSE;
    }

    if (pid) {
        *pid = 0;
    }

    /* CreateProcessW may write to the command line buffer. */
    wchar_t line[MORA_CONFIG_COMMAND_CAPACITY];

    if (wcscpy_s(line, _countof(line), command) != 0) {
        return FALSE;
    }

    STARTUPINFOW startup = { .cb = sizeof(startup) };
    PROCESS_INFORMATION process = { 0 };

    if (CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        if (pid) {
            *pid = process.dwProcessId;
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        return TRUE;
    }

    DWORD create_error = GetLastError();

    /* ShellExecute resolves what CreateProcessW does not: .cmd and .bat
     * scripts, documents, URLs, and programs registered via App Paths. */
    wchar_t file[MORA_CONFIG_COMMAND_CAPACITY];
    const wchar_t* parameters = NULL;

    mora_split_command(command, file, _countof(file), &parameters);
    SHELLEXECUTEINFOW info = {
        .cbSize = sizeof(info),
        .fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI,
        .lpFile = file,
        .lpParameters = parameters,
        .nShow = SW_SHOWNORMAL,
    };

    if (ShellExecuteExW(&info)) {
        if (pid && info.hProcess) {
            *pid = GetProcessId(info.hProcess);
        }

        if (info.hProcess) {
            CloseHandle(info.hProcess);
        }

        return TRUE;
    }

    mora_log(L"config: launch failed (error %lu, shell error %lu): %ls",
        create_error, GetLastError(), command);

    return FALSE;
}

static DWORD mora_parent_of(HANDLE snapshot, DWORD pid)
{
    PROCESSENTRY32W entry = { .dwSize = sizeof(entry) };

    for (BOOL found = Process32FirstW(snapshot, &entry); found; found = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == pid) {
            return entry.th32ParentProcessID;
        }

        entry.dwSize = sizeof(entry);
    }

    return 0;
}

BOOL mora_config_process_matches(DWORD pid, DWORD ancestor)
{
    if (!pid || !ancestor) {
        return FALSE;
    }

    if (pid == ancestor) {
        return TRUE;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    /* Bounded: launcher chains are short, and a bad parent id would not loop. */
    BOOL matched = FALSE;
    DWORD walk = pid;

    for (int depth = 0; depth < MORA_PARENT_WALK_DEPTH && walk > MORA_SYSTEM_PROCESS_MAX_PID; ++depth) {
        DWORD parent = mora_parent_of(snapshot, walk);

        if (!parent || parent == walk) {
            break;
        }

        if (parent == ancestor) {
            matched = TRUE;
            break;
        }

        walk = parent;
    }

    CloseHandle(snapshot);

    return matched;
}

static BOOL mora_process_is_program(const wchar_t* name, const PROCESSENTRY32W* entry)
{
    if (_wcsicmp(name, entry->szExeFile) == 0) {
        return TRUE;
    }

    if (wcschr(name, L'.')) {
        return FALSE;
    }

    size_t length = wcslen(name);

    return wcslen(entry->szExeFile) > length && entry->szExeFile[length] == L'.'
        && _wcsnicmp(name, entry->szExeFile, length) == 0;
}

size_t mora_config_running_pids(const wchar_t* command, DWORD* pids, size_t capacity)
{
    if (!command || !command[0] || !pids || capacity == 0) {
        return 0;
    }

    wchar_t file[MORA_CONFIG_COMMAND_CAPACITY];
    const wchar_t* parameters = NULL;

    mora_split_command(command, file, _countof(file), &parameters);
    const wchar_t* name = mora_file_name(file);

    if (!*name) {
        return 0;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    size_t count = 0;
    PROCESSENTRY32W entry = { .dwSize = sizeof(entry) };

    for (BOOL found = Process32FirstW(snapshot, &entry); found; found = Process32NextW(snapshot, &entry)) {
        if (mora_process_is_program(name, &entry)) {
            pids[count++] = entry.th32ProcessID;

            if (count == capacity) {
                break;
            }
        }

        entry.dwSize = sizeof(entry);
    }

    CloseHandle(snapshot);

    return count;
}
