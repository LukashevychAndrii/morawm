#include "internal.h"

#include <shlobj.h>
#include <wchar.h>

#include "detection/window_detection.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

#define MORA_ICON_CACHE_CAPACITY 256
/* Icons are rendered this many times larger, then averaged down: GDI's own
 * icon scaling is nearest-neighbour, which is what made them look pixelated. */
#define MORA_ICON_OVERSAMPLE 4
#define MORA_ICON_QUERY_TIMEOUT_MS 50
static mora_icon_entry_t g_icons[MORA_ICON_CACHE_CAPACITY];
static size_t g_icon_count;

static void mora_icon_release(mora_icon_entry_t* entry)
{
    if (entry->image) {
        DeleteObject(entry->image);
    }
}

void mora_icons_clear(void)
{
    for (size_t i = 0; i < g_icon_count; ++i) {
        mora_icon_release(&g_icons[i]);
    }

    g_icon_count = 0;
}

void mora_icons_prune(void)
{
    size_t kept = 0;

    for (size_t i = 0; i < g_icon_count; ++i) {
        if (IsWindow(g_icons[i].hwnd)) {
            g_icons[kept++] = g_icons[i];
        }
        else {
            mora_icon_release(&g_icons[i]);
        }
    }

    g_icon_count = kept;
}

static HICON mora_window_icon(HWND hwnd)
{
    static const WPARAM KINDS[] = { ICON_BIG, ICON_SMALL2, ICON_SMALL };

    for (size_t i = 0; i < ARRAYSIZE(KINDS); ++i) {
        DWORD_PTR icon = 0;

        /* A hung program must not stall the bar. */
        if (SendMessageTimeoutW(hwnd, WM_GETICON, KINDS[i], 0, SMTO_ABORTIFHUNG, MORA_ICON_QUERY_TIMEOUT_MS, &icon) && icon) {
            return (HICON)icon;
        }
    }

    HICON icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);

    return icon ? icon : (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
}

static HBITMAP mora_render_icon(HICON icon, int size)
{
    int large = size * MORA_ICON_OVERSAMPLE;
    BITMAPINFO info = { .bmiHeader = { .biSize = sizeof(BITMAPINFOHEADER), .biWidth = large,
        .biHeight = -large, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB } };
    DWORD* black = NULL;
    DWORD* white = NULL;
    DWORD* pixels = NULL;
    HBITMAP on_black = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void**)&black, NULL, 0);
    HBITMAP on_white = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void**)&white, NULL, 0);

    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    HBITMAP image = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    HDC memory = CreateCompatibleDC(NULL);
    BOOL ok = on_black && on_white && image && memory;

    if (ok) {
        memset(black, 0x00, (size_t)large * large * 4);
        memset(white, 0xFF, (size_t)large * large * 4);
        HGDIOBJ old = SelectObject(memory, on_black);

        DrawIconEx(memory, 0, 0, icon, large, large, 0, NULL, DI_NORMAL);
        SelectObject(memory, on_white);
        DrawIconEx(memory, 0, 0, icon, large, large, 0, NULL, DI_NORMAL);
        SelectObject(memory, old);
        GdiFlush();

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                /* On black the colour is already premultiplied by coverage. */
                UINT sum[4] = { 0 };

                for (int sy = 0; sy < MORA_ICON_OVERSAMPLE; ++sy) {
                    for (int sx = 0; sx < MORA_ICON_OVERSAMPLE; ++sx) {
                        size_t at = (size_t)(y * MORA_ICON_OVERSAMPLE + sy) * large + x * MORA_ICON_OVERSAMPLE + sx;
                        int alpha = 255 - ((int)(white[at] >> 8 & 0xFF) - (int)(black[at] >> 8 & 0xFF));

                        sum[0] += black[at] & 0xFF;
                        sum[1] += black[at] >> 8 & 0xFF;
                        sum[2] += black[at] >> 16 & 0xFF;
                        sum[3] += alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                    }
                }

                enum { N = MORA_ICON_OVERSAMPLE * MORA_ICON_OVERSAMPLE };
                pixels[y * size + x] = sum[3] / N << 24 | sum[2] / N << 16 | sum[1] / N << 8 | sum[0] / N;
            }
        }
    }

    if (memory) {
        DeleteDC(memory);
    }

    if (on_white) {
        DeleteObject(on_white);
    }

    if (on_black) {
        DeleteObject(on_black);
    }

    if (!ok && image) {
        DeleteObject(image);
        image = NULL;
    }

    return image;
}

const mora_icon_entry_t* mora_icon_for_window(const tracked_window_t* window, int size)
{
    for (size_t i = 0; i < g_icon_count; ++i) {
        if (g_icons[i].hwnd == window->hwnd && g_icons[i].size == size) {
            return &g_icons[i];
        }
    }

    if (g_icon_count == MORA_ICON_CACHE_CAPACITY) {
        return NULL;
    }

    mora_icon_entry_t entry = { .hwnd = window->hwnd, .size = size };

    mora_process_image_path(window->pid, entry.program, _countof(entry.program));

    /* Store apps all run in ApplicationFrameHost, whose file icon is generic:
     * ask their window instead, and never group them together. */
    BOOL hosted = entry.program[0] && mora_is_app_frame_host_path(entry.program);
    HICON extracted = NULL;

    if (entry.program[0] && !hosted) {
        UINT large = (UINT)(size * MORA_ICON_OVERSAMPLE);

        if (SHDefExtractIconW(entry.program, 0, 0, &extracted, NULL, large) != S_OK) {
            extracted = NULL;
        }
    }

    HICON icon = extracted;

    if (!icon) {
        icon = mora_window_icon(window->hwnd);
    }

    if (!icon) {
        icon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }

    entry.image = icon ? mora_render_icon(icon, size) : NULL;

    if (extracted) {
        DestroyIcon(extracted);
    }

    if (hosted || !entry.program[0]) {
        swprintf(entry.program, _countof(entry.program), L"window:%p", (void*)window->hwnd);
    }

    g_icons[g_icon_count] = entry;

    return &g_icons[g_icon_count++];
}

/* Visible windows left to right, then top to bottom; minimized ones last. */
typedef struct {
    const tracked_window_t* window;
    BOOL minimized;
    POINT origin;
} mora_icon_order_t;

static BOOL mora_icon_order_before(const mora_icon_order_t* a, const mora_icon_order_t* b)
{
    if (a->minimized != b->minimized) {
        return b->minimized;
    }

    if (a->origin.x != b->origin.x) {
        return a->origin.x < b->origin.x;
    }

    return a->origin.y < b->origin.y;
}

UINT mora_bar_workspace_icons(const mora_workspace_state_t* state, mora_index_t workspace, int size,
    const mora_icon_entry_t** icons, UINT capacity)
{
    mora_icon_order_t order[MORA_ICON_ORDER_CAPACITY];
    UINT windows = 0;

    for (size_t i = 0; i < state->windows.count && windows < _countof(order); ++i) {
        const tracked_window_t* window = &state->windows.entries[i];

        if (window->workspace_index != workspace) {
            continue;
        }

        mora_icon_order_t item = { window, IsIconic(window->hwnd), mora_manager_window_origin(window->hwnd) };
        UINT at = windows++;

        for (; at > 0 && mora_icon_order_before(&item, &order[at - 1]); --at) {
            order[at] = order[at - 1];
        }

        order[at] = item;
    }

    UINT count = 0, total = 0;
    const wchar_t* seen[MORA_ICON_ORDER_CAPACITY];
    UINT seen_count = 0;

    for (UINT w = 0; w < windows; ++w) {
        const mora_icon_entry_t* entry = mora_icon_for_window(order[w].window, size);

        if (!entry || !entry->image) {
            continue;
        }

        BOOL duplicate = FALSE;

        for (UINT k = 0; k < seen_count && !duplicate; ++k) {
            duplicate = wcscmp(seen[k], entry->program) == 0;
        }

        if (duplicate) {
            continue;
        }

        if (seen_count < _countof(seen)) {
            seen[seen_count++] = entry->program;
        }

        if (count < capacity) {
            icons[count++] = entry;
        }

        ++total;
    }

    return total;
}

void mora_draw_icon(HDC dc, const mora_icon_entry_t* entry, int left, int top, BYTE opacity)
{
    HDC memory = CreateCompatibleDC(dc);

    if (!memory) {
        return;
    }

    HGDIOBJ old = SelectObject(memory, entry->image);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA };

    AlphaBlend(dc, left, top, entry->size, entry->size, memory, 0, 0, entry->size, entry->size, blend);
    SelectObject(memory, old);
    DeleteDC(memory);
}
