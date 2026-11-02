#include "internal.h"

#include <stdlib.h>

#include "common/array.h"
#include "log/log.h"
#include "manager/manager.h"

static mora_config_t g_config;

static BOOL mora_route_window(const tracked_window_t* window, mora_index_t* workspace)
{
    if (!window || !window->pid) {
        return FALSE;
    }

    ULONGLONG now = GetTickCount64();

    for (size_t i = 0; i < g_config.launch_count; ++i) {
        const mora_config_launch_t* rule = &g_config.launches[i];

        if (!rule->launched || now >= rule->deadline) {
            continue;
        }

        if (rule->placement.workspace == MORA_INVALID_INDEX && rule->placement.monitor == MORA_INVALID_INDEX) {
            continue;
        }

        if (!mora_config_process_matches(window->pid, rule->pid)) {
            continue;
        }

        mora_index_t target = rule->placement.workspace;

        if (target == MORA_INVALID_INDEX) {
            const mora_workspace_state_t* state = mora_manager_state();

            target = rule->placement.monitor < state->monitor_count
                ? state->active_workspace[rule->placement.monitor]
                : MORA_INVALID_INDEX;

            if (target == MORA_INVALID_INDEX) {
                continue;
            }
        }

        *workspace = target;
        mora_log(L"config: pid %lu goes to workspace %u", window->pid, target + 1);

        return TRUE;
    }

    return FALSE;
}

void mora_config_load_path(const wchar_t* path)
{
    mora_config_free(&g_config);
    wchar_t* text = mora_config_read_file(path);

    if (!text) {
        return;
    }

    mora_config_parse(text, &g_config);
    free(text);
    mora_log(L"config: %ls: %zu placement(s), %zu launch(es)", path,
        g_config.placement_count, g_config.launch_count);
}

void mora_config_load(void)
{
    wchar_t path[MAX_PATH];

    mora_config_default_path(path, _countof(path));

    if (!path[0]) {
        mora_log(L"config: cannot resolve the local app data folder");

        return;
    }

    mora_config_load_path(path);
}

void mora_config_apply_workspaces(void)
{
    const mora_workspace_state_t* state = mora_manager_state();

    for (size_t i = 0; i < g_config.placement_count; ++i) {
        const mora_config_placement_t* placement = &g_config.placements[i];

        if (placement->monitor >= state->monitor_count) {
            mora_log(L"config: workspace %u: monitor %u is not connected",
                placement->workspace + 1, placement->monitor + 1);
            continue;
        }

        if (mora_manager_configure_workspace(placement->workspace, placement->monitor)) {
            mora_log(L"config: workspace %u is shown on monitor %u",
                placement->workspace + 1, placement->monitor + 1);
        }
        else {
            mora_log(L"config: workspace %u cannot be shown on monitor %u",
                placement->workspace + 1, placement->monitor + 1);
        }
    }
}

/* The workspace a rule sends windows to: its own, or the one shown on its
 * monitor; MORA_INVALID_INDEX when the rule names neither. */
static mora_index_t mora_rule_workspace(const mora_config_launch_t* rule,
    const mora_workspace_state_t* state)
{
    if (rule->placement.workspace != MORA_INVALID_INDEX) {
        return rule->placement.workspace;
    }

    if (rule->placement.monitor == MORA_INVALID_INDEX || rule->placement.monitor >= state->monitor_count) {
        return MORA_INVALID_INDEX;
    }

    return state->active_workspace[rule->placement.monitor];
}

static size_t mora_move_program_windows(const DWORD* pids, size_t pid_count,
    mora_index_t workspace)
{
    const mora_workspace_state_t* state = mora_manager_state();
    HWND* windows = NULL;
    size_t count = 0;
    size_t capacity = 0;

    /* A move reorders the tracked windows, so they are collected first. */
    for (size_t i = 0; i < state->windows.count; ++i) {
        const tracked_window_t* window = &state->windows.entries[i];

        if (window->workspace_index == workspace) {
            continue;
        }

        BOOL match = FALSE;

        for (size_t p = 0; p < pid_count && !match; ++p) {
            match = window->pid == pids[p] || mora_config_process_matches(window->pid, pids[p]);
        }

        if (!match) {
            continue;
        }

        HWND* grown = mora_array_reserve(windows, &capacity, count + 1, sizeof(*grown));

        if (!grown) {
            mora_log(L"config: out of memory, a window stays where it is");
            break;
        }

        windows = grown;
        windows[count++] = window->hwnd;
    }

    for (size_t i = 0; i < count; ++i) {
        mora_manager_move_window_to_workspace(windows[i], workspace);
    }

    free(windows);

    return count;
}

/* A rule whose program is already running starts nothing: the program's
 * windows move to the rule's workspace instead. FALSE to launch as usual. */
static BOOL mora_adopt_running_program(mora_config_launch_t* rule, ULONGLONG now)
{
    DWORD pids[MORA_CONFIG_MAX_PROCESSES];
    size_t pid_count = mora_config_running_pids(rule->command, pids, _countof(pids));

    if (pid_count == 0) {
        return FALSE;
    }

    const mora_workspace_state_t* state = mora_manager_state();
    mora_index_t workspace = mora_rule_workspace(rule, state);

    if (workspace == MORA_INVALID_INDEX) {
        mora_log(L"config: pid %lu already runs, no target: %ls", pids[0], rule->command);
    } else {
        size_t moved = mora_move_program_windows(pids, pid_count, workspace);

        mora_log(L"config: pid %lu already runs: %zu window(s) go to workspace %u", pids[0],
            moved, workspace + 1);
    }

    /* Its windows keep being routed for a minute, like a launch's, so one
     * opened right after startup still lands in the workspace. */
    rule->launched = TRUE;
    rule->pid = pids[0];
    rule->deadline = now + MORA_CONFIG_MATCH_MS;

    return TRUE;
}

void mora_config_launch(void)
{
    if (g_config.launch_count == 0) {
        return;
    }

    mora_manager_set_window_router(mora_route_window);

    const mora_workspace_state_t* state = mora_manager_state();
    ULONGLONG now = GetTickCount64();

    for (size_t i = 0; i < g_config.launch_count; ++i) {
        mora_config_launch_t* rule = &g_config.launches[i];

        if (mora_adopt_running_program(rule, now)) {
            continue;
        }

        if (rule->placement.monitor != MORA_INVALID_INDEX && rule->placement.monitor >= state->monitor_count) {
            mora_log(L"config: monitor %u is not connected, launching anyway: %ls",
                rule->placement.monitor + 1, rule->command);
            rule->placement.monitor = MORA_INVALID_INDEX;
        }

        DWORD pid = 0;

        if (!mora_config_spawn(rule->command, &pid)) {
            continue;
        }

        rule->launched = TRUE;
        rule->pid = pid;
        rule->deadline = now + MORA_CONFIG_MATCH_MS;
        mora_log(L"config: launched pid %lu: %ls", pid, rule->command);
    }
}

void mora_config_dispose(void)
{
    mora_manager_set_window_router(NULL);
    mora_config_free(&g_config);
}
