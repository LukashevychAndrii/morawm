#include "internal.h"

#include <wchar.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

static const wchar_t* g_font_face = L"Segoe UI";

static int CALLBACK mora_font_exists_proc(const LOGFONTW* font, const TEXTMETRICW* metrics, DWORD type, LPARAM found)
{
    (void)font; (void)metrics; (void)type;
    *(BOOL*)found = TRUE;

    return 0;
}

static BOOL mora_font_exists(const wchar_t* face)
{
    LOGFONTW query = { .lfCharSet = DEFAULT_CHARSET };

    wcscpy_s(query.lfFaceName, _countof(query.lfFaceName), face);
    BOOL found = FALSE;
    HDC screen = GetDC(NULL);

    EnumFontFamiliesExW(screen, &query, mora_font_exists_proc, (LPARAM)&found, 0);
    ReleaseDC(NULL, screen);

    return found;
}

/* Windows 11 ships Segoe UI Variable; older systems fall back to Segoe UI. */
static const wchar_t* mora_pick_font_face(void)
{
    return mora_font_exists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
}

/* The system symbol font: Segoe Fluent Icons on Windows 11, else Segoe MDL2
 * Assets. Both put the media symbols at the same code points. */
static const wchar_t* mora_pick_symbol_face(void)
{
    return mora_font_exists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
}

static HFONT mora_create_font_face(const wchar_t* face, int height, int weight)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}

void mora_bar_fonts_init(void)
{
    g_font_face = mora_pick_font_face();
}

HFONT mora_create_font(int height, int weight)
{
    return mora_create_font_face(g_font_face, height, weight);
}

HFONT mora_create_symbol_font(int height)
{
    return mora_create_font_face(mora_pick_symbol_face(), height, FW_NORMAL);
}
