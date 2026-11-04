#include "gfx.h"
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>

namespace gfx {

uint16_t* fb;

void init() {
#ifdef PG_HOST
    fb = LCD_GetVRAMAddress();
#else
    fb = vram;                          // fixed VRAM address from calc.h
#endif
}

uint16_t mix(uint16_t a, uint16_t b, int t) {
    if (t <= 0) return a;
    if (t >= 256) return b;
    const int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
    const int rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
    const int r = ra + (((rb - ra) * t) >> 8), g = ga + (((gb - ga) * t) >> 8), bl = ba + (((bb - ba) * t) >> 8);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t shade(uint16_t c, int s) {
    const int r = ((c >> 11) * s) >> 8, g = (((c >> 5) & 63) * s) >> 8, b = ((c & 31) * s) >> 8;
    return (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b));
}

uint16_t tint(uint16_t c, int t) { return mix(c, 0xFFFF, t); }

void fillSpanP(uint16_t* dst, int pixels, uint16_t c) {
    if (pixels <= 0) return;
    if ((uintptr_t)dst & 2) { *dst++ = c; --pixels; }
    const uint32_t p = pair(c);
    u32a* d = (u32a*)dst;
    int n = pixels >> 1;
    for (; n >= 4; n -= 4) { d[0] = p; d[1] = p; d[2] = p; d[3] = p; d += 4; }
    for (; n > 0; --n) *d++ = p;
    if (pixels & 1) dst[pixels - 1] = c;
}

void copyRowP(uint16_t* dst, const uint16_t* src) {
    u32a* d = (u32a*)dst;
    const u32a* s = (const u32a*)src;
    for (int n = PW / 2; n > 0; n -= 8) {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        d[4] = s[4]; d[5] = s[5]; d[6] = s[6]; d[7] = s[7];
        d += 8; s += 8;
    }
}

void pixel(int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) *at(x, y) = c;
}

void vspan(int x, int y0, int y1, uint16_t c) {
    if ((unsigned)x >= (unsigned)W) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= H) y1 = H - 1;
    if (y1 < y0) return;
    // logical y1 (bottom) is the leftmost physical pixel of the run
    fillSpanP(at(x, y1), y1 - y0 + 1, c);
}

void hspan(int x0, int x1, int y, uint16_t c) {
    if ((unsigned)y >= (unsigned)H) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= W) x1 = W - 1;
    uint16_t* d = at(x0, y);
    for (int x = x0; x <= x1; ++x, d += PW) *d = c;
}

void fillRect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (x + w > W) w = W - x;
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; ++i) vspan(x + i, y, y + h - 1, c);
}

void frameRect(int x, int y, int w, int h, uint16_t c, int t) {
    fillRect(x, y, w, t, c);
    fillRect(x, y + h - t, w, t, c);
    fillRect(x, y + t, t, h - 2 * t, c);
    fillRect(x + w - t, y + t, t, h - 2 * t, c);
}

void blendRect(int x, int y, int w, int h, uint16_t c, int t) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; ++i) {
        uint16_t* d = at(x + i, y + h - 1);
        for (int j = 0; j < h; ++j) d[j] = mix(d[j], c, t);
    }
}

static int isqrt(int v) {
    if (v <= 0) return 0;
    int r = 0, bit = 1 << 14;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

void fillCircle(int cx, int cy, int r, uint16_t c) {
    for (int dx = -r; dx <= r; ++dx) {
        const int h = isqrt(r * r - dx * dx);
        vspan(cx + dx, cy - h, cy + h, c);
    }
}

void ring(int cx, int cy, int r0, int r1, uint16_t c) {
    for (int dx = -r1; dx <= r1; ++dx) {
        const int ho = isqrt(r1 * r1 - dx * dx);
        if (dx * dx >= r0 * r0) { vspan(cx + dx, cy - ho, cy + ho, c); continue; }
        const int hi = isqrt(r0 * r0 - dx * dx);
        vspan(cx + dx, cy - ho, cy - hi, c);
        vspan(cx + dx, cy + hi, cy + ho, c);
    }
}

void triangleV(int ax, int ay, int x0, int x1, int by, uint16_t c) {
    // For every column, the span between the base and the edge towards the apex.
    if (x1 < x0) { const int t = x0; x0 = x1; x1 = t; }
    const int dy = ay - by;
    for (int x = x0; x <= x1; ++x) {
        int f;   // 0..256 fraction of the way to the apex
        if (x <= ax) f = ax == x0 ? 256 : ((x - x0) * 256) / (ax - x0);
        else f = ax == x1 ? 256 : ((x1 - x) * 256) / (x1 - ax);
        const int ye = by + ((dy * f) >> 8);
        if (ye < by) vspan(x, ye, by, c); else vspan(x, by, ye, c);
    }
}

void line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ---------------------------------------------------------------------------
// 5x7 font, ASCII 0x20..0x5F (lower case is drawn as upper case). One byte per
// row, bit 4 = leftmost pixel.
// ---------------------------------------------------------------------------
static const uint8_t FONT[64][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // !
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // "
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // #
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // $
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // %
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // &
    {0x0C,0x04,0x08,0x00,0x00,0x00,0x00}, // '
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // (
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // )
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // *
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // +
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, // ,
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // .
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // /
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // :
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, // ;
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // <
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // =
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // ?
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // @
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // [
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // backslash
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ]
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // _
};

int textWidth(const char* s, int scale) {
    int n = 0;
    while (s[n]) ++n;
    return n ? n * 6 * scale - scale : 0;
}

void text(int x, int y, const char* s, int scale, uint16_t c) {
    for (; *s; ++s, x += 6 * scale) {
        int ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 0x20 || ch > 0x5F) ch = '?';
        const uint8_t* g = FONT[ch - 0x20];
        for (int col = 0; col < 5; ++col) {
            // runs down each column so the fill is a vertical span
            int row = 0;
            while (row < 7) {
                if (!(g[row] & (0x10 >> col))) { ++row; continue; }
                int end = row;
                while (end + 1 < 7 && (g[end + 1] & (0x10 >> col))) ++end;
                fillRect(x + col * scale, y + row * scale, scale, (end - row + 1) * scale, c);
                row = end + 1;
            }
        }
    }
}

void textCentered(int cx, int y, const char* s, int scale, uint16_t c) {
    text(cx - textWidth(s, scale) / 2, y, s, scale, c);
}

void textShadow(int x, int y, const char* s, int scale, uint16_t c, uint16_t shadow) {
    const int o = scale > 1 ? scale / 2 + 1 : 1;
    text(x + o, y + o, s, scale, shadow);
    text(x, y, s, scale, c);
}

char* putStr(char* d, const char* s) {
    while (*s) *d++ = *s++;
    *d = 0;
    return d;
}

char* putInt(char* d, int32_t v) {
    char tmp[12];
    int n = 0;
    uint32_t u = (uint32_t)v;
    if (v < 0) { *d++ = '-'; u = 0u - u; }
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *d++ = tmp[--n];
    *d = 0;
    return d;
}

static const int16_t SIN_Q[257] = {
    0, 101, 201, 302, 402, 503, 603, 704, 804, 904, 1005, 1105, 1205, 1306, 1406, 1506, 1606, 1706, 1806, 1906,
    2006, 2105, 2205, 2305, 2404, 2503, 2603, 2702, 2801, 2900, 2999, 3098, 3196, 3295, 3393, 3492, 3590, 3688,
    3786, 3883, 3981, 4078, 4176, 4273, 4370, 4467, 4563, 4660, 4756, 4852, 4948, 5044, 5139, 5235, 5330, 5425,
    5520, 5614, 5708, 5803, 5897, 5990, 6084, 6177, 6270, 6363, 6455, 6547, 6639, 6731, 6823, 6914, 7005, 7096,
    7186, 7276, 7366, 7456, 7545, 7635, 7723, 7812, 7900, 7988, 8076, 8163, 8250, 8337, 8423, 8509, 8595, 8680,
    8765, 8850, 8935, 9019, 9102, 9186, 9269, 9352, 9434, 9516, 9598, 9679, 9760, 9841, 9921, 10001, 10080, 10159,
    10238, 10316, 10394, 10471, 10549, 10625, 10702, 10778, 10853, 10928, 11003, 11077, 11151, 11224, 11297,
    11370, 11442, 11514, 11585, 11656, 11727, 11797, 11866, 11935, 12004, 12072, 12140, 12207, 12274, 12340,
    12406, 12472, 12537, 12601, 12665, 12729, 12792, 12854, 12916, 12978, 13039, 13100, 13160, 13219, 13279,
    13337, 13395, 13453, 13510, 13567, 13623, 13678, 13733, 13788, 13842, 13896, 13949, 14001, 14053, 14104,
    14155, 14206, 14256, 14305, 14354, 14402, 14449, 14497, 14543, 14589, 14635, 14680, 14724, 14768, 14811,
    14854, 14896, 14937, 14978, 15019, 15059, 15098, 15137, 15175, 15213, 15250, 15286, 15322, 15357, 15392,
    15426, 15460, 15493, 15525, 15557, 15588, 15619, 15649, 15679, 15707, 15736, 15763, 15791, 15817, 15843,
    15868, 15893, 15917, 15941, 15964, 15986, 16008, 16029, 16049, 16069, 16088, 16107, 16125, 16143, 16160,
    16176, 16192, 16207, 16221, 16235, 16248, 16261, 16273, 16284, 16295, 16305, 16315, 16324, 16332, 16340,
    16347, 16353, 16359, 16364, 16369, 16373, 16376, 16379, 16381, 16383, 16384, 16384
};

int isin(int a) {
    a &= 1023;
    if (a < 256) return SIN_Q[a];
    if (a < 512) return SIN_Q[512 - a];
    if (a < 768) return -SIN_Q[a - 512];
    return -SIN_Q[1024 - a];
}

}  // namespace gfx
