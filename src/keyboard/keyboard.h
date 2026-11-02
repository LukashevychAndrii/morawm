#pragma once
#include <Windows.h>
#include <stddef.h>

typedef enum {
    MORA_KEY_WORKSPACE, MORA_KEY_MOVE_WORKSPACE,
    MORA_KEY_FOCUS, MORA_KEY_MONITOR,
    MORA_KEY_LAYOUT, MORA_KEY_QUIT,
    MORA_KEY_RETILE, MORA_KEY_MANUAL, MORA_KEY_SWAP_MONITOR,
    MORA_KEY_PICK, MORA_KEY_FLOAT, MORA_KEY_CLOSE
} mora_key_command_t;

typedef struct {
    UINT modifiers;
    UINT key;
    mora_key_command_t command;
    int argument;
} mora_key_binding_t;

const mora_key_binding_t* mora_keyboard_binding(size_t id);
BOOL mora_keyboard_register(HWND receiver, size_t* failed_id, DWORD* error);
void mora_keyboard_unregister(HWND receiver);

#define MORA_PICK_ESCAPE L'\x1b'
#define MORA_PICK_ID_BASE 0x1000
#define MORA_LETTER_COUNT 26

/* While a window is held, claim each of `keys` ('A'-'Z', '1'-'9') with and
 * without Alt, and Escape. A key another program or a binding already owns
 * is skipped: Alt+H, for example, arrives as its binding instead. */
void mora_keyboard_register_pick(HWND receiver, const wchar_t* keys);
void mora_keyboard_unregister_pick(HWND receiver);
wchar_t mora_keyboard_pick_key(size_t id);
wchar_t mora_keyboard_binding_pick_key(const mora_key_binding_t* binding);
void mora_keyboard_assign_keys(const wchar_t* const* names, size_t count, wchar_t* keys);
