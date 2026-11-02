#include "internal.h"

#include <shlobj.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "common/paths.h"
#include "log/log.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

#define MORA_CONFIG_FILE_LIMIT (64 * 1024)

void mora_config_default_path(wchar_t* path, size_t capacity)
{
    if (!path || capacity == 0) {
        return;
    }

    if (!mora_app_data_path(L"morawm.conf", path, capacity)) {
        path[0] = L'\0';
    }
}

static char* mora_read_bytes(const wchar_t* path, DWORD* size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();

        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            mora_log(L"config: no file at %ls", path);
        }
        else {
            mora_log(L"config: cannot open %ls (error %lu)", path, error);
        }

        return NULL;
    }

    LARGE_INTEGER length = { 0 };

    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 || length.QuadPart > MORA_CONFIG_FILE_LIMIT) {
        mora_log(L"config: %ls is not a file of up to %d bytes", path, MORA_CONFIG_FILE_LIMIT);
        CloseHandle(file);

        return NULL;
    }

    char* bytes = malloc((size_t)length.QuadPart + 1);
    DWORD read = 0;
    BOOL ok = bytes && ReadFile(file, bytes, (DWORD)length.QuadPart, &read, NULL);

    CloseHandle(file);

    if (!ok || read != (DWORD)length.QuadPart) {
        free(bytes);
        mora_log(L"config: cannot read %ls (error %lu)", path, GetLastError());

        return NULL;
    }

    bytes[read] = '\0';
    *size = read;

    return bytes;
}

static wchar_t* mora_decode(const char* bytes, DWORD size)
{
    /* UTF-16LE when it has the byte order mark. */
    if (size >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) {
        size_t characters = (size - 2) / 2;
        wchar_t* text = malloc((characters + 1) * sizeof(wchar_t));

        if (!text) {
            return NULL;
        }

        memcpy(text, bytes + 2, characters * sizeof(wchar_t));
        text[characters] = L'\0';

        return text;
    }

    const char* utf8 = bytes;
    DWORD length = size;

    if (size >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB
        && (unsigned char)bytes[2] == 0xBF) {
        utf8 += 3;
        length -= 3;
    }

    /* UTF-8 first, Windows' own code page when the bytes are not valid UTF-8. */
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int wide = MultiByteToWideChar(code_page, flags, utf8, (int)length, NULL, 0);

    if (wide == 0) {
        code_page = CP_ACP;
        flags = 0;
        wide = MultiByteToWideChar(code_page, flags, utf8, (int)length, NULL, 0);
    }

    if (wide == 0 && length != 0) {
        return NULL;
    }

    wchar_t* text = malloc(((size_t)wide + 1) * sizeof(wchar_t));

    if (!text) {
        return NULL;
    }

    if (wide > 0) {
        MultiByteToWideChar(code_page, flags, utf8, (int)length, text, wide);
    }

    text[wide] = L'\0';

    return text;
}

wchar_t* mora_config_read_file(const wchar_t* path)
{
    if (!path || !path[0]) {
        return NULL;
    }

    DWORD size = 0;
    char* bytes = mora_read_bytes(path, &size);

    if (!bytes) {
        return NULL;
    }

    wchar_t* text = mora_decode(bytes, size);

    free(bytes);

    if (!text) {
        mora_log(L"config: cannot decode %ls", path);
    }

    return text;
}
