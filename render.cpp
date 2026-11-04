// Pocket Gridiron - renderer. Everything is drawn in logical (landscape) coordinates; one logical
// column is one contiguous physical VRAM row (gfx.h), so the field is built from pre-made column
// templates and sprites are drawn column by column from run-length data.
#include "render.h"
#include "gfx.h"
#include "art.h"

using gfx::rgb;

namespace render {

namespace {

constexpr int PW = gfx::PW;
constexpr int SW = gfx::W, SH = gfx::H;

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
constexpr uint16_t BLACK    = rgb(0, 0, 0);
constexpr uint16_t WHITE    = rgb(255, 255, 255);
constexpr uint16_t INK      = rgb(16, 16, 22);       // sprite outline
constexpr uint16_t HUD_TOP  = rgb(34, 38, 56);
constexpr uint16_t HUD_BOT  = rgb(12, 13, 22);
constexpr uint16_t HUD_LINE = rgb(90, 100, 140);
constexpr uint16_t CROWD_BG = rgb(20, 20, 32);
constexpr uint16_t WALL     = rgb(26, 46, 104);
constexpr uint16_t SIDELINE = rgb(34, 104, 46);
constexpr uint16_t BOARD    = rgb(24, 28, 46);
constexpr uint16_t BOARD_HI = rgb(80, 90, 130);
constexpr uint16_t GRASS_A  = rgb(56, 152, 64);
constexpr uint16_t GRASS_B  = rgb(46, 134, 54);
constexpr uint16_t CHALK    = rgb(244, 246, 240);
constexpr uint16_t NUMCOL   = rgb(226, 236, 222);
constexpr uint16_t LOS_COL  = rgb(70, 150, 255);
constexpr uint16_t FD_COL   = rgb(255, 222, 40);
constexpr uint16_t YELLOW   = rgb(255, 224, 40);
constexpr uint16_t ORANGE   = rgb(255, 150, 30);
constexpr uint16_t KEYBOX   = rgb(18, 20, 30);
constexpr uint16_t PANEL_BG = rgb(14, 18, 30);

// Fast translucent darkening of one pixel (no multiplies)
inline uint16_t half(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }
inline uint16_t quarterDark(uint16_t c) { return (uint16_t)(c - ((c >> 2) & 0x39E7)); }

// Pointer to logical (lx, ly); the next row DOWN the screen is at -1.
inline uint16_t* px(int lx, int ly) { return gfx::fb + lx * PW + (PW - 1 - ly); }

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

// Perceived brightness 0..63 (for choosing black or white text on a colour)
int luma(uint16_t c) {
    const int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    return (38 * r + 38 * g + 14 * b) >> 6;
}
uint16_t inkOn(uint16_t c) { return luma(c) > 44 ? INK : WHITE; }

// Darken an RGB565 rectangle to 50% (column-contiguous, two pixels per 32-bit word).
void dimRect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; ++i) {
        uint16_t* d = px(x + i, y + h - 1);        // lowest row = lowest address
        int n = h;
        if ((uintptr_t)d & 2) { *d = half(*d); ++d; --n; }
        gfx::u32a* d32 = (gfx::u32a*)d;
        for (; n >= 2; n -= 2, ++d32) *d32 = (*d32 >> 1) & 0x7BEF7BEFu;
        if (n) { uint16_t* e = (uint16_t*)d32; *e = half(*e); }
    }
}

// ---------------------------------------------------------------------------
// Sprites: ASCII art -> per-column runs (row, length, palette slot)
// ---------------------------------------------------------------------------
enum Slot : uint8_t {
    S_NONE, S_INK, S_HELMET, S_HELMET_HI, S_MASK, S_SKIN, S_JERSEY, S_JERSEY_SH, S_NUMBER,
    S_PANTS, S_SOCKS, S_SHOES, S_BALL, S_BALL_HI, S_LACE, S_COUNT
};

int slotOf(char ch) {
    switch (ch) {
        case 'k': return S_INK;      case 'H': return S_HELMET;  case 'h': return S_HELMET_HI;
        case 'm': return S_MASK;     case 's': return S_SKIN;    case 'J': return S_JERSEY;
        case 'j': return S_JERSEY_SH; case 'n': return S_NUMBER; case 'P': return S_PANTS;
        case 'w': return S_SOCKS;    case 'S': return S_SHOES;   case 'B': return S_BALL;
        case 'L': return S_BALL_HI;  case 'W': return S_LACE;
        default:  return S_NONE;
    }
}

struct Run { uint8_t row, len, slot; };
struct Spr { uint8_t w, h; uint16_t col0; };     // col0: index into colOff (w + 1 entries)

enum SprId { SP_SKILL0, SP_SKILL1, SP_SKILL2, SP_BIG0, SP_BIG1, SP_BIG2, SP_DOWN, SP_BALL, SP_COUNT };

Run runs[640];
uint16_t colOff[128];
Spr sprs[SP_COUNT];
int nRuns, nCols;

void buildSprite(Spr& s, const char* const* rows, int h) {
    int w = 0;
    while (rows[0][w]) ++w;
    s.w = (uint8_t)w;
    s.h = (uint8_t)h;
    s.col0 = (uint16_t)nCols;
    for (int c = 0; c < w; ++c) {
        colOff[nCols++] = (uint16_t)nRuns;
        int r = 0;
        while (r < h) {
            const int sl = slotOf(rows[r][c]);
            if (!sl) { ++r; continue; }
            int e = r + 1;
            while (e < h && slotOf(rows[e][c]) == sl) ++e;
            if (nRuns < (int)(sizeof(runs) / sizeof(runs[0])))
                runs[nRuns++] = { (uint8_t)r, (uint8_t)(e - r), (uint8_t)sl };
            r = e;
        }
    }
    colOff[nCols] = (uint16_t)nRuns;             // end of the last column (next sprite reuses it)
}

// Draw sprite `s` with its top-left at (x0, top); `flip` mirrors it horizontally.
void blit(const Spr& s, int x0, int top, bool flip, const uint16_t* pal) {
    if (x0 >= SW || x0 + s.w <= 0 || top >= SH || top + s.h <= 0) return;
    for (int c = 0; c < s.w; ++c) {
        const int lx = x0 + (flip ? s.w - 1 - c : c);
        if ((unsigned)lx >= (unsigned)SW) continue;
        uint16_t* col = gfx::fb + lx * PW + (PW - 1);
        const int k1 = colOff[s.col0 + c + 1];
        for (int k = colOff[s.col0 + c]; k < k1; ++k) {
            const Run& r = runs[k];
            int y0 = top + r.row, y1 = y0 + r.len - 1;
            if (y0 < 0) y0 = 0;
            if (y1 >= SH) y1 = SH - 1;
            const uint16_t v = pal[r.slot];
            uint16_t* d = col - y0;
            for (int y = y0; y <= y1; ++y) *d-- = v;
        }
    }
}

// ---------------------------------------------------------------------------
// Field column templates (one physical row each = one logical column)
// ---------------------------------------------------------------------------
enum ColType : uint8_t {
    CT_OUT, CT_ENDLINE, CT_EZ_OWN, CT_EZ_OPP, CT_GOAL, CT_LINE5,
    CT_GRASS_A, CT_GRASS_B, CT_TICK_A, CT_TICK_B, CT_COUNT
};
uint16_t tmpl[CT_COUNT][PW] __attribute__((aligned(4)));

// Column type per world pixel column (world px = yards * PX_X), with a margin on both sides.
constexpr int TAB_MIN = -64;
constexpr int TAB_N = 120 * PX_X + 128;
uint8_t colType[TAB_N];

int ezOwnTeam = -1, ezOppTeam = -1;              // teams the end-zone templates were built for

// Stands: 64-column crowd pattern for ly 22..27 (physical index 292..297), copied per column.
constexpr int CROWD_Y0 = 22, CROWD_H = 6, CROWD_P = 64;
uint16_t crowd[CROWD_P * CROWD_H] __attribute__((aligned(4)));
// Near-side boards: ly 312..319 (physical 0..7), 64-column pattern anchored to the field.
constexpr int BOARD_H = 8, BOARD_P = 64;
uint16_t boards[BOARD_P * BOARD_H] __attribute__((aligned(4)));
const uint16_t SKIN_C[4] = { rgb(240, 196, 150), rgb(198, 140, 96), rgb(140, 92, 58), rgb(96, 62, 40) };
// HUD background segment ly 0..21 (physical 298..319)
uint16_t hudSeg[HUD_H] __attribute__((aligned(4)));

constexpr int HASH_T = FIELD_TOP + (int)((pg::HASH_TOP * PX_Y) >> pg::FXS);
constexpr int HASH_B = FIELD_TOP + (int)((pg::HASH_BOT * PX_Y) >> pg::FXS);
constexpr int FIELD_MID = (FIELD_TOP + FIELD_BOT) / 2;

inline void put(uint16_t* row, int ly, uint16_t c) { row[PW - 1 - ly] = c; }

// Far (top) side of the field is a bit darker than the near side: free depth cue.
uint16_t depth(uint16_t c, int ly) {
    const int t = (ly - FIELD_TOP) * 256 / (FIELD_BOT - FIELD_TOP);
    return gfx::shade(c, 206 + ((50 * t) >> 8));
}

// Stands, sidelines and boards around the playing surface.
void buildFrame(uint16_t* row, bool inField) {
    for (int ly = 0; ly < HUD_H; ++ly) put(row, ly, hudSeg[HUD_H - 1 - ly]);
    for (int ly = CROWD_Y0; ly < CROWD_Y0 + CROWD_H; ++ly) put(row, ly, CROWD_BG);
    put(row, 28, WALL);
    put(row, 29, gfx::shade(SIDELINE, 190));
    put(row, 30, gfx::shade(SIDELINE, 210));
    put(row, 31, inField ? gfx::shade(CHALK, 215) : gfx::shade(SIDELINE, 215));
    put(row, FIELD_BOT + 1, inField ? CHALK : SIDELINE);
    put(row, FIELD_BOT + 2, inField ? CHALK : SIDELINE);
    for (int ly = FIELD_BOT + 3; ly < 312; ++ly) put(row, ly, gfx::shade(SIDELINE, 256 + (ly - FIELD_BOT) * 2));
    put(row, 312, BOARD_HI);
    for (int ly = 313; ly < SH; ++ly) put(row, ly, BOARD);
}

void buildSolid(uint16_t* row, uint16_t c, bool inField) {
    buildFrame(row, inField);
    for (int ly = FIELD_TOP; ly <= FIELD_BOT; ++ly) put(row, ly, depth(c, ly));
}

void buildTick(uint16_t* row, uint16_t grass) {
    buildSolid(row, grass, true);
    const int marks[4] = { FIELD_TOP + 1, HASH_T, HASH_B, FIELD_BOT - 1 };
    for (int m : marks)
        for (int ly = m - 1; ly <= m + 1; ++ly) put(row, ly, depth(CHALK, ly));
}

void buildEndZones(int own, int opp) {
    if (own != ezOwnTeam) {
        buildSolid(tmpl[CT_EZ_OWN], gfx::shade(pg::TEAMS[own].c1, 150), true);
        ezOwnTeam = own;
    }
    if (opp != ezOppTeam) {
        buildSolid(tmpl[CT_EZ_OPP], gfx::shade(pg::TEAMS[opp].c1, 150), true);
        ezOppTeam = opp;
    }
}

uint8_t typeAt(int wpx) {
    constexpr int END = 120 * PX_X;               // 840
    if (wpx < 0 || wpx > END) return CT_OUT;
    if (wpx <= 1 || wpx >= END - 1) return CT_ENDLINE;
    const int yard = wpx / PX_X, sub = wpx - yard * PX_X;
    if (wpx == 10 * PX_X || wpx == 10 * PX_X - 1 || wpx == 110 * PX_X || wpx == 110 * PX_X - 1) return CT_GOAL;
    if (yard < 10) return CT_EZ_OWN;
    if (yard >= 110) return CT_EZ_OPP;
    const bool b = ((yard - 10) / 5) & 1;
    if (sub == 0) return yard % 5 == 0 ? CT_LINE5 : (b ? CT_TICK_B : CT_TICK_A);
    return b ? CT_GRASS_B : CT_GRASS_A;
}

void buildField() {
    for (int i = 0; i < HUD_H; ++i)
        hudSeg[HUD_H - 1 - i] = i == HUD_H - 1 ? HUD_LINE : gfx::mix(HUD_TOP, HUD_BOT, i * 256 / (HUD_H - 1));
    buildSolid(tmpl[CT_OUT], SIDELINE, false);
    buildSolid(tmpl[CT_ENDLINE], CHALK, true);
    buildSolid(tmpl[CT_GOAL], CHALK, true);
    buildSolid(tmpl[CT_LINE5], CHALK, true);
    buildSolid(tmpl[CT_GRASS_A], GRASS_A, true);
    buildSolid(tmpl[CT_GRASS_B], GRASS_B, true);
    buildTick(tmpl[CT_TICK_A], GRASS_A);
    buildTick(tmpl[CT_TICK_B], GRASS_B);
    for (int i = 0; i < TAB_N; ++i) colType[i] = typeAt(i + TAB_MIN);
    ezOwnTeam = ezOppTeam = -1;

    // Crowd: two tiers of little figures (head pixel over a 2-pixel shirt), staggered.
    static const uint16_t SHIRTS[8] = {
        rgb(170, 60, 60), rgb(60, 90, 170), rgb(190, 170, 70), rgb(90, 150, 90),
        rgb(160, 160, 170), rgb(180, 100, 40), rgb(120, 70, 150), rgb(220, 210, 190),
    };
    for (int c = 0; c < CROWD_P; ++c) {
        for (int i = 0; i < CROWD_H; ++i) crowd[c * CROWD_H + i] = gfx::shade(CROWD_BG, 140);
        for (int tier = 0; tier < 2; ++tier) {
            const int pid = (c + tier) >> 1, first = ((c + tier) & 1) == 0;
            const uint32_t h = hash32((uint32_t)(pid * 31 + tier * 1000 + 5));
            const int head = CROWD_Y0 + tier * 3;              // ly of the head row
            const int dim = tier ? 200 : 140;                  // back tier is darker
            if ((h & 7) == 0) {                                // empty seat
                crowd[c * CROWD_H + (CROWD_Y0 + CROWD_H - 1 - (head + 2))] = gfx::shade(rgb(40, 40, 60), dim);
                continue;
            }
            const uint16_t shirt = gfx::shade(SHIRTS[(h >> 4) & 7], dim);
            const uint16_t skin = gfx::shade(SKIN_C[(h >> 8) & 3], dim);
            for (int r = 1; r <= 2; ++r) crowd[c * CROWD_H + (CROWD_Y0 + CROWD_H - 1 - (head + r))] = shirt;
            if (first) crowd[c * CROWD_H + (CROWD_Y0 + CROWD_H - 1 - head)] = skin;
        }
    }
    // Advertising boards on the near side: 32-column panels, scrolling with the field.
    static const uint16_t PANELS[4] = { rgb(28, 40, 110), rgb(120, 26, 34), rgb(24, 90, 70), rgb(70, 40, 110) };
    for (int c = 0; c < BOARD_P; ++c) {
        const int panel = c >> 5, k = c & 31;
        const uint16_t base = PANELS[(panel * 3 + 1) & 3];
        for (int i = 0; i < BOARD_H; ++i) {
            const int ly = SH - 1 - i;
            uint16_t v = base;
            if (ly == SH - BOARD_H) v = BOARD_HI;              // top edge
            else if (k == 0 || k == 31) v = gfx::shade(base, 120);
            else if (ly >= 314 && ly <= 317 && k >= 5 && k <= 26 && ((k + ly) % 5) != 0) v = gfx::tint(base, 120 + (panel & 1) * 60);
            boards[c * BOARD_H + i] = v;
        }
    }
}

// ---------------------------------------------------------------------------
// Team badge (shield) - also painted at midfield
// ---------------------------------------------------------------------------
// One shield-shaped layer: flat top with rounded corners, straight sides, pointed bottom.
void shieldLayer(int cx, int top, int halfW, int h, uint16_t c) {
    for (int dx = -halfW; dx <= halfW; ++dx) {
        const int a = dx < 0 ? -dx : dx;
        const int t = top + (a >= halfW - 1 ? 2 : (a >= halfW - 2 ? 1 : 0));
        const int b = top + h - (a * 15) / 14;
        gfx::vspan(cx + dx, t, b, c);
    }
}

void badge(int cx, int cy, int team, int s, uint16_t toward, int amt) {
    if (s < 1) s = 1;
    const pg::TeamDef& T = pg::TEAMS[(unsigned)team % pg::NTEAMS];
    const uint16_t c1 = gfx::mix(T.c1, toward, amt), c2 = gfx::mix(T.c2, toward, amt);
    const uint16_t ink = gfx::mix(INK, toward, amt);
    const int halfW = 14 * s, h = 34 * s, top = cy - h / 2;
    shieldLayer(cx, top, halfW, h, ink);
    shieldLayer(cx, top + s, halfW - s, h - 2 * s - s / 2, c2);
    shieldLayer(cx, top + 3 * s, halfW - 3 * s, h - 6 * s - s, c1);
    // chevron in the lower half
    const int vy = top + 19 * s;
    for (int dx = -9 * s; dx <= 9 * s; ++dx) {
        const int a = dx < 0 ? -dx : dx;
        const int y0 = vy + a / 2;
        gfx::vspan(cx + dx, y0, y0 + 2 * s, c2);
    }
    // abbreviation
    const int ty = top + 8 * s;
    const uint16_t tc = gfx::mix(inkOn(T.c1), toward, amt);
    if (amt == 0) gfx::textCentered(cx + (s > 1 ? 1 : 0), ty + (s > 1 ? 1 : 0), T.abbr, s, tc == WHITE ? INK : gfx::shade(T.c1, 120));
    gfx::textCentered(cx, ty, T.abbr, s, tc);
}

// ---------------------------------------------------------------------------
// The play screen pieces
// ---------------------------------------------------------------------------
inline int camPixels(pg::fx camX) {
    // ceil(camX * PX_X / FX1), so that a line at world pixel W appears at W - camPx == sx(...)
    return -(int)((-(int64_t)camX * PX_X) >> pg::FXS);
}

void drawFieldColumns(pg::fx camX) {
    const int camPx = camPixels(camX);
    int idx = camPx - TAB_MIN;
    const int par = camPx >> 1;                      // crowd moves at half speed (parallax)
    uint16_t* col = gfx::fb;
    for (int lx = 0; lx < SW; ++lx, ++idx, col += PW) {
        const uint8_t t = (unsigned)idx < (unsigned)TAB_N ? colType[idx] : (uint8_t)CT_OUT;
        gfx::copyRowP(col, tmpl[t]);
        const gfx::u32a* s = (const gfx::u32a*)(crowd + ((par + lx) & (CROWD_P - 1)) * CROWD_H);
        gfx::u32a* d = (gfx::u32a*)(col + (PW - CROWD_Y0 - CROWD_H));
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        const gfx::u32a* b = (const gfx::u32a*)(boards + ((camPx + lx) & (BOARD_P - 1)) * BOARD_H);
        gfx::u32a* e = (gfx::u32a*)col;
        e[0] = b[0]; e[1] = b[1]; e[2] = b[2]; e[3] = b[3];
    }
}

// Small arrow (pointing left when dir < 0) next to a yard number.
void yardArrow(int tipX, int cy, int dir, uint16_t c) {
    for (int k = 0; k < 4; ++k) gfx::vspan(tipX - dir * k, cy - k, cy + k, c);
}

void drawFieldMarks(const pg::Play& pl, const pg::Match& m, pg::fx camX) {
    // yard numbers 10..50..10 at both sidelines
    for (int y = 20; y <= 100; y += 10) {
        const int x = sx(y * pg::FX1, camX);
        if (x < -40 || x > SW + 40) continue;
        const int n = y <= 60 ? y - 10 : 110 - y;
        const char d1[2] = { (char)('0' + n / 10), 0 };
        const int rows[2] = { FIELD_TOP + 42, FIELD_BOT - 42 - 14 };
        for (int r : rows) {
            gfx::text(x - 13, r, d1, 2, NUMCOL);
            gfx::text(x + 4, r, "0", 2, NUMCOL);
            if (y < 60) yardArrow(x - 20, r + 7, -1, NUMCOL);
            else if (y > 60) yardArrow(x + 20, r + 7, 1, NUMCOL);
        }
    }
    // team names stacked in the end zones
    for (int side = 0; side < 2; ++side) {
        const int cx = sx((side ? 115 : 5) * pg::FX1, camX);
        if (cx < -30 || cx > SW + 30) continue;
        const pg::TeamDef& T = pg::TEAMS[side ? m.opp : m.user];
        const uint16_t ez = gfx::shade(T.c1, 150);
        const uint16_t c = gfx::mix(ez, luma(T.c2) > 10 ? T.c2 : WHITE, 150);
        int n = 0;
        while (T.abbr[n]) ++n;
        const int step = 36, total = n * step - 8;
        int ty = FIELD_MID - total / 2;
        for (int i = 0; i < n; ++i, ty += step) {
            const char ch[2] = { T.abbr[i], 0 };
            gfx::text(cx - 10, ty, ch, 4, c);
        }
    }
    // midfield emblem of the home (user) team, painted into the grass
    {
        const int cx = sx(60 * pg::FX1, camX);
        if (cx > -50 && cx < SW + 50) badge(cx, FIELD_MID, m.user, 2, GRASS_A, 90);
    }
    // line of scrimmage and first-down marker
    const int xl = sx(pl.los, camX);
    gfx::vspan(xl - 1, FIELD_TOP, FIELD_BOT, LOS_COL);
    gfx::vspan(xl, FIELD_TOP, FIELD_BOT, LOS_COL);
    if (pl.firstDown < pg::GOAL_OPP) {
        const int xf = sx(pl.firstDown, camX);
        gfx::vspan(xf - 1, FIELD_TOP, FIELD_BOT, FD_COL);
        gfx::vspan(xf, FIELD_TOP, FIELD_BOT, FD_COL);
    }
}

// Shadow ellipse under a player / the ball (darkens the ground, very cheap).
void shadow(int cx, int cy, int rx) {
    for (int dx = -rx; dx <= rx; ++dx) {
        const int lx = cx + dx;
        if ((unsigned)lx >= (unsigned)SW) continue;
        const int a = dx < 0 ? -dx : dx;
        const int hh = a < rx - 1 ? 1 : 0;
        for (int ly = cy - hh; ly <= cy + hh; ++ly) {
            if ((unsigned)ly >= (unsigned)SH) continue;
            uint16_t* d = px(lx, ly);
            *d = quarterDark(*d);
        }
    }
}

const uint16_t* const SKINS = SKIN_C;

void teamPalette(uint16_t* pal, const pg::TeamDef& T) {
    pal[S_NONE] = 0;
    pal[S_INK] = INK;
    pal[S_HELMET] = T.c2;
    pal[S_HELMET_HI] = gfx::tint(T.c2, 110);
    pal[S_MASK] = rgb(170, 170, 180);
    pal[S_SKIN] = SKINS[0];
    pal[S_JERSEY] = T.c1;
    pal[S_JERSEY_SH] = gfx::shade(T.c1, 175);
    pal[S_NUMBER] = luma(T.c1) > 44 ? gfx::shade(T.c2, 200) : rgb(245, 245, 245);
    pal[S_PANTS] = gfx::mix(T.c2, rgb(235, 235, 225), 130);
    pal[S_SOCKS] = T.c1;
    pal[S_SHOES] = rgb(40, 40, 48);
    pal[S_BALL] = rgb(150, 76, 34);
    pal[S_BALL_HI] = rgb(196, 112, 60);
    pal[S_LACE] = rgb(250, 250, 250);
}

uint16_t ballPal[S_COUNT];

void drawBallAt(int cx, int cy) { blit(sprs[SP_BALL], cx - 3, cy - 2, false, ballPal); }

// Flattened ring on the ground where a pass will come down, with a centre dot.
void landingMarker(int x, int y, int t) {
    const uint16_t c = ((t >> 3) & 1) ? YELLOW : ORANGE;
    const int rx = 7 + ((t >> 2) & 1);
    for (int a = 0; a < 1024; a += 32) {
        const int cs = gfx::icos(a), sn = gfx::isin(a);
        gfx::pixel(x + ((cs * rx) >> 14), y + ((sn * 4) >> 14), c);
        gfx::pixel(x + ((cs * (rx - 1)) >> 14), y + ((sn * 3) >> 14), c);
    }
    gfx::fillRect(x - 1, y, 3, 1, c);
}

// Key label (white digit on a dark rounded box) with its top-left at (bx, by).
void keyLabel(int bx, int by, int digit) {
    gfx::fillRect(bx + 1, by, 9, 11, rgb(220, 220, 230));
    gfx::fillRect(bx, by + 1, 11, 9, rgb(220, 220, 230));
    gfx::fillRect(bx + 1, by + 1, 9, 9, KEYBOX);
    const char s[2] = { (char)('0' + digit), 0 };
    gfx::text(bx + 3, by + 2, s, 1, WHITE);
}

constexpr int ANIM_SHIFT = 5;                    // Player::anim advances ~64 per yard: one stride per 2 yards

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void init() {
    nRuns = nCols = 0;
#define BUILD(id, a) buildSprite(sprs[id], a, (int)(sizeof(a) / sizeof(a[0])))
    BUILD(SP_SKILL0, art::SKILL_STAND);
    BUILD(SP_SKILL1, art::SKILL_RUN_A);
    BUILD(SP_SKILL2, art::SKILL_RUN_B);
    BUILD(SP_BIG0, art::BIG_STAND);
    BUILD(SP_BIG1, art::BIG_RUN_A);
    BUILD(SP_BIG2, art::BIG_RUN_B);
    BUILD(SP_DOWN, art::DOWN);
    BUILD(SP_BALL, art::BALL);
#undef BUILD
    teamPalette(ballPal, pg::TEAMS[0]);
    buildField();
}

namespace {
pg::fx clampCam(pg::fx c) {
    constexpr pg::fx lo = -4 * pg::FX1;
    constexpr pg::fx hi = pg::FIELD_LEN + 4 * pg::FX1 - VIEW_W;
    return c < lo ? lo : c > hi ? hi : c;
}
}  // namespace

pg::fx cameraFor(pg::fx focusX) { return clampCam(focusX - (VIEW_W * 35) / 100); }

pg::fx followCamera(pg::fx camX, pg::fx focusX) {
    const pg::fx d = cameraFor(focusX) - camX;
    constexpr pg::fx MIN_STEP = pg::FX1 / 32;     // always move at least a little, then snap
    pg::fx step = d / 8;
    if (step > -MIN_STEP && step < MIN_STEP) step = d > MIN_STEP ? MIN_STEP : (d < -MIN_STEP ? -MIN_STEP : d);
    return clampCam(camX + step);
}

void drawPlay(const pg::Play& pl, const pg::Match& m, pg::fx camX) {
    buildEndZones(m.user, m.opp);
    drawFieldColumns(camX);
    drawFieldMarks(pl, m, camX);

    if (pl.phase == pg::PH_AIR) {
        landingMarker(sx(pl.ball.tx, camX), sy(pl.ball.ty), pl.ball.t);
    }

    // depth order: insertion sort by y (feet)
    int8_t order[pg::NP];
    for (int i = 0; i < pg::NP; ++i) {
        int j = i;
        while (j > 0 && pl.p[order[j - 1]].y > pl.p[i].y) { order[j] = order[j - 1]; --j; }
        order[j] = (int8_t)i;
    }
    int cxs[pg::NP], tops[pg::NP];
    for (int i = 0; i < pg::NP; ++i) {
        cxs[i] = sx(pl.p[i].x, camX);
        shadow(cxs[i] + 1, sy(pl.p[i].y), pl.p[i].state == pg::PS_DOWN ? 8 : 5);
    }

    uint16_t palO[S_COUNT], palD[S_COUNT];
    teamPalette(palO, pg::TEAMS[m.user]);
    teamPalette(palD, pg::TEAMS[m.opp]);

    for (int k = 0; k < pg::NP; ++k) {
        const int i = order[k];
        const pg::Player& P = pl.p[i];
        const int cx = cxs[i], feet = sy(P.y);
        uint16_t* pal = pg::isOffense(i) ? palO : palD;
        pal[S_SKIN] = SKINS[(i * 7 + (pg::isOffense(i) ? m.user : m.opp)) & 3];
        const bool flip = P.face < 0;
        const Spr* s;
        if (P.state == pg::PS_DOWN) {
            s = &sprs[SP_DOWN];
        } else {
            const bool big = P.role == pg::R_OL || P.role == pg::R_DL;
            int f = 0;
            if (P.vx != 0 || P.vy != 0) {
                const int ph = (P.anim >> ANIM_SHIFT) & 3;
                f = ph == 0 ? 1 : ph == 2 ? 2 : 0;
            }
            s = &sprs[(big ? SP_BIG0 : SP_SKILL0) + f];
        }
        const int top = feet - s->h + 1;
        tops[i] = top;
        if (cx < -16 || cx > SW + 16) continue;
        blit(*s, cx - s->w / 2, top, flip, pal);
        if (i == pl.carrier) {
            if (P.state == pg::PS_DOWN) drawBallAt(cx + (flip ? -2 : 2), feet - 6);
            else drawBallAt(cx + (flip ? -3 : 3), feet - 6);
        }
    }

    // ball in flight / loose
    if (pl.carrier < 0) {
        const int bx = sx(pl.ball.x, camX), by = sy(pl.ball.y);
        const int hz = (int)((pl.ball.z * 6) >> pg::FXS);
        if (hz > 0) shadow(bx, by, 3);
        drawBallAt(bx, by - 2 - hz);
    }

    // the player the user steers: arrow above the head
    if (pl.control >= 0 && pl.control < pg::NP && pl.phase != pg::PH_OVER) {
        const int cx = cxs[pl.control], t = tops[pl.control];
        gfx::triangleV(cx, t - 2, cx - 5, cx + 5, t - 9, INK);
        gfx::triangleV(cx, t - 3, cx - 3, cx + 3, t - 8, YELLOW);
    }

    // receiver keys 1..4 while a pass is possible (clamped to the screen edges)
    if (pg::canThrow(pl)) {
        for (int k = 0; k < pg::NTARGETS; ++k) {
            const int i = pg::TARGETS[k];
            int bx = cxs[i] - 5, by = tops[i] - 14;
            if (bx < 2) bx = 2;
            if (bx > SW - 13) bx = SW - 13;
            if (by < HUD_H + 2) by = HUD_H + 2;
            keyLabel(bx, by, k + 1);
        }
    }
}

// ---------------------------------------------------------------------------
// Scoreboard
// ---------------------------------------------------------------------------
namespace {
int teamTag(int x, int team, int score) {
    const pg::TeamDef& T = pg::TEAMS[(unsigned)team % pg::NTEAMS];
    gfx::fillRect(x, 2, 4, 18, T.c2);
    gfx::fillRect(x + 4, 2, 40, 18, T.c1);
    gfx::textCentered(x + 24, 4, T.abbr, 2, inkOn(T.c1));
    char b[8];
    gfx::putInt(b, score);
    gfx::textShadow(x + 50, 4, b, 2, WHITE, BLACK);
    return x + 50 + gfx::textWidth(b, 2);
}
}  // namespace

void drawScoreboard(const pg::Match& m) {
    // background: one 22-pixel segment per column
    for (int lx = 0; lx < SW; ++lx) {
        const gfx::u32a* s = (const gfx::u32a*)hudSeg;
        gfx::u32a* d = (gfx::u32a*)(gfx::fb + lx * PW + (PW - HUD_H));
        for (int i = 0; i < HUD_H / 2; ++i) d[i] = s[i];
    }
    teamTag(4, m.user, m.score[0]);
    teamTag(104, m.opp, m.score[1]);
    gfx::vspan(194, 4, 17, HUD_LINE);

    char b[24], *p;
    // quarter + clock
    if (m.quarter >= 5) {
        gfx::text(204, 4, "OT", 2, YELLOW);
    } else {
        const char q[3] = { 'Q', (char)('0' + m.quarter), 0 };
        gfx::text(204, 4, q, 2, YELLOW);
        const int c = m.clock < 0 ? 0 : m.clock;
        p = gfx::putInt(b, c / 60);
        *p++ = ':';
        *p++ = (char)('0' + (c % 60) / 10);
        *p++ = (char)('0' + c % 10);
        *p = 0;
        gfx::text(236, 4, b, 2, WHITE);
    }
    gfx::vspan(292, 4, 17, HUD_LINE);

    // down & distance
    static const char* const ORD[4] = { "1ST", "2ND", "3RD", "4TH" };
    const int dn = m.down >= 1 && m.down <= 4 ? m.down - 1 : 0;
    p = gfx::putStr(b, ORD[dn]);
    p = gfx::putStr(p, " & ");
    const int ytg = pg::yardsToGo(m);
    if (ytg <= 0) gfx::putStr(p, "GOAL");
    else gfx::putInt(p, ytg);
    gfx::text(302, 4, b, 2, rgb(255, 236, 140));

    // ball position
    const int on = pg::ballOnYard(m);
    if (on < 50) gfx::putInt(gfx::putStr(b, "OWN "), on);
    else if (on == 50) gfx::putStr(b, "50");
    else gfx::putInt(gfx::putStr(b, "OPP "), 100 - on);
    gfx::text(SW - 28 - gfx::textWidth(b, 2), 4, b, 2, rgb(200, 210, 230));   // the pause button sits at the right edge
}

// ---------------------------------------------------------------------------
// Banners
// ---------------------------------------------------------------------------
void drawBanner(const char* text, uint16_t color) {
    int scale = 4;
    if (gfx::textWidth(text, scale) > SW - 16) scale = 3;
    if (gfx::textWidth(text, scale) > SW - 16) scale = 2;
    constexpr int Y = FIELD_MID - 38, H = 44;
    dimRect(0, Y, SW, H);
    gfx::fillRect(0, Y, SW, 2, color);
    gfx::fillRect(0, Y + H - 2, SW, 2, color);
    gfx::textShadow((SW - gfx::textWidth(text, scale)) / 2, Y + (H - 7 * scale) / 2, text, scale, color, BLACK);
}

void drawSubBanner(const char* text, uint16_t color) {
    constexpr int Y = FIELD_MID + 8, H = 24;
    const int tw = gfx::textWidth(text, 2);
    int w = tw + 32;
    if (w > SW) w = SW;
    dimRect((SW - w) / 2, Y, w, H);
    gfx::textShadow((SW - tw) / 2, Y + 5, text, 2, color, BLACK);
}

// ---------------------------------------------------------------------------
// Menu widgets
// ---------------------------------------------------------------------------
namespace {
uint16_t rowBuf[PW] __attribute__((aligned(4)));
}

void drawMenuBackground(int seed) {
    const uint32_t hs = hash32((uint32_t)seed * 2654435761u + 7u);
    constexpr int STANDS = 186, WALL_Y = 246, GRASS_Y = 254;
    const uint16_t skyTop = rgb(4, 6, 20), skyBot = rgb(30, 26, 66);
    for (int ly = 0; ly < SH; ++ly) {
        uint16_t c;
        if (ly < STANDS) c = gfx::mix(skyTop, skyBot, ly * 256 / STANDS);
        else if (ly < WALL_Y) c = gfx::mix(rgb(16, 16, 28), rgb(30, 30, 48), (ly - STANDS) * 256 / (WALL_Y - STANDS));
        else if (ly == WALL_Y) c = rgb(110, 130, 200);
        else if (ly < GRASS_Y) c = WALL;
        else if (ly == GRASS_Y + 6) c = gfx::shade(CHALK, 200);
        else c = gfx::mix(rgb(24, 80, 34), GRASS_A, (ly - GRASS_Y) * 256 / (SH - GRASS_Y));
        rowBuf[PW - 1 - ly] = c;
    }
    for (int lx = 0; lx < SW; ++lx) gfx::copyRowP(gfx::fb + lx * PW, rowBuf);

    // stars
    for (int i = 0; i < 70; ++i) {
        const uint32_t h = hash32(hs + (uint32_t)i * 977u);
        const int x = (int)(h % SW), y = (int)((h >> 10) % 150);
        gfx::pixel(x, y, gfx::mix(rgb(90, 90, 130), WHITE, (int)((h >> 20) & 255)));
    }
    // light towers with a soft glow
    const int nT = 2 + (int)(hs & 1);
    for (int t = 0; t < nT; ++t) {
        const int x = 40 + (t * (SW - 80)) / (nT - 1) + (int)((hs >> (4 + t * 3)) & 15) - 8;
        const int y = 44 + (int)((hs >> (12 + t * 2)) & 15);
        const uint16_t sky = gfx::mix(skyTop, skyBot, y * 256 / STANDS);
        gfx::fillCircle(x, y, 44, gfx::mix(sky, rgb(140, 140, 200), 26));
        gfx::fillCircle(x, y, 30, gfx::mix(sky, rgb(170, 170, 220), 52));
        gfx::fillCircle(x, y, 18, gfx::mix(sky, rgb(220, 220, 240), 90));
        gfx::fillRect(x - 1, y + 9, 3, STANDS - y - 9, rgb(52, 52, 70));
        gfx::fillRect(x - 15, y - 9, 30, 18, rgb(40, 40, 54));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j) gfx::fillRect(x - 13 + i * 9, y - 7 + j * 8, 8, 6, rgb(255, 250, 216));
    }
    // crowd in the stands
    static const uint16_t SHIRTS[6] = {
        rgb(150, 60, 60), rgb(60, 80, 160), rgb(170, 160, 70), rgb(100, 150, 100), rgb(150, 150, 160), rgb(160, 100, 50),
    };
    for (int ly = STANDS + 3; ly < WALL_Y - 2; ly += 3) {
        const int dim = 110 + (ly - STANDS) * 2;
        for (int lx = (ly / 3) & 1; lx < SW; lx += 2) {
            const uint32_t h = hash32(hs ^ (uint32_t)(lx * 7919 + ly * 131));
            if ((h & 3) == 0) continue;
            *px(lx, ly) = gfx::shade(SHIRTS[(h >> 3) % 6], dim);
        }
    }
    // yard lines converging towards the stands
    for (int k = -5; k <= 5; ++k) {
        const int x0 = SW / 2 + k * 56, x1 = SW / 2 + k * 96;
        gfx::line(x0, GRASS_Y + 7, x1, SH - 1, gfx::shade(CHALK, 170));
    }
}

void drawPanel(int x, int y, int w, int h, uint16_t accent) {
    gfx::fillRect(x + 2, y + 2, w - 4, h - 4, PANEL_BG);
    gfx::fillRect(x + 1, y, w - 2, 2, accent);
    gfx::fillRect(x + 1, y + h - 2, w - 2, 2, accent);
    gfx::fillRect(x, y + 1, 2, h - 2, accent);
    gfx::fillRect(x + w - 2, y + 1, 2, h - 2, accent);
    gfx::fillRect(x + 4, y + 4, w - 8, 1, gfx::mix(PANEL_BG, accent, 90));
}

void drawButton(int x, int y, int w, int h, const char* label, int scale, uint16_t color) {
    // dark rounded outline, body, top gloss, bottom shade
    gfx::fillRect(x + 1, y, w - 2, h, INK);
    gfx::fillRect(x, y + 1, w, h - 2, INK);
    gfx::fillRect(x + 2, y + 2, w - 4, h - 4, color);
    gfx::fillRect(x + 2, y + 2, w - 4, (h - 4) / 3, gfx::tint(color, 60));
    gfx::fillRect(x + 3, y + 2, w - 6, 1, gfx::tint(color, 140));
    gfx::fillRect(x + 2, y + h - 5, w - 4, 3, gfx::shade(color, 170));
    const int tw = gfx::textWidth(label, scale);
    const uint16_t tc = inkOn(color);
    gfx::textShadow(x + (w - tw) / 2, y + (h - 7 * scale) / 2, label, scale, tc, tc == WHITE ? INK : gfx::tint(color, 120));
}

void drawTeamBadge(int cx, int cy, int team, int scale) {
    badge(cx, cy, team, scale < 1 ? 1 : scale > 3 ? 3 : scale, 0, 0);
}

void drawBar(int x, int y, int w, int h, int value, int max, uint16_t color) {
    gfx::fillRect(x, y, w, h, INK);
    gfx::fillRect(x + 1, y + 1, w - 2, h - 2, rgb(40, 42, 56));
    if (max <= 0) return;
    if (value < 0) value = 0;
    if (value > max) value = max;
    const int fill = (w - 2) * value / max;
    if (fill > 0) {
        gfx::fillRect(x + 1, y + 1, fill, h - 2, color);
        gfx::fillRect(x + 1, y + 1, fill, (h - 2) / 3, gfx::tint(color, 70));
    }
    if (max <= 12)                                   // segmented look for ratings
        for (int i = 1; i < max; ++i) gfx::vspan(x + 1 + (w - 2) * i / max, y + 1, y + h - 2, INK);
}

void drawKickMeter(int x, int y, int w, int h, int needle, int window) {
    if (w <= 0) return;
    gfx::fillRect(x - 2, y - 2, w + 4, h + 4, INK);
    const int stepQ = (256 << 16) / w;               // one division per call
    const uint16_t red = rgb(200, 40, 30), amber = rgb(240, 170, 40);
    const uint16_t green = rgb(60, 210, 80), greenHi = rgb(150, 255, 150);
    for (int i = 0; i < w; ++i) {
        const int v = (i * stepQ) >> 16;
        const int d = v < 128 ? 128 - v : v - 128;
        uint16_t c;
        if (d <= window) c = d * 3 <= window ? greenHi : green;
        else c = gfx::mix(amber, red, (d - window) * 2 > 256 ? 256 : (d - window) * 2);
        gfx::vspan(x + i, y, y + h - 1, c);
        if (h > 6) *px(x + i, y + 1) = gfx::tint(c, 90);
    }
    gfx::vspan(x + w / 2, y, y + h - 1, WHITE);
    if (needle < 0) needle = 0;
    if (needle > 255) needle = 255;
    const int nx = x + ((needle * w) >> 8);
    gfx::fillRect(nx - 2, y - 4, 5, h + 8, INK);
    gfx::fillRect(nx - 1, y - 3, 3, h + 6, WHITE);
    gfx::triangleV(nx, y - 3, nx - 5, nx + 5, y - 10, WHITE);
    gfx::triangleV(nx, y + h + 2, nx - 5, nx + 5, y + h + 9, WHITE);
}

}  // namespace render
