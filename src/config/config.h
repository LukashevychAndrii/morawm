#pragma once

#include <Windows.h>

/* Optional startup configuration: %LOCALAPPDATA%\MoraWM\morawm.conf can show
 * workspaces on monitors and launch programs whose windows open in a chosen
 * workspace or monitor. It is read once during startup; without the file
 * nothing changes. Call only from the thread running MoraWM's message loop. */

void mora_config_load(void);
void mora_config_apply_workspaces(void);
void mora_config_launch(void);
void mora_config_dispose(void);
