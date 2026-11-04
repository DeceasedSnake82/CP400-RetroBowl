// Framebuffer helpers for a LANDSCAPE game on the portrait 320x528 panel.
//
// The calculator is held sideways (keypad on the right). All drawing uses logical
// coordinates: lx 0..527 left to right, ly 0..319 top to bottom. The mapping to the
// physical VRAM (px 0..319, py 0..527) is
//     px = 319 - ly,  py = lx      ->  index = lx * 320 + (319 - ly)
// so one logical COLUMN is one contiguous physical row: vertical spans are the fast path.
#pragma once

#include <stdint.h>

namespace gfx {

constexpr int PW = 320, PH = 528;            // physical panel
constexpr int W = 528, H = 320;              // logical (what the player sees)

typedef uint32_t __attribute__((may_alias)) u32a;

extern uint16_t* fb;                         // physical VRAM, set by init()
void init();

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint32_t pair(uint16_t c) { return ((uint32_t)c << 16) | c; }

// Blend two RGB565 colours, t = 0..256 (0 = a, 256 = b).
uint16_t mix(uint16_t a, uint16_t b, int t);
// Darken/lighten: scale 0..256 (256 = unchanged), add = 0..256 towards white.
uint16_t shade(uint16_t c, int scale);
uint16_t tint(uint16_t c, int towardsWhite);

inline uint16_t* at(int lx, int ly) { return fb + lx * PW + (PW - 1 - ly); }

// Physical row helpers (used for whole-screen fills)
void fillSpanP(uint16_t* dst, int pixels, uint16_t c);           // any alignment
void copyRowP(uint16_t* dst, const uint16_t* src);              // one 320-pixel row, 4-byte aligned

// Logical drawing, all clipped to the screen
void pixel(int x, int y, uint16_t c);
void vspan(int x, int y0, int y1, uint16_t c);                  // inclusive, fast
void hspan(int x0, int x1, int y, uint16_t c);                  // inclusive, strided
void fillRect(int x, int y, int w, int h, uint16_t c);
void frameRect(int x, int y, int w, int h, uint16_t c, int t = 1);
void blendRect(int x, int y, int w, int h, uint16_t c, int t);  // translucent fill (t 0..256)
void fillCircle(int cx, int cy, int r, uint16_t c);
void ring(int cx, int cy, int r0, int r1, uint16_t c);          // r0 <= dist <= r1
// Triangle with a horizontal base: apex at (ax, ay), base from (x0, by) to (x1, by).
void triangleV(int ax, int ay, int x0, int x1, int by, uint16_t c);
void line(int x0, int y0, int x1, int y1, uint16_t c);

// Text: 5x7 font, scale >= 1
int  textWidth(const char* s, int scale);
void text(int x, int y, const char* s, int scale, uint16_t c);
void textCentered(int cx, int y, const char* s, int scale, uint16_t c);
void textShadow(int x, int y, const char* s, int scale, uint16_t c, uint16_t shadow);

char* putStr(char* d, const char* s);
char* putInt(char* d, int32_t v);

// sin/cos for angles 0..1023 (full turn), Q14
int isin(int a);
inline int icos(int a) { return isin(a + 256); }

}  // namespace gfx
