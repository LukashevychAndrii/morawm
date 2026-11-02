#include "keyboard.h"

#define KEY(extra, key, command, argument) { MOD_ALT | MOD_NOREPEAT | (extra), key, command, argument }
#define WORKSPACE(key, index) \
    KEY(0, key, MORA_KEY_WORKSPACE, index), \
    KEY(MOD_SHIFT, key, MORA_KEY_MOVE_WORKSPACE, index)

static const mora_key_binding_t bindings[] = {
    WORKSPACE('1', 0), WORKSPACE('2', 1), WORKSPACE('3', 2),
    WORKSPACE('4', 3), WORKSPACE('5', 4), WORKSPACE('6', 5),
    WORKSPACE('7', 6), WORKSPACE('8', 7), WORKSPACE('9', 8),
    KEY(0, 'H', MORA_KEY_FOCUS, -1),
    KEY(0, 'L', MORA_KEY_FOCUS, 1),
    KEY(0, 'M', MORA_KEY_PICK, 0),
    /* The key left of 1: one key to flip between two monitors. */
    KEY(0, VK_OEM_3, MORA_KEY_MONITOR, 1),
    KEY(0, 'S', MORA_KEY_SWAP_MONITOR, 1),
    KEY(0, 'T', MORA_KEY_LAYOUT, 0),
    KEY(MOD_SHIFT, 'T', MORA_KEY_MANUAL, 0),
    KEY(0, 'R', MORA_KEY_RETILE, 0),
    KEY(MOD_SHIFT, VK_SPACE, MORA_KEY_FLOAT, 0),
    KEY(0, 'Q', MORA_KEY_CLOSE, 0),
    KEY(MOD_SHIFT, 'Q', MORA_KEY_QUIT, 0)
};

const mora_key_binding_t* mora_keyboard_binding(size_t id)
{
    return id > 0 && id <= _countof(bindings) ? &bindings[id - 1] : NULL;
}

BOOL mora_keyboard_register(HWND receiver, size_t* failed_id, DWORD* error)
{
    for (size_t i = 0; i < _countof(bindings); ++i) {
        if (!RegisterHotKey(receiver, (int)i + 1, bindings[i].modifiers, bindings[i].key)) {
            if (failed_id) {
                *failed_id = i + 1;
            }

            if (error) {
                *error = GetLastError();
            }

            /* Roll back only bindings registered by this attempt. */
            for (size_t id = i; id > 0; --id) {
                UnregisterHotKey(receiver, (int)id);
            }

            return FALSE;
        }
    }

    return TRUE;
}

void mora_keyboard_unregister(HWND receiver)
{
    for (size_t i = 0; i < _countof(bindings); ++i) {
        UnregisterHotKey(receiver, (int)i + 1);
    }
}

/* Pick key k registers as MORA_PICK_ID_BASE + k, and with Alt 0x80 above.
 * Keys are ASCII, so the two ranges never meet. */
#define MORA_PICK_ALT 0x80

static BOOL mora_is_pick_key(wchar_t key)
{
    return (key >= L'A' && key <= L'Z') || (key >= L'1' && key <= L'9') || key == MORA_PICK_ESCAPE;
}

void mora_keyboard_register_pick(HWND receiver, const wchar_t* keys)
{
    (void)RegisterHotKey(receiver, MORA_PICK_ID_BASE + MORA_PICK_ESCAPE, MOD_NOREPEAT, VK_ESCAPE);

    for (const wchar_t* key = keys; key && *key; ++key) {
        if (!mora_is_pick_key(*key) || *key == MORA_PICK_ESCAPE) {
            continue;
        }

        (void)RegisterHotKey(receiver, MORA_PICK_ID_BASE + *key, MOD_NOREPEAT, *key);
        (void)RegisterHotKey(receiver, MORA_PICK_ID_BASE + MORA_PICK_ALT + *key, MOD_ALT | MOD_NOREPEAT, *key);
    }
}

void mora_keyboard_unregister_pick(HWND receiver)
{
    for (int id = 0; id < 2 * MORA_PICK_ALT; ++id) {
        if (mora_is_pick_key((wchar_t)(id % MORA_PICK_ALT))) {
            UnregisterHotKey(receiver, MORA_PICK_ID_BASE + id);
        }
    }
}

wchar_t mora_keyboard_pick_key(size_t id)
{
    if (id < MORA_PICK_ID_BASE || id >= MORA_PICK_ID_BASE + 2 * MORA_PICK_ALT) {
        return 0;
    }

    wchar_t key = (wchar_t)((id - MORA_PICK_ID_BASE) % MORA_PICK_ALT);

    return mora_is_pick_key(key) ? key : 0;
}

wchar_t mora_keyboard_binding_pick_key(const mora_key_binding_t* binding)
{
    if (!binding || (binding->modifiers & ~MOD_NOREPEAT) != MOD_ALT) {
        return 0;
    }

    wchar_t key = (wchar_t)binding->key;

    return mora_is_pick_key(key) && key != MORA_PICK_ESCAPE ? key : 0;
}

static wchar_t mora_upper_letter(wchar_t c)
{
    if (c >= L'a' && c <= L'z') {
        return (wchar_t)(c - L'a' + L'A');
    }

    return c >= L'A' && c <= L'Z' ? c : 0;
}

void mora_keyboard_assign_keys(const wchar_t* const* names, size_t count, wchar_t* keys)
{
    BOOL taken[MORA_LETTER_COUNT] = { 0 };

    for (size_t i = 0; i < count; ++i) {
        keys[i] = 0;

        for (const wchar_t* c = names[i]; c && *c && !keys[i]; ++c) {
            wchar_t letter = mora_upper_letter(*c);

            if (letter && !taken[letter - L'A']) {
                keys[i] = letter;
            }
        }

        for (int letter = 0; letter < MORA_LETTER_COUNT && !keys[i]; ++letter) {
            if (!taken[letter]) {
                keys[i] = (wchar_t)(L'A' + letter);
            }
        }

        if (keys[i]) {
            taken[keys[i] - L'A'] = TRUE;
        }
    }
}
