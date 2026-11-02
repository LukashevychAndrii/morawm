#pragma once

#include <Windows.h>
#include <stdarg.h>
#include <wchar.h>

#define MORA_DEBUG_MESSAGE_CAPACITY 512

static inline void mora_debug_log(_Printf_format_string_ const wchar_t* format, ...)
{
#ifdef DEBUG
    wchar_t buffer[MORA_DEBUG_MESSAGE_CAPACITY];
    va_list args;

    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringW(buffer);
#else
    (void)format;
#endif
}
