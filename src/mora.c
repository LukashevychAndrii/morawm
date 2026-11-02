#include <Windows.h>
#include <shellscalingapi.h>

#include "app/app.h"
#include "bar/bar.h"
#include "common/version.h"
#include "config/config.h"
#include "manager/manager.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "Shcore.lib")

#define MORA_MUTEX_NAME L"MORA_IS_CURRENTLY_RUNNING"
#ifdef DEBUG
#define MORA_BUILD L"debug"
#else
#define MORA_BUILD L"release"
#endif

static HANDLE g_mutex = NULL;
BOOL g_manager_initialized = FALSE;

static BOOL mora_acquire_single_instance(void)
{
    g_mutex = CreateMutexW(NULL, TRUE, MORA_MUTEX_NAME);

    if (!g_mutex) {
        OutputDebugStringW(L"Failed creating mutex\n");

        return FALSE;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        OutputDebugStringW(L"MoraWM is already running\n");
        CloseHandle(g_mutex);
        g_mutex = NULL;

        return FALSE;
    }

    return TRUE;
}

static void mora_release_single_instance(void)
{
    if (!g_mutex) {
        return;
    }

    ReleaseMutex(g_mutex);
    CloseHandle(g_mutex);
    g_mutex = NULL;
}

static BOOL mora_is_elevated(void)
{
    HANDLE token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return FALSE;
    }

    TOKEN_ELEVATION elevation = { 0 };
    DWORD size = 0;
    BOOL elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
        elevation.TokenIsElevated;
    CloseHandle(token);

    return elevated;
}

static BOOL mora_startup(HINSTANCE instance)
{
    /* Bars exist before the manager: it focuses them for empty workspaces and
     * keeps the space they reserve free of tiles. */
    if (!mora_detect_monitors()) {
        return FALSE;
    }

    size_t monitor_count = 0;
    mora_monitor_t* monitors = mora_monitors(&monitor_count);

    if (!mora_bars_create(instance, monitors, monitor_count, mora_handle_bar_message)) {
        mora_show_error(L"Failed to create bars", L"MoraWM could not create its bar windows.");

        return FALSE;
    }

    if (!mora_manager_init(monitors, monitor_count)) {
        mora_show_error(L"Workspace initialization failed", L"MoraWM supports one to nine monitors.");

        return FALSE;
    }

    g_manager_initialized = TRUE;
    mora_config_load();
    mora_config_apply_workspaces();

    if (!mora_register_hotkeys() || !mora_event_hooks_init()) {
        return FALSE;
    }

    mora_disable_animations();
    mora_manager_recover_windows();
    mora_manager_detect_windows();
    mora_manager_tile();
    mora_events_schedule_verification();
    mora_bars_refresh();
    mora_config_launch();

    return TRUE;
}

static void mora_cleanup(void)
{
    mora_event_hooks_shutdown();
    mora_restore_animations();

    if (g_manager_initialized) {
        mora_manager_dispose();
        g_manager_initialized = FALSE;
    }

    mora_config_dispose();
    mora_unregister_hotkeys();
    mora_bars_destroy();
    mora_forget_monitors();
    mora_release_single_instance();
}

static int mora_run_message_loop(void)
{
    MSG msg;
    BOOL result;

    while ((result = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        mora_events_schedule_verification();
    }

    return result == -1 ? 1 : (int)msg.wParam;
}

static int mora_run(HINSTANCE instance)
{
    (void)mora_log_open();

    if (!mora_acquire_single_instance()) {
        mora_log(L"not started: MoraWM is already running");
        mora_log_close();

        return 1;
    }

    mora_log(L"start: pid %lu, MoraWM %ls, %ls build, %ls", GetCurrentProcessId(), MORA_VERSION_WIDE,
        MORA_BUILD, mora_is_elevated() ? L"elevated" : L"not elevated: windows of elevated programs are not tiled");
    (void)SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    mora_install_recovery();

    int exit_code = mora_startup(instance) ? mora_run_message_loop() : 1;

    mora_cleanup();
    mora_log(L"stop: exit code %d", exit_code);
    mora_log_close();

    return exit_code;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    (void)hPrevInstance; (void)lpCmdLine; (void)nShowCmd;

    return mora_run(hInstance);
}
