#include "app.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

#define MORA_REGISTRY_KEY L"Software\\MoraWM"
#define MORA_SAVED_MINIMIZE_ANIMATION L"SavedMinimizeAnimation"
#define MORA_SAVED_CLIENT_AREA_ANIMATION L"SavedClientAreaAnimation"

typedef struct {
    int minimize;
    BOOL client_area;
} mora_animations_t;

static BOOL g_animations_saved = FALSE;
static mora_animations_t g_animations;

static BOOL mora_read_registry_dword(const wchar_t* name, DWORD* value)
{
    DWORD size = sizeof(*value);

    return RegGetValueW(HKEY_CURRENT_USER, MORA_REGISTRY_KEY, name, RRF_RT_REG_DWORD,
        NULL, value, &size) == ERROR_SUCCESS;
}

static BOOL mora_load_saved_animations(mora_animations_t* animations)
{
    DWORD minimize, client_area;

    if (!mora_read_registry_dword(MORA_SAVED_MINIMIZE_ANIMATION, &minimize) ||
        !mora_read_registry_dword(MORA_SAVED_CLIENT_AREA_ANIMATION, &client_area)) {
        return FALSE;
    }

    *animations = (mora_animations_t){ (int)minimize, client_area != 0 };

    return TRUE;
}

static BOOL mora_store_saved_animations(const mora_animations_t* animations)
{
    DWORD minimize = (DWORD)animations->minimize, client_area = (DWORD)animations->client_area;

    return RegSetKeyValueW(HKEY_CURRENT_USER, MORA_REGISTRY_KEY, MORA_SAVED_MINIMIZE_ANIMATION,
               REG_DWORD, &minimize, sizeof(minimize)) == ERROR_SUCCESS &&
        RegSetKeyValueW(HKEY_CURRENT_USER, MORA_REGISTRY_KEY, MORA_SAVED_CLIENT_AREA_ANIMATION,
            REG_DWORD, &client_area, sizeof(client_area)) == ERROR_SUCCESS;
}

static void mora_clear_saved_animations(void)
{
    (void)RegDeleteKeyValueW(HKEY_CURRENT_USER, MORA_REGISTRY_KEY, MORA_SAVED_MINIMIZE_ANIMATION);
    (void)RegDeleteKeyValueW(HKEY_CURRENT_USER, MORA_REGISTRY_KEY, MORA_SAVED_CLIENT_AREA_ANIMATION);
}

static BOOL mora_get_animations(mora_animations_t* animations)
{
    ANIMATIONINFO minimize = { .cbSize = sizeof(minimize) };
    BOOL client_area = FALSE;

    if (!SystemParametersInfoW(SPI_GETANIMATION, sizeof(minimize), &minimize, 0) ||
        !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &client_area, 0)) {
        return FALSE;
    }

    *animations = (mora_animations_t){ minimize.iMinAnimate, client_area };

    return TRUE;
}

static void mora_set_animations(const mora_animations_t* animations)
{
    ANIMATIONINFO minimize = { .cbSize = sizeof(minimize), .iMinAnimate = animations->minimize };

    (void)SystemParametersInfoW(SPI_SETANIMATION, sizeof(minimize), &minimize, SPIF_SENDCHANGE);
    (void)SystemParametersInfoW(SPI_SETCLIENTAREAANIMATION, 0,
        (PVOID)(INT_PTR)animations->client_area, SPIF_SENDCHANGE);
}

void mora_disable_animations(void)
{
    if (!mora_load_saved_animations(&g_animations)) {
        if (!mora_get_animations(&g_animations) || !mora_store_saved_animations(&g_animations)) {
            return;
        }
    }

    g_animations_saved = TRUE;
    mora_set_animations(&(mora_animations_t){ 0, FALSE });
}

void mora_restore_animations(void)
{
    if (!g_animations_saved) {
        return;
    }

    g_animations_saved = FALSE;
    mora_set_animations(&g_animations);
    mora_clear_saved_animations();
}
