#include "internal.h"

#include <wchar.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

static const wchar_t MORA_APP_FRAME_HOST[] = L"ApplicationFrameHost.exe";

static BOOL CALLBACK mora_enum_child_process_proc(HWND hwnd, LPARAM lParam)
{
    DWORD* hosted_pid = (DWORD*)lParam;
    DWORD child_pid = 0;

    GetWindowThreadProcessId(hwnd, &child_pid);

    if (child_pid != 0 && child_pid != *hosted_pid) {
        *hosted_pid = child_pid;

        return FALSE;
    }

    return TRUE;
}

const wchar_t* mora_file_name(const wchar_t* path)
{
    const wchar_t* name = path ? path : L"";

    for (const wchar_t* at = name; *at; ++at) {
        if (*at == L'\\' || *at == L'/') {
            name = at + 1;
        }
    }

    return name;
}

BOOL mora_is_app_frame_host_path(const wchar_t* path)
{
    return path && _wcsicmp(mora_file_name(path), MORA_APP_FRAME_HOST) == 0;
}

BOOL mora_process_image_path(DWORD pid, wchar_t* path, size_t capacity)
{
    if (!path || capacity == 0) {
        return FALSE;
    }

    path[0] = L'\0';
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!process) {
        return FALSE;
    }

    DWORD size = (DWORD)capacity;
    BOOL result = QueryFullProcessImageNameW(process, 0, path, &size);

    CloseHandle(process);

    if (!result) {
        path[0] = L'\0';
    }

    return result;
}

/* UWP apps are hosted by ApplicationFrameHost; resolve to the hosted app's
 * process through its child windows when available. */
BOOL mora_window_image_path(HWND hwnd, wchar_t* path, size_t capacity)
{
    DWORD pid = 0;

    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == 0 || !mora_process_image_path(pid, path, capacity)) {
        return FALSE;
    }

    if (!mora_is_app_frame_host_path(path)) {
        return TRUE;
    }

    DWORD hosted_pid = pid;

    EnumChildWindows(hwnd, mora_enum_child_process_proc, (LPARAM)&hosted_pid);

    if (hosted_pid == pid) {
        return TRUE;
    }

    return mora_process_image_path(hosted_pid, path, capacity);
}

static DWORD mora_get_integrity_level(HANDLE process)
{
    HANDLE token;

    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return 0;
    }

    DWORD level = 0;
    BYTE buffer[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    DWORD size = 0;

    if (GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof(buffer), &size)) {
        PSID sid = ((TOKEN_MANDATORY_LABEL*)buffer)->Label.Sid;

        level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    }

    CloseHandle(token);

    return level;
}

/* UIPI denies SetWindowPos and ShowWindow from a lower integrity level, so a
 * window of a more privileged process can be neither tiled nor parked. The
 * frame host's level counts for UWP apps, since it owns the frame. */
BOOL mora_is_window_elevated(HWND hwnd)
{
    static DWORD own_level = MAXDWORD;

    if (own_level == MAXDWORD) {
        own_level = mora_get_integrity_level(GetCurrentProcess());
    }

    /* Without its own level MoraWM cannot compare, so it tries every window. */
    if (own_level == 0) {
        return FALSE;
    }

    DWORD pid = 0;

    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL;

    if (!process) {
        return FALSE;
    }

    DWORD level = mora_get_integrity_level(process);

    CloseHandle(process);

    return level > own_level;
}

void mora_read_process_info(HWND hwnd, mora_window_snapshot_t* snapshot)
{
    snapshot->is_elevated = mora_is_window_elevated(hwnd);

    wchar_t path[MAX_PATH];

    if (!mora_window_image_path(hwnd, path, _countof(path))) {
        return;
    }

    snapshot->is_process_blacklisted = mora_is_process_path_blacklisted(path);
    snapshot->is_process_pending = mora_is_app_frame_host_path(path);
}
