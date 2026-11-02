#pragma once

#include "bar/bar.h"
#include "common/dpi.h"
#include "common/geometry.h"
#include "common/limits.h"
#include "media/media.h"

#define MORA_BAR_CLASS_NAME L"MORA_BAR_CLASS"
#define MORA_BAR_TIMER_CLOCK 200
#define MORA_BAR_WM_MEDIA (WM_APP + 1)
#define MORA_BAR_MAX_CHIPS 32u
#define MORA_BAR_MAX_ICONS 3u
#define MORA_BAR_MORE_CAPACITY 8
#define MORA_CHIP_TITLE_CAPACITY 128
#define MORA_ICON_ORDER_CAPACITY 64
#define MORA_MEDIA_BUTTON_COUNT MORA_MEDIA_COMMAND_COUNT

typedef struct {
    COLORREF background;
    COLORREF divider;
    COLORREF text;
    COLORREF subtext;
    COLORREF dim;
    COLORREF faint;
    COLORREF hover;
    COLORREF title;
    COLORREF key;
    COLORREF key_edge;
    COLORREF frame;
    COLORREF mark_ring;
    COLORREF mark_dot;
} mora_bar_theme_t;

extern const mora_bar_theme_t THEME;

typedef struct {
    HWND hwnd;
    mora_index_t monitor;
    UINT dpi;
    mora_bar_metrics_t metrics;
    HFONT label_font;
    HFONT title_font;
    HFONT symbol_font;
    mora_index_t hot_workspace;
    UINT slot_icons[MORA_WORKSPACE_COUNT];
    BOOL tracking_mouse;

    /* Set while the window procedure paints; a nested WM_PAINT is refused. */
    BOOL painting;

    struct {
        HWND hwnd;
        RECT rect;
    } chips[MORA_BAR_MAX_CHIPS];
    UINT chip_count;
    HWND hot_chip;
    RECT media_buttons[MORA_MEDIA_BUTTON_COUNT];
    int hot_button;
} mora_bar_t;

extern mora_bar_t g_bars[MORA_WORKSPACE_COUNT];
extern size_t g_bar_count;
extern mora_bar_message_handler_t g_bar_handler;
extern mora_media_t g_bar_media;

LRESULT CALLBACK mora_bar_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void mora_bar_fonts_init(void);
HFONT mora_create_font(int height, int weight);
HFONT mora_create_symbol_font(int height);

void mora_fill_rect(HDC dc, RECT rect, COLORREF color);
void mora_fill_round_rect(HDC dc, RECT rect, int radius, COLORREF color);
void mora_draw_divider(const mora_bar_t* bar, HDC dc, int x);
void mora_draw_text(HDC dc, const wchar_t* text, RECT rect, COLORREF color, UINT format);
int mora_text_width(HDC dc, const wchar_t* text);

void mora_draw_mark(HDC dc, int left, int top, int size);

typedef struct {
    HWND hwnd;
    int size;
    HBITMAP image;
    wchar_t program[MAX_PATH];
} mora_icon_entry_t;

void mora_icons_clear(void);
void mora_icons_prune(void);
const mora_icon_entry_t* mora_icon_for_window(const tracked_window_t* window, int size);
UINT mora_bar_workspace_icons(const mora_workspace_state_t* state, mora_index_t workspace, int size,
    const mora_icon_entry_t** icons, UINT capacity);
#define MORA_ICON_DIM 150
void mora_draw_icon(HDC dc, const mora_icon_entry_t* entry, int left, int top, BYTE opacity);

int mora_paint_workspaces(mora_bar_t* bar, HDC dc, const mora_workspace_state_t* state);

void mora_paint_chips(mora_bar_t* bar, HDC dc, int left, int center, int right);
HWND mora_bar_chip_hit(const mora_bar_t* bar, int x, int y);

void mora_pick_overlays_sync(HINSTANCE instance);
void mora_pick_overlays_destroy(void);

int mora_paint_media(mora_bar_t* bar, HDC dc, int right);
int mora_bar_media_button_hit(const mora_bar_t* bar, int x, int y);

int mora_paint_clock(const mora_bar_t* bar, HDC dc, int right);
void mora_schedule_clock(void);

void mora_paint_background(const mora_bar_t* bar, HDC dc, RECT client);
void mora_bar_on_paint(mora_bar_t* bar);
