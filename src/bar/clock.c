#include "internal.h"

#define MORA_CLOCK_TEXT_CAPACITY 32
#define MORA_SECONDS_PER_MINUTE 60
#define MORA_MS_PER_SECOND 1000
#define MORA_CLOCK_REFRESH_SLACK_MS 20

int mora_paint_clock(const mora_bar_t* bar, HDC dc, int right)
{
    SYSTEMTIME now;

    GetLocalTime(&now);
    wchar_t time[MORA_CLOCK_TEXT_CAPACITY] = L"";

    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, NULL, time, _countof(time));
    RECT rect = { right - mora_text_width(dc, time), 0, right, bar->metrics.height };

    mora_draw_text(dc, time, rect, THEME.subtext, DT_RIGHT | DT_NOCLIP);

    return rect.left;
}

void mora_schedule_clock(void)
{
    if (g_bar_count == 0) {
        return;
    }

    SYSTEMTIME now;

    GetLocalTime(&now);
    UINT until_next_minute = (MORA_SECONDS_PER_MINUTE - now.wSecond) * MORA_MS_PER_SECOND -
        now.wMilliseconds + MORA_CLOCK_REFRESH_SLACK_MS;

    SetTimer(g_bars[0].hwnd, MORA_BAR_TIMER_CLOCK, until_next_minute, NULL);
}
