#include "app.h"

#include <dbghelp.h>
#include <stdlib.h>

#pragma comment(lib, "dbghelp.lib")

#define MORA_STACK_CAPTURE_FRAMES 32
#define MORA_SYMBOL_BUFFER_CAPACITY 256
#define MORA_SYMBOL_NAME_CAPACITY 255
#define MORA_ACCESS_WRITE 1
#define MORA_ACCESS_EXECUTE 8
#define MORA_ACCESS_VIOLATION_PARAMETERS 2

void mora_restore_desktop(void)
{
    static volatile LONG restoring = 0;

    if (InterlockedExchange(&restoring, 1)) {
        return;
    }

    if (g_manager_initialized) {
        mora_manager_restore_windows();
    }

    mora_restore_animations();
    restoring = 0;
}

static void mora_log_crash_stack(EXCEPTION_POINTERS* exception)
{
    if (!exception || !exception->ContextRecord) {
        return;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

    if (!SymInitialize(process, NULL, TRUE)) {
        return;
    }

    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame = { 0 };

    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < MORA_STACK_CAPTURE_FRAMES; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, NULL,
                SymFunctionTableAccess64, SymGetModuleBase64, NULL) || !frame.AddrPC.Offset) {
            break;
        }

        char buffer[sizeof(SYMBOL_INFO) + MORA_SYMBOL_BUFFER_CAPACITY];
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;

        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MORA_SYMBOL_NAME_CAPACITY;
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line = { .SizeOfStruct = sizeof(line) };
        DWORD line_displacement = 0;
        BOOL has_line = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line);

        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            mora_log(L"  #%d %S +0x%llX (%S:%lu)", i, symbol->Name, displacement,
                has_line ? line.FileName : "", has_line ? line.LineNumber : 0);
        }
        else {
            mora_log(L"  #%d %p", i, (void*)frame.AddrPC.Offset);
        }
    }

    SymCleanup(process);
}

static LONG WINAPI mora_crash_handler(EXCEPTION_POINTERS* exception)
{
    const EXCEPTION_RECORD* record = exception ? exception->ExceptionRecord : NULL;

    mora_log(L"crash: exception 0x%08lX at %p", record ? record->ExceptionCode : 0,
        record ? record->ExceptionAddress : NULL);

    if (record && record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= MORA_ACCESS_VIOLATION_PARAMETERS) {
        const wchar_t* kind = record->ExceptionInformation[0] == MORA_ACCESS_WRITE ? L"writing" :
            record->ExceptionInformation[0] == MORA_ACCESS_EXECUTE ? L"executing" : L"reading";
        mora_log(L"  %ls %p", kind, (void*)(ULONG_PTR)record->ExceptionInformation[1]);
    }

    mora_log_crash_stack(exception);
    /* Best effort: state may be corrupt, and a second fault ends the process. */
    mora_restore_desktop();

    return EXCEPTION_CONTINUE_SEARCH;
}

static void mora_restore_desktop_at_exit(void)
{
    mora_restore_desktop();
}

void mora_install_recovery(void)
{
    SetUnhandledExceptionFilter(mora_crash_handler);
    atexit(mora_restore_desktop_at_exit);
}
