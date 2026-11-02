#include "internal.h"

#include <math.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")

#define MORA_MARK_POINTS 48
static const struct { float x, y; } MARK_RINGS[][MORA_MARK_POINTS] = {
    {
        { 148.9f, 36.8f }, { 136.2f, 40.1f }, { 123.9f, 44.4f }, { 111.8f, 49.4f }, { 99.6f, 54.3f }, { 87.5f, 59.2f }, { 75.2f, 63.7f }, { 62.8f, 67.9f },
        { 50.3f, 72.0f }, { 38.1f, 76.7f }, { 26.6f, 83.0f }, { 17.0f, 91.8f }, { 12.0f, 103.8f }, { 12.8f, 116.8f }, { 17.1f, 129.1f }, { 23.0f, 140.8f },
        { 29.3f, 152.3f }, { 35.7f, 163.7f }, { 41.9f, 175.2f }, { 48.1f, 186.8f }, { 54.6f, 198.2f }, { 62.0f, 209.0f }, { 71.2f, 218.2f }, { 82.6f, 224.5f },
        { 95.4f, 227.0f }, { 108.5f, 226.0f }, { 121.2f, 222.9f }, { 133.6f, 218.6f }, { 145.7f, 213.6f }, { 157.7f, 208.3f }, { 169.7f, 203.1f }, { 181.6f, 197.7f },
        { 193.5f, 192.1f }, { 204.9f, 185.7f }, { 214.8f, 177.2f }, { 221.1f, 165.8f }, { 222.6f, 152.9f }, { 220.9f, 139.9f }, { 217.9f, 127.1f }, { 214.7f, 114.4f },
        { 211.7f, 101.7f }, { 208.9f, 88.9f }, { 205.8f, 76.2f }, { 201.9f, 63.7f }, { 195.8f, 52.1f }, { 186.6f, 42.9f }, { 174.9f, 37.2f }, { 161.9f, 35.5f },
    },
    {
        { 128.0f, 76.7f }, { 120.4f, 78.5f }, { 113.1f, 80.8f }, { 105.8f, 83.5f }, { 98.6f, 86.5f }, { 91.6f, 89.8f }, { 84.7f, 93.4f }, { 78.0f, 97.4f },
        { 71.7f, 101.8f }, { 66.0f, 107.0f }, { 61.3f, 113.2f }, { 58.4f, 120.4f }, { 57.6f, 128.1f }, { 58.4f, 135.8f }, { 60.5f, 143.2f }, { 63.5f, 150.4f },
        { 67.1f, 157.2f }, { 71.2f, 163.8f }, { 75.9f, 170.0f }, { 81.1f, 175.8f }, { 86.9f, 181.0f }, { 93.4f, 185.1f }, { 100.5f, 188.1f }, { 108.1f, 189.6f },
        { 115.9f, 189.8f }, { 123.6f, 188.8f }, { 131.1f, 187.0f }, { 138.4f, 184.5f }, { 145.6f, 181.4f }, { 152.5f, 177.8f }, { 159.1f, 173.8f }, { 165.4f, 169.3f },
        { 171.1f, 164.0f }, { 176.0f, 158.1f }, { 179.7f, 151.2f }, { 181.8f, 143.8f }, { 182.5f, 136.1f }, { 181.9f, 128.3f }, { 180.5f, 120.7f }, { 178.4f, 113.2f },
        { 175.7f, 106.0f }, { 172.5f, 98.9f }, { 168.6f, 92.2f }, { 163.8f, 86.1f }, { 157.9f, 81.1f }, { 151.0f, 77.7f }, { 143.4f, 75.9f }, { 135.7f, 75.7f },
    },
    {
        { 118.7f, 102.0f }, { 114.3f, 103.2f }, { 110.1f, 104.6f }, { 106.0f, 106.4f }, { 101.9f, 108.5f }, { 98.2f, 110.9f }, { 94.6f, 113.7f }, { 91.4f, 116.9f },
        { 88.6f, 120.4f }, { 86.4f, 124.3f }, { 85.0f, 128.6f }, { 84.4f, 133.0f }, { 84.7f, 137.5f }, { 85.6f, 141.9f }, { 87.3f, 146.1f }, { 89.4f, 150.1f },
        { 92.0f, 153.8f }, { 95.0f, 157.1f }, { 98.4f, 160.1f }, { 102.1f, 162.6f }, { 106.2f, 164.5f }, { 110.5f, 165.9f }, { 114.9f, 166.6f }, { 119.4f, 166.7f },
        { 123.9f, 166.3f }, { 128.3f, 165.4f }, { 132.6f, 164.1f }, { 136.7f, 162.2f }, { 140.7f, 160.0f }, { 144.3f, 157.5f }, { 147.7f, 154.4f }, { 150.7f, 151.1f },
        { 153.2f, 147.3f }, { 155.1f, 143.2f }, { 156.3f, 138.9f }, { 156.9f, 134.5f }, { 156.8f, 130.0f }, { 156.1f, 125.5f }, { 154.8f, 121.2f }, { 152.9f, 117.1f },
        { 150.6f, 113.2f }, { 147.8f, 109.7f }, { 144.5f, 106.7f }, { 140.7f, 104.3f }, { 136.5f, 102.5f }, { 132.2f, 101.5f }, { 127.7f, 101.1f }, { 123.2f, 101.3f },
    },
};
static const float MARK_OPACITY[] = { 0.7f, 0.85f, 1.0f };
#define MORA_MARK_CENTER_X 121.0f
#define MORA_MARK_CENTER_Y 135.0f
#define MORA_MARK_DOT_RADIUS 13.0f
#define MORA_MARK_STROKE 11.0f

typedef struct {
    float ax, ay;
    float dx, dy;
    float length;
} mora_segment_t;

/* Squared distance, so the caller can compare 48 candidates without a square
 * root each. sqrtf is monotone, so the nearest segment and the final distance
 * are the same as taking the root of every candidate. */
static float mora_segment_distance_squared(float x, float y, const mora_segment_t* segment)
{
    float t = segment->length > 0.0f
        ? ((x - segment->ax) * segment->dx + (y - segment->ay) * segment->dy) / segment->length
        : 0.0f;

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float ex = x - (segment->ax + t * segment->dx), ey = y - (segment->ay + t * segment->dy);

    return ex * ex + ey * ey;
}

static float mora_coverage(float value)
{
    return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

static void mora_blend(float* rgba, COLORREF color, float alpha)
{
    rgba[0] = GetRValue(color) * alpha + rgba[0] * (1.0f - alpha);
    rgba[1] = GetGValue(color) * alpha + rgba[1] * (1.0f - alpha);
    rgba[2] = GetBValue(color) * alpha + rgba[2] * (1.0f - alpha);
    rgba[3] = alpha + rgba[3] * (1.0f - alpha);
}

/* GDI has no anti-aliased shapes, so coverage comes from each pixel's
 * distance to the shapes and the result is alpha-blended. */
void mora_draw_mark(HDC dc, int left, int top, int size)
{
    if (size <= 0) {
        return;
    }

    BITMAPINFO info = { .bmiHeader = { .biSize = sizeof(BITMAPINFOHEADER), .biWidth = size,
        .biHeight = -size, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB } };
    DWORD* pixels = NULL;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);

    if (!bitmap) {
        return;
    }

    /* Icon units to pixels. The dot sits at the centre and the square fits
     * the outer ring plus its stroke, so nothing is clipped. */
    static float radius;

    if (radius == 0.0f) {
        for (size_t r = 0; r < ARRAYSIZE(MARK_RINGS); ++r) {
            for (int i = 0; i < MORA_MARK_POINTS; ++i) {
                float x = MARK_RINGS[r][i].x - MORA_MARK_CENTER_X, y = MARK_RINGS[r][i].y - MORA_MARK_CENTER_Y;

                radius = max(radius, sqrtf(x * x + y * y));
            }
        }
    }

    float reach = radius + MORA_MARK_STROKE / 2.0f;
    float scale = size / (2.0f * reach);
    float half = size / 2.0f;
    float stroke = max(MORA_MARK_STROKE * scale, 1.5f) / 2.0f;

    float rings[ARRAYSIZE(MARK_RINGS)][MORA_MARK_POINTS][2];

    for (size_t r = 0; r < ARRAYSIZE(MARK_RINGS); ++r) {
        for (int i = 0; i < MORA_MARK_POINTS; ++i) {
            float x = MARK_RINGS[r][i].x - MORA_MARK_CENTER_X, y = MARK_RINGS[r][i].y - MORA_MARK_CENTER_Y;

            rings[r][i][0] = half + x * scale;
            rings[r][i][1] = half + y * scale;
        }
    }

    float dot_radius = max(MORA_MARK_DOT_RADIUS * scale, 2.0f);

    /* Each ring's distance band from the icon centre. A pixel outside a ring's
     * band cannot be within `stroke` of it, so the pixel loop skips that ring
     * instead of testing all 48 segments. The band is widened past the visible
     * threshold, so skipping never changes a pixel. */
    mora_segment_t segments[ARRAYSIZE(MARK_RINGS)][MORA_MARK_POINTS];
    float ring_min[ARRAYSIZE(MARK_RINGS)], ring_max[ARRAYSIZE(MARK_RINGS)];
    float band = stroke + 1.0f;

    for (size_t r = 0; r < ARRAYSIZE(MARK_RINGS); ++r) {
        float inner2 = 1e18f, outer2 = 0.0f;

        for (int i = 0; i < MORA_MARK_POINTS; ++i) {
            const float* a = rings[r][i];
            const float* b = rings[r][(i + 1) % MORA_MARK_POINTS];
            mora_segment_t* segment = &segments[r][i];

            *segment = (mora_segment_t){ a[0], a[1], b[0] - a[0], b[1] - a[1] };
            segment->length = segment->dx * segment->dx + segment->dy * segment->dy;

            float ax = a[0] - half, ay = a[1] - half, bx = b[0] - half, by = b[1] - half;

            outer2 = max(outer2, max(ax * ax + ay * ay, bx * bx + by * by));
            inner2 = min(inner2, mora_segment_distance_squared(half, half, segment));
        }

        float inner = sqrtf(inner2) - band, outer = sqrtf(outer2) + band;

        ring_min[r] = inner > 0.0f ? inner * inner : 0.0f;
        ring_max[r] = outer * outer;
    }

    for (int y = 0; y < size; ++y) {
        float py = y + 0.5f;

        for (int x = 0; x < size; ++x) {
            float px = x + 0.5f;
            float rgba[4] = { 0 };
            float center_x = px - half, center_y = py - half;
            float center2 = center_x * center_x + center_y * center_y;

            for (size_t r = 0; r < ARRAYSIZE(MARK_RINGS); ++r) {
                if (center2 < ring_min[r] || center2 > ring_max[r]) {
                    continue;
                }

                float nearest2 = 1e18f;

                for (int i = 0; i < MORA_MARK_POINTS; ++i) {
                    float d2 = mora_segment_distance_squared(px, py, &segments[r][i]);

                    if (d2 < nearest2) {
                        nearest2 = d2;
                    }
                }

                mora_blend(rgba, THEME.mark_ring,
                    mora_coverage(stroke - sqrtf(nearest2) + 0.5f) * MARK_OPACITY[r]);
            }

            float dx = px - half, dy = py - half;

            mora_blend(rgba, THEME.mark_dot, mora_coverage(dot_radius - sqrtf(dx * dx + dy * dy) + 0.5f));
            /* AlphaBlend expects premultiplied BGRA. */
            pixels[y * size + x] = (DWORD)(rgba[3] * 255.0f + 0.5f) << 24 | (DWORD)(rgba[0] + 0.5f) << 16 |
                (DWORD)(rgba[1] + 0.5f) << 8 | (DWORD)(rgba[2] + 0.5f);
        }
    }

    HDC memory = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(memory, bitmap);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    AlphaBlend(dc, left, top, size, size, memory, 0, 0, size, size, blend);
    SelectObject(memory, old);
    DeleteDC(memory);
    DeleteObject(bitmap);
}
