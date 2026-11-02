#include "app.h"

#include <stdio.h>

#include "keyboard/keyboard.h"

#pragma comment(lib, "user32.lib")

#define MORA_KEY_NAME_CAPACITY 64
#define MORA_USER_MESSAGE_CAPACITY 256

static BOOL g_hotkeys_registered = FALSE;

BOOL mora_register_hotkeys(void)
{
    size_t failed_id = 0;
    DWORD error = 0;

    if (mora_keyboard_register(mora_main_hwnd(), &failed_id, &error)) {
        g_hotkeys_registered = TRUE;

        return TRUE;
    }

    const mora_key_binding_t* binding = mora_keyboard_binding(failed_id);
    wchar_t key_name[MORA_KEY_NAME_CAPACITY] = L"unknown key";

    if (binding) {
        UINT scan_code = MapVirtualKeyW(binding->key, MAPVK_VK_TO_VSC);

        (void)GetKeyNameTextW((LONG)(scan_code << 16), key_name, _countof(key_name));
    }

    BOOL shift = binding && (binding->modifiers & MOD_SHIFT);
    wchar_t message[MORA_USER_MESSAGE_CAPACITY];

    swprintf_s(message, _countof(message),
        L"Could not register Alt%s + %s (Windows error %lu). "
        L"Another application may own this shortcut. MoraWM will exit.",
        shift ? L"+Shift" : L"", key_name, error);
    mora_show_error(L"Keyboard shortcut unavailable", message);

    return FALSE;
}

void mora_unregister_hotkeys(void)
{
    if (g_hotkeys_registered && mora_main_hwnd()) {
        mora_keyboard_unregister(mora_main_hwnd());
    }

    g_hotkeys_registered = FALSE;
}
