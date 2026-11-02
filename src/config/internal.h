#pragma once

#include <Windows.h>

#include "config/config.h"
#include "workspace/workspace.h"

#define MORA_CONFIG_COMMAND_CAPACITY 1024
#define MORA_CONFIG_MAX_PROCESSES 64
/* Windows launched by an exec rule are still routed this long after startup. */
#define MORA_CONFIG_MATCH_MS 60000

typedef struct {
    mora_index_t workspace;
    mora_index_t monitor;
} mora_config_placement_t;

typedef struct {
    wchar_t command[MORA_CONFIG_COMMAND_CAPACITY];
    mora_config_placement_t placement;

    /* Set once the launch succeeded; a failed launch never matches. */
    DWORD pid;
    ULONGLONG deadline;
    BOOL launched;
} mora_config_launch_t;

typedef struct {
    mora_config_placement_t* placements;
    size_t placement_count;
    size_t placement_capacity;
    mora_config_launch_t* launches;
    size_t launch_count;
    size_t launch_capacity;
} mora_config_t;

void mora_config_parse(const wchar_t* text, mora_config_t* config);
void mora_config_free(mora_config_t* config);

void mora_config_load_path(const wchar_t* path);
void mora_config_default_path(wchar_t* path, size_t capacity);
wchar_t* mora_config_read_file(const wchar_t* path);

BOOL mora_config_spawn(const wchar_t* command, DWORD* pid);
size_t mora_config_running_pids(const wchar_t* command, DWORD* pids, size_t capacity);
BOOL mora_config_process_matches(DWORD pid, DWORD ancestor);
