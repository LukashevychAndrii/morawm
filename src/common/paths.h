#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <wchar.h>

static inline BOOL mora_app_data_path(const wchar_t* leaf, wchar_t* out, size_t capacity)
{
    if (!leaf || !out || capacity == 0) {
        return FALSE;
    }

    out[0] = L'\0';

    wchar_t* local = NULL;

    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local))) {
        return FALSE;
    }

    int written = swprintf_s(out, capacity, L"%ls\\MoraWM", local);

    CoTaskMemFree(local);

    if (written < 0) {
        out[0] = L'\0';

        return FALSE;
    }

    (void)CreateDirectoryW(out, NULL);

    size_t length = wcslen(out);

    if (length + 1 >= capacity ||
        swprintf_s(out + length, capacity - length, L"\\%ls", leaf) < 0) {
        out[0] = L'\0';

        return FALSE;
    }

    return TRUE;
}
