#pragma once

#include "common/limits.h"
#include "detection/window_detection.h"

mora_window_snapshot_t mora_get_window_snapshot(HWND hwnd);

/* window_process.c: the expensive process lookup, filling in the
 * snapshot's process fields. A freshly created UWP frame may not host its app
 * yet; that is reported as pending rather than waited for, so callers on a UI
 * thread never block. */
void mora_read_process_info(HWND hwnd, mora_window_snapshot_t* snapshot);
