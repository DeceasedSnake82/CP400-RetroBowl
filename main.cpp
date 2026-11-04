// ============================================================================
//  Pocket Gridiron - an arcade American-football season for the CP400 (Hollyhock 3)
//
//  Hold the calculator SIDEWAYS (keypad on the right). You play every offensive
//  snap of your team; the opponent's drives are simulated. Win games, earn coach
//  points, upgrade your team and win the championship.
//
//  game.cpp    rules: plays, match flow, season (fixed point, no SDK)
//  render.cpp  game state -> pixels      gfx.cpp  rotated framebuffer + font
//  save.cpp    settings / season file    main.cpp menus, input, timing, loop
// ============================================================================

#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <stdint.h>

#include "game.h"
#include "gfx.h"
#include "render.h"
#include "save.h"

APP_NAME("Pocket Gridiron")
APP_DESCRIPTION("Arcade football season: pass, run, kick and upgrade your team. Hold the calculator sideways.")
APP_AUTHOR("DeceasedSnake82")
APP_VERSION("1.0.0")

using gfx::rgb;
using namespace pg;

#ifdef PG_HOST
extern uint8_t host_rtc();
#endif

namespace {

// ---------------------------------------------------------------------------
// Timing: the SDK has no timer. FRAME mode advances `pace` ticks (1/120 s) per frame;
// RTC mode (opt-in) reads the SH7305 128 Hz counter R64CNT, like Pulse Dash.
// ---------------------------------------------------------------------------
uint8_t rtcRead() {
#ifdef PG_HOST
    return host_rtc() & 0x7F;
#else
    volatile uint8_t* const r64 = (volatile uint8_t*)0xA413FEC0;
    uint8_t a, b;
    do { a = *r64; b = *r64; } while (a != b);
    return a & 0x7F;
#endif
}

constexpr int MAX_TICKS = 16;              // per frame (below ~8 fps the game slows down)

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
constexpr uint16_t C_WHITE = rgb(255, 255, 255), C_BLACK = rgb(0, 0, 0);
constexpr uint16_t C_GOLD = rgb(255, 210, 70), C_DIM = rgb(180, 190, 210), C_RED = rgb(230, 70, 60);
constexpr uint16_t C_GREEN = rgb(40, 160, 70), C_BLUE = rgb(40, 110, 190), C_PURPLE = rgb(110, 80, 170);
constexpr uint16_t C_BROWN = rgb(150, 100, 40), C_GREY = rgb(80, 84, 100), C_DARKRED = rgb(150, 50, 50);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum Screen { SC_TITLE, SC_PICK, SC_HUB, SC_UPGRADE, SC_TABLE, SC_MATCH, SC_SETTINGS, SC_HELP };
enum MState { MS_CALL, MS_PLAY, MS_KICK, MS_KICKED, MS_OPP, MS_HALF, MS_FINAL };

save::Data data;
bool saveOk = true;
Screen screen = SC_TITLE;
bool quit;
char msg[48];
int msgTimer;

int pickSel;                               // team picker cursor
bool pickTapped;                           // a card was tapped once (a second tap starts)
MState ms;
bool paused;
Play play;                                 // the live play (or the pre-snap preview)
fx camX;
bool kickXp;                               // kick meter: extra point (else field goal)
int needle, needleDir, kickedT;
int earned;                                // coach points from the last game

// input (play)
int8_t pendThrow = -1;
bool pendJuke;
bool touchDown;
int touchX, touchY;

// timing
uint8_t rtcLast;
uint32_t rtcAcc;
int rtcIdle;
int fps = -1, fpsFrames, fpsTicks;

Match& M() { return data.match; }
Season& S() { return data.season; }

void setMsg(const char* s) { gfx::putStr(msg, s); msgTimer = 90; }
void persist() { saveOk = save::store(data); }

inline bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
bool keyDown(uint16_t sc) { return Input_GetKeyStateU(&sc); }

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
char* putRecord(char* d, int w, int l, int t) {
    d = gfx::putInt(d, w); *d++ = '-'; d = gfx::putInt(d, l);
    if (t) { *d++ = '-'; d = gfx::putInt(d, t); }
    *d = 0;
    return d;
}

char* putClock(char* d, int secs) {
    d = gfx::putInt(d, secs / 60);
    *d++ = ':';
    *d++ = (char)('0' + (secs % 60) / 10);
    *d++ = (char)('0' + secs % 10);
    *d = 0;
    return d;
}

const char* stageName(int st) {
    switch (st) {
        case SS_SEMI: return "SEMI-FINAL";
        case SS_FINAL: return "CHAMPIONSHIP GAME";
        case SS_CHAMPION: return "CHAMPIONS!";
        case SS_OUT: return "SEASON OVER";
        default: return "REGULAR SEASON";
    }
}

void centered(int y, const char* s, int scale, uint16_t c) {
    gfx::textShadow((gfx::W - gfx::textWidth(s, scale)) / 2, y, s, scale, c, C_BLACK);
}

// ---------------------------------------------------------------------------
// Match flow
// ---------------------------------------------------------------------------
int diffDelta() { return (int)data.set.difficulty - 1; }   // -1 easy, 0 normal, +1 hard

void preview() {
    // pre-snap picture: the formation at the current spot (does not touch the match RNG)
    setupPlay(play, M().ballX, M().ballY, M().firstDown, CALL_QUICK, M().r, 1);
    camX = render::cameraFor(cameraFocus(play));
}

void enterNext() {
    paused = false;
    switch (M().next) {
        case NX_CALL: ms = MS_CALL; preview(); break;
        case NX_XP: ms = MS_KICK; kickXp = true; needle = 0; needleDir = 1; preview(); break;
        case NX_OPP: oppDrive(M()); ms = MS_OPP; break;
        case NX_HALF: ms = MS_HALF; break;
        default: ms = MS_FINAL; break;
    }
}

void startMatch() {
    Season& s = S();
    const int opp = opponentOf(s);
    if (opp < 0) return;
    Ratings r = userRatings(s, opp);
    const int dd = diffDelta();
    r.def = (uint8_t)(r.def + dd * 2 < 1 ? 1 : r.def + dd * 2 > 10 ? 10 : r.def + dd * 2);
    matchInit(M(), s.team, opp, r, oppOffense(s, opp) + dd * 2, s.level[U_DEF], rnd(s.rng));
    M().playoff = (s.stage == SS_SEMI || s.stage == SS_FINAL) ? 1 : 0;
    data.inMatch = 1;
    screen = SC_MATCH;
    enterNext();
}

void resumeMatch() {
    // a saved match always resumes between plays (a pending opponent drive is simulated now)
    screen = SC_MATCH;
    enterNext();
}

void callPlay(PlayCall c) {
    snap(M(), play, c);
    camX = render::cameraFor(cameraFocus(play));
    pendThrow = -1; pendJuke = false; touchDown = false;
    ms = MS_PLAY;
    if (data.set.timer) rtcLast = rtcRead();
}

void finishMatch() {
    Match& m = M();
    earned = seasonRecord(S(), m.score[0], m.score[1], m.tds);
    data.inMatch = 0;
    persist();
    screen = SC_HUB;
    char b[40];
    gfx::putStr(gfx::putInt(gfx::putStr(b, "+"), earned), " COACH POINTS");
    setMsg(b);
}

void exitMatch() {
    // save and leave; an unfinished play is discarded (the match resumes at the play call)
    if (ms == MS_FINAL) { finishMatch(); return; }
    data.inMatch = 1;
    persist();
    screen = SC_HUB;
}

// ---------------------------------------------------------------------------
// Ticks
// ---------------------------------------------------------------------------
int ticksThisFrame() {
    if (!data.set.timer) return data.set.pace;
    const uint8_t now = rtcRead();
    const uint8_t d = (uint8_t)((now - rtcLast) & 0x7F);
    rtcLast = now;
    fpsTicks += d;
    ++fpsFrames;
    if (fpsTicks >= 128) { fps = fpsFrames * 128 / fpsTicks; fpsFrames = 0; fpsTicks = 0; }
    rtcAcc += (uint32_t)d * TICK_HZ;            // 128 Hz -> 120 Hz ticks
    int n = (int)(rtcAcc >> 7);
    rtcAcc &= 127;
    if (n == 0) {
        if (++rtcIdle > 200) { data.set.timer = 0; setMsg("RTC STOPPED - FRAME TIMER"); }
    } else rtcIdle = 0;
    return n > MAX_TICKS ? MAX_TICKS : n;
}

// Steering from the arrow keys. The screen is landscape: physical UP points left,
// DOWN right, RIGHT up, LEFT down.
void readSteer(Input& in) {
    in.mx = (int8_t)((keyDown(ScancodeDown) ? 1 : 0) - (keyDown(ScancodeUp) ? 1 : 0));
    in.my = (int8_t)((keyDown(ScancodeLeft) ? 1 : 0) - (keyDown(ScancodeRight) ? 1 : 0));
    if (touchDown && !in.mx && !in.my) {
        in.steer = true;
        in.sx = render::wx(touchX, camX);
        in.sy = render::wy(touchY);
    }
}

void playFrame() {
    Input in = Input();
    in.throwTo = pendThrow;
    in.juke = pendJuke;
    readSteer(in);
    pendThrow = -1;
    pendJuke = false;
    const int n = ticksThisFrame();
    for (int i = 0; i < n; ++i) {
        step(play, in);
        in.throwTo = -1;
        in.juke = false;
        if (play.phase == PH_OVER && play.overT >= sec(1.4)) {
            applyPlay(M(), play);
            enterNext();
            return;
        }
    }
    camX = render::followCamera(camX, cameraFocus(play));
}

void kickFrame() {
    const int n = ticksThisFrame();
    if (ms == MS_KICKED) {
        kickedT += n;
        if (kickedT >= sec(1.5)) enterNext();
        return;
    }
    // the needle sweeps 0..255 and back; faster for long field goals
    const int speed = kickXp ? 3 : 3 + fgDistance(M()) / 18;
    for (int i = 0; i < n; ++i) {
        needle += needleDir * speed;
        if (needle >= 255) { needle = 255; needleDir = -1; }
        if (needle <= 0) { needle = 0; needleDir = 1; }
    }
}

void kickNow() {
    if (ms != MS_KICK) return;
    if (kickXp) kickXP(M(), needle); else kickFG(M(), needle);
    ms = MS_KICKED;
    kickedT = 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
// Title / menus share this button row height
constexpr int BTN_H = 44;

void drawFooterMsg() {
    if (msgTimer > 0 || !saveOk) {
        const char* m = msgTimer > 0 ? msg : "SEASON NOT SAVED (FILE ERROR)";
        gfx::fillRect(0, gfx::H - 14, gfx::W, 14, C_BLACK);
        gfx::textCentered(gfx::W / 2, gfx::H - 11, m, 1, C_GOLD);
    }
}

void drawTitle() {
    render::drawMenuBackground(1);
    centered(26, "POCKET", 3, C_GOLD);
    centered(52, "GRIDIRON", 5, C_WHITE);
    gfx::textCentered(gfx::W / 2, 98, "ARCADE FOOTBALL FOR THE CP400", 1, C_DIM);
    if (data.hasSeason) {
        render::drawTeamBadge(gfx::W / 2, 140, S().team, 2);
        char b[48];
        char* p = gfx::putStr(b, TEAMS[S().team].name);
        p = gfx::putStr(p, "  YEAR ");
        gfx::putInt(p, S().year);
        gfx::textCentered(gfx::W / 2, 172, b, 2, C_WHITE);
    }
    const int y = 200;
    render::drawButton(30, y, 228, BTN_H, data.hasSeason ? "CONTINUE" : "NEW SEASON", 2, C_GREEN);
    render::drawButton(270, y, 228, BTN_H, data.hasSeason ? "NEW FRANCHISE" : "HOW TO PLAY", 2, data.hasSeason ? C_DARKRED : C_BROWN);
    render::drawButton(30, y + 54, 150, 40, "SETUP", 2, C_PURPLE);
    render::drawButton(189, y + 54, 150, 40, "HELP", 2, C_BROWN);
    render::drawButton(348, y + 54, 150, 40, "QUIT", 2, C_GREY);
    gfx::text(8, gfx::H - 10, "EXE START  1 NEW  = SETUP  . HELP  CLEAR QUIT", 1, C_DIM);
    drawFooterMsg();
}

// team picker: 6 x 2 grid
constexpr int PK_X = 12, PK_Y = 44, PK_W = 82, PK_H = 92, PK_GAP = 2;

void drawPick() {
    render::drawMenuBackground(2);
    centered(10, "CHOOSE YOUR TEAM", 3, C_WHITE);
    for (int i = 0; i < NTEAMS; ++i) {
        const int cx = PK_X + (i % 6) * (PK_W + PK_GAP), cy = PK_Y + (i / 6) * (PK_H + PK_GAP);
        render::drawPanel(cx, cy, PK_W, PK_H, i == pickSel ? C_GOLD : C_GREY);
        render::drawTeamBadge(cx + PK_W / 2, cy + 34, i, 2);
        gfx::textCentered(cx + PK_W / 2, cy + 70, TEAMS[i].abbr, 2, i == pickSel ? C_GOLD : C_WHITE);
    }
    const TeamDef& t = TEAMS[pickSel];
    char b[48];
    char* p = gfx::putStr(b, t.city);
    *p++ = ' ';
    gfx::putStr(p, t.name);
    centered(240, b, 2, C_GOLD);
    p = gfx::putStr(b, "OFFENSE ");
    p = gfx::putInt(p, t.off);
    p = gfx::putStr(p, "   DEFENSE ");
    gfx::putInt(p, t.def);
    gfx::textCentered(gfx::W / 2, 262, b, 1, C_DIM);
    gfx::textCentered(gfx::W / 2, 274, "(YOUR STARTING SQUAD IS THE SAME FOR EVERY TEAM)", 1, C_DIM);
    render::drawButton(140, 284, 248, 32, "START SEASON", 2, C_GREEN);
    gfx::text(8, 300, "ARROWS", 1, C_DIM);
    gfx::text(8, 310, "EXE OK", 1, C_DIM);
}

void drawHub() {
    const Season& s = S();
    render::drawMenuBackground(3 + s.week);
    char b[64];
    char* p = gfx::putStr(b, "YEAR ");
    p = gfx::putInt(p, s.year);
    p = gfx::putStr(p, "  ");
    if (s.stage == SS_REGULAR) { p = gfx::putStr(p, "WEEK "); p = gfx::putInt(p, s.week + 1); p = gfx::putStr(p, " OF "); gfx::putInt(p, NWEEKS); }
    else gfx::putStr(p, stageName(s.stage));
    centered(8, b, 2, C_WHITE);

    // your team
    render::drawPanel(10, 30, 250, 200, TEAMS[s.team].c1);
    render::drawTeamBadge(56, 82, s.team, 3);
    gfx::text(100, 50, TEAMS[s.team].city, 1, C_DIM);
    gfx::text(100, 62, TEAMS[s.team].name, 2, C_WHITE);
    putRecord(b, s.w[s.team], s.l[s.team], s.t[s.team]);
    gfx::text(100, 86, b, 3, C_GOLD);
    p = gfx::putStr(b, "COACH POINTS ");
    gfx::putInt(p, s.points);
    gfx::text(22, 130, b, 2, C_WHITE);
    p = gfx::putStr(b, "TITLES ");
    gfx::putInt(p, s.titles);
    gfx::text(22, 152, b, 2, C_WHITE);
    static const char* const UP[U_COUNT] = { "QB", "RB", "WR", "OL", "DEF" };
    for (int i = 0; i < U_COUNT; ++i) {
        const int x = 22 + i * 47;
        gfx::text(x, 178, UP[i], 1, C_DIM);
        render::drawBar(x, 190, 40, 8, s.level[i], MAX_LEVEL, C_GREEN);
    }
    if (s.lastStage != SS_REGULAR || s.week > 0 || s.stage != SS_REGULAR) {
        p = gfx::putStr(b, "LAST: ");
        *p++ = s.lastUs > s.lastThem ? 'W' : s.lastUs < s.lastThem ? 'L' : 'T';
        *p++ = ' ';
        p = gfx::putInt(p, s.lastUs);
        *p++ = '-';
        p = gfx::putInt(p, s.lastThem);
        p = gfx::putStr(p, " VS ");
        gfx::putStr(p, TEAMS[s.lastOpp].abbr);
        if (s.week > 0 || s.lastStage != SS_REGULAR) gfx::text(22, 210, b, 1, C_DIM);
    }

    // next opponent
    const int opp = opponentOf(s);
    render::drawPanel(268, 30, 250, 200, opp >= 0 ? TEAMS[opp].c1 : C_GOLD);
    if (opp >= 0) {
        gfx::textCentered(393, 38, data.inMatch ? "GAME IN PROGRESS" : "NEXT GAME", 1, C_DIM);
        render::drawTeamBadge(393, 84, opp, 2);
        gfx::textCentered(393, 124, TEAMS[opp].city, 1, C_DIM);
        gfx::textCentered(393, 136, TEAMS[opp].name, 2, C_WHITE);
        putRecord(b, s.w[opp], s.l[opp], s.t[opp]);
        gfx::textCentered(393, 158, b, 2, C_GOLD);
        p = gfx::putStr(b, "OFF ");
        p = gfx::putInt(p, oppOffense(s, opp));
        p = gfx::putStr(p, "  DEF ");
        gfx::putInt(p, oppDefense(s, opp));
        gfx::textCentered(393, 182, b, 2, C_DIM);
        if (data.inMatch) {
            p = gfx::putInt(b, M().score[0]);
            p = gfx::putStr(p, " - ");
            gfx::putInt(p, M().score[1]);
            gfx::textCentered(393, 206, b, 2, C_WHITE);
        } else if (s.stage != SS_REGULAR) gfx::textCentered(393, 206, stageName(s.stage), 1, C_GOLD);
    } else {
        centered(70, s.stage == SS_CHAMPION ? "CHAMPIONS!" : "SEASON OVER", 3, C_GOLD);
        if (s.stage == SS_CHAMPION) render::drawTeamBadge(393, 140, s.team, 3);
        else gfx::textCentered(393, 130, "BETTER LUCK NEXT YEAR", 1, C_DIM);
        gfx::textCentered(393, 190, "PLAY ON FOR ANOTHER SEASON", 1, C_DIM);
    }

    const char* playLabel = opp < 0 ? "NEXT SEASON" : data.inMatch ? "RESUME" : "PLAY";
    render::drawButton(10, 240, 170, BTN_H, playLabel, 2, C_GREEN);
    render::drawButton(188, 240, 110, BTN_H, "TEAM", 2, C_BLUE);
    render::drawButton(306, 240, 110, BTN_H, "TABLE", 2, C_PURPLE);
    render::drawButton(424, 240, 94, BTN_H, "MENU", 2, C_GREY);
    gfx::text(10, 292, "EXE PLAY   1 TEAM UPGRADES   2 STANDINGS   CLEAR MENU", 1, C_DIM);
    drawFooterMsg();
}

constexpr int UP_Y0 = 46, UP_ROW = 46;

void drawUpgrade() {
    const Season& s = S();
    render::drawMenuBackground(4);
    centered(8, "TEAM UPGRADES", 3, C_WHITE);
    char b[48];
    char* p = gfx::putStr(b, "COACH POINTS: ");
    gfx::putInt(p, s.points);
    gfx::textCentered(gfx::W / 2, 34, b, 1, C_GOLD);
    static const char* const NAME[U_COUNT] = { "QUARTERBACK", "RUNNING BACK", "RECEIVERS", "O-LINE", "DEFENSE" };
    static const char* const INFO[U_COUNT] = { "ACCURACY + ARM STRENGTH", "SPEED + BREAK TACKLES", "SPEED + SURE HANDS",
                                               "TIME IN THE POCKET", "STOPS OPPONENT DRIVES" };
    for (int i = 0; i < U_COUNT; ++i) {
        const int y = UP_Y0 + i * UP_ROW;
        render::drawPanel(14, y, 500, UP_ROW - 4, C_GREY);
        b[0] = (char)('1' + i); b[1] = 0;
        gfx::text(24, y + 14, b, 2, C_GOLD);
        gfx::text(48, y + 8, NAME[i], 2, C_WHITE);
        gfx::text(48, y + 28, INFO[i], 1, C_DIM);
        render::drawBar(236, y + 12, 150, 16, s.level[i], MAX_LEVEL, C_GREEN);
        p = gfx::putInt(b, s.level[i]);
        gfx::text(392, y + 14, b, 2, C_WHITE);
        const int c = upgradeCost(s, i);
        if (c == 0) gfx::text(432, y + 14, "MAX", 2, C_GOLD);
        else {
            p = gfx::putStr(b, "+ ");
            gfx::putInt(p, c);
            render::drawButton(428, y + 4, 80, UP_ROW - 12, b, 2, s.points >= c ? C_GREEN : C_GREY);
        }
    }
    render::drawButton(14, 278, 120, 36, "BACK", 2, C_BLUE);
    gfx::text(150, 290, "1-5 BUY  (COST = LEVEL + 1)   CLEAR BACK", 1, C_DIM);
    drawFooterMsg();
}

void drawTable() {
    const Season& s = S();
    render::drawMenuBackground(5);
    render::drawPanel(24, 20, 480, 268, C_PURPLE);
    centered(6, "STANDINGS", 2, C_WHITE);
    uint8_t ord[NTEAMS];
    standings(s, ord);
    const int x0 = 40;
    gfx::text(x0, 26, "  TEAM                  W  L  T   PF   PA", 1, C_DIM);
    for (int r = 0; r < NTEAMS; ++r) {
        const int t = ord[r], y = 38 + r * 20;
        if (t == s.team) gfx::fillRect(x0 - 6, y - 3, 460, 18, rgb(50, 60, 90));
        if (r == 4) gfx::hspan(x0 - 6, x0 + 454, y - 4, C_GOLD);      // playoff line
        char b[48];
        gfx::putInt(b, r + 1);
        gfx::text(x0 - 4, y, b, 1, r < 4 ? C_GOLD : C_DIM);
        gfx::fillRect(x0 + 12, y, 10, 10, TEAMS[t].c1);
        gfx::text(x0 + 28, y + 1, TEAMS[t].abbr, 1, C_WHITE);
        gfx::text(x0 + 54, y + 1, TEAMS[t].name, 1, t == s.team ? C_GOLD : C_WHITE);
        const int vals[5] = { s.w[t], s.l[t], s.t[t], s.pf[t], s.pa[t] };
        const int col[5] = { 150, 168, 186, 216, 246 };
        for (int c = 0; c < 5; ++c) {
            gfx::putInt(b, vals[c]);
            gfx::text(x0 + col[c] + (c >= 3 ? 18 - gfx::textWidth(b, 1) : 0), y + 1, b, 1, C_WHITE);
        }
    }
    gfx::text(x0 + 300, 50, "TOP 4 MAKE", 1, C_DIM);
    gfx::text(x0 + 300, 62, "THE PLAYOFFS", 1, C_DIM);
    render::drawButton(380, 270, 130, 40, "BACK", 2, C_BLUE);
}

void drawSettings() {
    render::drawMenuBackground(6);
    render::drawPanel(24, 40, 480, 228, C_PURPLE);
    centered(10, "SETUP", 3, C_WHITE);
    char b[48];
    int y = 50;
    gfx::text(40, y + 8, "TICKS PER FRAME", 2, C_WHITE);
    render::drawButton(320, y, 50, 40, "-", 3, C_DARKRED);
    gfx::putInt(b, data.set.pace);
    gfx::textCentered(400, y + 10, b, 3, C_GOLD);
    render::drawButton(430, y, 50, 40, "+", 3, C_GREEN);
    gfx::text(40, y + 30, "HIGHER = FASTER GAME. 4 = 30 FPS (FRAME TIMER)", 1, C_DIM);
    y += 56;
    gfx::text(40, y + 8, "TIMER", 2, C_WHITE);
    render::drawButton(320, y, 160, 40, data.set.timer ? "RTC" : "FRAME", 2, data.set.timer ? rgb(40, 140, 160) : C_GREY);
    gfx::text(40, y + 30, "RTC = REAL-TIME SPEED (EXPERIMENTAL)", 1, C_DIM);
    y += 56;
    gfx::text(40, y + 8, "DIFFICULTY", 2, C_WHITE);
    static const char* const DIFF[3] = { "EASY", "NORMAL", "HARD" };
    render::drawButton(320, y, 160, 40, DIFF[data.set.difficulty > 2 ? 1 : data.set.difficulty], 2, C_PURPLE);
    y += 56;
    gfx::text(40, y + 8, "SHOW FPS", 2, C_WHITE);
    render::drawButton(320, y, 160, 40, data.set.showFps ? "ON" : "OFF", 2, C_GREY);
    gfx::text(40, y + 30, "NEEDS THE RTC TIMER", 1, C_DIM);
    render::drawButton(40, 278, 140, 36, "BACK", 2, C_GREEN);
    gfx::text(196, 284, "KEYS: + -  1 TIMER  2 DIFFICULTY  3 FPS", 1, C_DIM);
    gfx::text(196, 296, "EXE / CLEAR: BACK", 1, C_DIM);
    drawFooterMsg();
}

void drawHelp() {
    render::drawMenuBackground(7);
    render::drawPanel(8, 4, 512, 292, C_BROWN);
    static const char* const LINES[] = {
        // scale 2 fits 43 characters per line
        "HOW TO PLAY",
        "HOLD THE CALCULATOR SIDEWAYS, KEYS RIGHT.",
        "PICK A PLAY: 1 QUICK  2 DEEP  3 RUN",
        "4TH DOWN: 4 PUNT  5 FIELD GOAL",
        "THROW: KEYS 1-4 (NUMBERS OVER RECEIVERS)",
        "  OR TAP A RECEIVER",
        "MOVE: ARROW KEYS OR HOLD A FINGER",
        "RUNNERS RUN FORWARD BY THEMSELVES",
        "EXE: JUKE (TACKLERS MISS FOR A MOMENT)",
        "KICK: PRESS EXE IN THE GREEN ZONE",
        "CLEAR: PAUSE / BACK",
    };
    int y = 12, n = 0;
    for (const char* l : LINES) {
        gfx::textShadow(18, y, l, 2, n == 0 ? C_GOLD : C_WHITE, C_BLACK);
        y += n == 0 ? 28 : 24;
        ++n;
    }
    gfx::textCentered(gfx::W / 2, gfx::H - 14, "TAP OR PRESS ANY KEY", 1, C_DIM);
}

// --- match screens ----------------------------------------------------------
const char* resultText(const Play& pl, uint16_t& c) {
    static char b[32];
    c = C_WHITE;
    switch (pl.result) {
        case RES_TD: c = C_GOLD; return "TOUCHDOWN!";
        case RES_INT: c = C_RED; return "INTERCEPTED!";
        case RES_SACK: c = C_RED; return "SACKED!";
        case RES_SAFETY: c = C_RED; return "SAFETY!";
        case RES_INCOMPLETE: return "INCOMPLETE";
        default: break;
    }
    if (pl.endX >= pl.firstDown && pl.firstDown < GOAL_OPP) { c = C_GOLD; return "FIRST DOWN!"; }
    const int y = playYards(pl);
    if (pl.result == RES_OOB && y == 0) return "OUT OF BOUNDS";
    if (y == 0) return "NO GAIN";
    char* p = b;
    if (y > 0) *p++ = '+';
    p = gfx::putInt(p, y);
    gfx::putStr(p, (y == 1 || y == -1) ? " YARD" : " YARDS");
    return b;
}

const char* eventText(int e) {
    switch (e) {
        case EV_CATCH: return "CAUGHT!";
        case EV_DROP: return "DROPPED!";
        case EV_DEFLECT: return "KNOCKED AWAY!";
        case EV_JUKE: return "JUKE!";
        case EV_BROKEN: return "BROKEN TACKLE!";
        case EV_HANDOFF: return "HANDOFF";
        case EV_PICK: return "PICKED OFF!";
        default: return nullptr;
    }
}

// Play-call panel at the bottom
constexpr int CALL_Y = 250, CALL_H = 50;

void drawCallPanel() {
    const Match& m = M();
    const bool fourth = m.down == 4;
    const int n = fourth ? 5 : 3;
    const int w = fourth ? 98 : 164, gap = 6;
    const int x0 = (gfx::W - (n * w + (n - 1) * gap)) / 2;
    static const char* const L[5] = { "1 QUICK", "2 DEEP", "3 RUN", "4 PUNT", "5 FG" };
    static const uint16_t C[5] = { C_GREEN, C_BLUE, C_BROWN, C_GREY, C_PURPLE };
    for (int i = 0; i < n; ++i) {
        const bool ok = i != 4 || fgAllowed(m);
        render::drawButton(x0 + i * (w + gap), CALL_Y, w, CALL_H, L[i], 2, ok ? C[i] : rgb(50, 50, 60));
    }
    if (fourth && fgAllowed(m)) {
        char b[24];
        gfx::putStr(gfx::putInt(gfx::putStr(b, "FG: "), fgDistance(m)), " YDS");
        gfx::textShadow(x0 + 4 * (w + gap) + 6, CALL_Y - 12, b, 1, C_WHITE, C_BLACK);
    }
}

void drawPauseButton() {
    gfx::fillRect(gfx::W - 22, 2, 20, 18, rgb(40, 44, 60));
    gfx::fillRect(gfx::W - 16, 5, 3, 12, C_WHITE);
    gfx::fillRect(gfx::W - 10, 5, 3, 12, C_WHITE);
}

void drawMatchPlay() {
    render::drawPlay(play, M(), camX);
    render::drawScoreboard(M());
    drawPauseButton();
    if (play.phase == PH_OVER) {
        uint16_t c;
        const char* t = resultText(play, c);
        render::drawBanner(t, c);
    }
    const char* e = eventText(play.event);
    if (e && play.eventT > 0) render::drawSubBanner(e, C_WHITE);
    if (fps >= 0 && data.set.showFps && data.set.timer) {
        char b[12];
        gfx::putStr(gfx::putInt(b, fps), " FPS");
        gfx::textShadow(4, gfx::H - 12, b, 1, C_GOLD, C_BLACK);
    }
}

void drawMatchCall() {
    render::drawPlay(play, M(), camX);
    render::drawScoreboard(M());
    drawPauseButton();
    drawCallPanel();
    if (msgTimer > 0) render::drawSubBanner(msg, C_GOLD);
    else if (M().last.firstDown && M().down == 1) render::drawSubBanner("FIRST DOWN", C_GOLD);
}

void drawMatchKick() {
    const Match& m = M();
    render::drawPlay(play, m, camX);
    render::drawScoreboard(m);
    const int d = kickXp ? 20 : fgDistance(m);
    char b[32];
    if (kickXp) gfx::putStr(b, "EXTRA POINT");
    else gfx::putStr(gfx::putInt(gfx::putStr(b, "FIELD GOAL "), d), " YDS");
    render::drawPanel(104, 214, 320, 92, C_GOLD);
    gfx::textCentered(gfx::W / 2, 224, b, 2, C_WHITE);
    render::drawKickMeter(124, 248, 280, 26, needle, kickWindow(d));
    if (ms == MS_KICK) gfx::textCentered(gfx::W / 2, 284, "PRESS EXE OR TAP IN THE GREEN ZONE", 1, C_DIM);
    else {
        const bool good = m.last.kind == K_FG_GOOD || m.last.kind == K_XP_GOOD;
        render::drawBanner(good ? "IT'S GOOD!" : "NO GOOD", good ? C_GOLD : C_RED);
    }
}

void drawScoreLine(int y) {
    const Match& m = M();
    char b[48];
    char* p = gfx::putStr(b, TEAMS[m.user].abbr);
    *p++ = ' ';
    p = gfx::putInt(p, m.score[0]);
    p = gfx::putStr(p, "  -  ");
    p = gfx::putInt(p, m.score[1]);
    *p++ = ' ';
    gfx::putStr(p, TEAMS[m.opp].abbr);
    centered(y, b, 3, C_WHITE);
}

const char* lastText(const Match& m) {
    if (m.last.kind == K_PUNT) return "YOU PUNTED";
    if (m.last.kind == K_FG_GOOD) return "FIELD GOAL GOOD";
    if (m.last.kind == K_FG_MISS) return "FIELD GOAL MISSED";
    if (m.last.kind == K_XP_GOOD || m.last.kind == K_XP_MISS || m.last.result == RES_TD) return "TOUCHDOWN - KICKOFF";
    if (m.last.result == RES_INT) return "INTERCEPTION";
    if (m.last.result == RES_SAFETY) return "SAFETY - FREE KICK";
    if (m.last.turnover) return "TURNOVER ON DOWNS";
    // m.last is cleared at the start of a match and of the second half: that drive follows a kickoff
    if (m.last.kind == K_PLAY && m.last.result == RES_NONE) return m.quarter >= 3 ? "SECOND-HALF KICKOFF" : "OPENING KICKOFF";
    return "KICKOFF";
}

void drawMatchOpp() {
    const Match& m = M();
    render::drawMenuBackground(8 + m.quarter);
    render::drawScoreboard(m);
    render::drawPanel(64, 36, 400, 220, TEAMS[m.opp].c1);
    gfx::textCentered(gfx::W / 2, 48, lastText(m), 1, C_DIM);
    char b[48];
    char* p = gfx::putStr(b, TEAMS[m.opp].name);
    gfx::putStr(p, " DRIVE");
    centered(64, b, 2, C_WHITE);
    render::drawTeamBadge(gfx::W / 2, 112, m.opp, 2);
    const DriveReport& r = m.drive;
    p = gfx::putInt(b, r.plays);
    p = gfx::putStr(p, " PLAYS  ");
    p = gfx::putInt(p, r.yards);
    p = gfx::putStr(p, " YDS  ");
    putClock(p, r.secs);
    gfx::textCentered(gfx::W / 2, 146, b, 2, C_DIM);
    static const char* const RES[] = { "TOUCHDOWN", "FIELD GOAL", "PUNT", "TURNOVER ON DOWNS", "FUMBLE - YOUR BALL", "MISSED FIELD GOAL", "END OF HALF", "SAFETY" };
    const bool bad = r.result == DR_TD || r.result == DR_FG;
    centered(170, RES[r.result < 8 ? r.result : 2], 3, bad ? C_RED : C_GOLD);
    drawScoreLine(210);
    gfx::textCentered(gfx::W / 2, 268, "TAP OR PRESS EXE", 1, C_DIM);
}

void drawMatchHalf() {
    render::drawMenuBackground(9);
    render::drawScoreboard(M());
    centered(70, "HALFTIME", 4, C_GOLD);
    drawScoreLine(130);
    gfx::textCentered(gfx::W / 2, 190, M().userSecondHalf ? "YOU RECEIVE THE SECOND-HALF KICKOFF" : "THEY RECEIVE THE SECOND-HALF KICKOFF", 1, C_DIM);
    gfx::textCentered(gfx::W / 2, 268, "TAP OR PRESS EXE", 1, C_DIM);
}

void drawMatchFinal() {
    const Match& m = M();
    render::drawMenuBackground(10);
    const bool win = m.score[0] > m.score[1], tie = m.score[0] == m.score[1];
    centered(18, "FINAL", 2, C_DIM);
    centered(40, win ? "YOU WIN!" : tie ? "TIE GAME" : "YOU LOSE", 4, win ? C_GOLD : tie ? C_WHITE : C_RED);
    render::drawTeamBadge(110, 110, m.user, 3);
    render::drawTeamBadge(gfx::W - 110, 110, m.opp, 3);
    drawScoreLine(98);
    render::drawPanel(104, 150, 320, 118, C_GREY);
    char b[48];
    char* p = gfx::putStr(b, "PASSING ");
    p = gfx::putInt(p, m.passComp);
    *p++ = '/';
    p = gfx::putInt(p, m.passAtt);
    p = gfx::putStr(p, "  ");
    p = gfx::putInt(p, m.passYds);
    gfx::putStr(p, " YDS");
    gfx::text(124, 162, b, 2, C_WHITE);
    p = gfx::putStr(b, "RUSHING ");
    p = gfx::putInt(p, m.rushYds);
    gfx::putStr(p, " YDS");
    gfx::text(124, 184, b, 2, C_WHITE);
    p = gfx::putStr(b, "TOUCHDOWNS ");
    gfx::putInt(p, m.tds);
    gfx::text(124, 206, b, 2, C_WHITE);
    p = gfx::putStr(b, "INT ");
    p = gfx::putInt(p, m.ints);
    p = gfx::putStr(p, "   SACKED ");
    gfx::putInt(p, m.sacks);
    gfx::text(124, 228, b, 2, C_WHITE);
    if (m.quarter > 4) gfx::textCentered(gfx::W / 2, 252, "AFTER OVERTIME", 1, C_DIM);
    gfx::textCentered(gfx::W / 2, 290, "TAP OR PRESS EXE", 1, C_DIM);
}

void drawPause() {
    gfx::blendRect(64, 70, 400, 180, C_BLACK, 190);
    centered(84, "PAUSED", 4, C_WHITE);
    render::drawButton(84, 140, 170, 56, "RESUME", 2, C_GREEN);
    render::drawButton(274, 140, 170, 56, "SAVE & EXIT", 2, C_DARKRED);
    gfx::textCentered(gfx::W / 2, 214, "EXE RESUME   CLEAR SAVE & EXIT", 1, C_DIM);
    gfx::textCentered(gfx::W / 2, 228, "(A PLAY IN PROGRESS IS REPLAYED)", 1, C_DIM);
}

void drawMatch() {
    switch (ms) {
        case MS_CALL: drawMatchCall(); break;
        case MS_PLAY: drawMatchPlay(); break;
        case MS_KICK: case MS_KICKED: drawMatchKick(); break;
        case MS_OPP: drawMatchOpp(); break;
        case MS_HALF: drawMatchHalf(); break;
        case MS_FINAL: drawMatchFinal(); break;
    }
    if (paused) drawPause();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void newFranchise() { pickSel = data.hasSeason ? S().team : 0; pickTapped = false; screen = SC_PICK; }

void startSeason() {
    uint32_t seed = 0x2545F491u ^ ((uint32_t)rtcRead() << 16) ^ (uint32_t)pickSel * 0x9E3779B9u;
    seasonNew(S(), pickSel, seed);
    data.hasSeason = 1;
    data.inMatch = 0;
    persist();
    screen = SC_HUB;
}

void hubPlay() {
    if (opponentOf(S()) < 0) { seasonNextYear(S()); persist(); setMsg("A NEW SEASON BEGINS"); return; }
    if (data.inMatch) resumeMatch(); else startMatch();
}

void buy(int i) {
    if (upgrade(S(), i)) { persist(); setMsg("UPGRADED!"); }
    else setMsg(upgradeCost(S(), i) == 0 ? "ALREADY AT THE MAXIMUM" : "NOT ENOUGH COACH POINTS");
}

void toggleTimer() {
    if (data.set.timer) { data.set.timer = 0; fps = -1; persist(); setMsg("TIMER: FRAME"); return; }
    const uint8_t a = rtcRead();
    bool moved = false;
    for (volatile uint32_t n = 0; n < 4000000u && !moved; n = n + 1) if (rtcRead() != a) moved = true;
    if (!moved) { setMsg("RTC NOT RUNNING - KEPT FRAME TIMER"); return; }
    data.set.timer = 1;
    persist();
    setMsg("TIMER: RTC");
}

void pace(int d) {
    int p = data.set.pace + d;
    data.set.pace = (uint8_t)(p < 1 ? 1 : p > 8 ? 8 : p);
    persist();
}

void callKey(int k) {
    // k = 0..4 (QUICK, DEEP, RUN, PUNT, FG)
    const Match& m = M();
    if (k <= 2) { callPlay((PlayCall)k); return; }
    if (m.down != 4) return;
    if (k == 3) { punt(M()); enterNext(); return; }
    if (k == 4 && fgAllowed(m)) { ms = MS_KICK; kickXp = false; needle = 0; needleDir = 1; }
}

void continueCard() {
    switch (ms) {
        case MS_OPP: enterNext(); break;              // m.next was set by oppDrive
        case MS_HALF: continueMatch(M()); enterNext(); break;
        case MS_FINAL: finishMatch(); break;
        default: break;
    }
}

// --- keys -------------------------------------------------------------------
void matchKey(int code) {
    if (paused) {
        if (code == KEYCODE_EXE) paused = false;
        else if (code == KEYCODE_POWER_CLEAR) exitMatch();
        return;
    }
    if (code == KEYCODE_POWER_CLEAR) {
        if (ms == MS_FINAL) finishMatch(); else paused = true;
        return;
    }
    switch (ms) {
        case MS_CALL:
            if (code >= KEYCODE_1 && code <= KEYCODE_5) callKey(code - KEYCODE_1);
            break;
        case MS_PLAY:
            if (code >= KEYCODE_1 && code <= KEYCODE_4) pendThrow = TARGETS[code - KEYCODE_1];
            else if (code == KEYCODE_EXE || code == KEYCODE_0 || code == KEYCODE_DOT) pendJuke = true;
            break;
        case MS_KICK:
            if (code == KEYCODE_EXE || (code >= KEYCODE_0 && code <= KEYCODE_9)) kickNow();
            break;
        case MS_KICKED: break;
        default:
            if (code == KEYCODE_EXE || code == KEYCODE_0) continueCard();
            break;
    }
}

void handleKey(int code) {
    switch (screen) {
        case SC_TITLE:
            if (code == KEYCODE_POWER_CLEAR) quit = true;
            else if (code == KEYCODE_EXE) { if (data.hasSeason) screen = SC_HUB; else newFranchise(); }
            else if (code == KEYCODE_1) newFranchise();
            else if (code == KEYCODE_EQUALS) screen = SC_SETTINGS;
            else if (code == KEYCODE_DOT) screen = SC_HELP;
            break;
        case SC_PICK: {
            int c = pickSel % 6, r = pickSel / 6;
            if (code == KEYCODE_UP) c = (c + 5) % 6;                 // rotated keys: UP = left
            else if (code == KEYCODE_DOWN) c = (c + 1) % 6;
            else if (code == KEYCODE_RIGHT || code == KEYCODE_LEFT) r ^= 1;
            else if (code == KEYCODE_EXE) { startSeason(); break; }
            else if (code == KEYCODE_POWER_CLEAR) { screen = SC_TITLE; break; }
            pickSel = r * 6 + c;
            break;
        }
        case SC_HUB:
            if (code == KEYCODE_EXE) hubPlay();
            else if (code == KEYCODE_1) screen = SC_UPGRADE;
            else if (code == KEYCODE_2) screen = SC_TABLE;
            else if (code == KEYCODE_POWER_CLEAR) screen = SC_TITLE;
            break;
        case SC_UPGRADE:
            if (code >= KEYCODE_1 && code <= KEYCODE_5) buy(code - KEYCODE_1);
            else if (code == KEYCODE_POWER_CLEAR || code == KEYCODE_EXE) screen = SC_HUB;
            break;
        case SC_TABLE: screen = SC_HUB; break;
        case SC_MATCH: matchKey(code); break;
        case SC_SETTINGS:
            if (code == KEYCODE_PLUS) pace(1);
            else if (code == KEYCODE_MINUS) pace(-1);
            else if (code == KEYCODE_1) toggleTimer();
            else if (code == KEYCODE_2) { data.set.difficulty = (uint8_t)((data.set.difficulty + 1) % 3); persist(); }
            else if (code == KEYCODE_3) { data.set.showFps ^= 1; persist(); }
            else if (code == KEYCODE_EXE || code == KEYCODE_POWER_CLEAR) screen = SC_TITLE;
            break;
        case SC_HELP: screen = SC_TITLE; break;
    }
}

// --- touch ------------------------------------------------------------------
void matchTouch(int dir, int x, int y) {
    if (dir == TOUCH_UP) { touchDown = false; return; }
    if (dir == TOUCH_HOLD_DRAG) { if (touchDown) { touchX = x; touchY = y; } return; }
    if (dir != TOUCH_DOWN) return;
    if (paused) {
        if (inRect(x, y, 84, 140, 170, 56)) paused = false;
        else if (inRect(x, y, 274, 140, 170, 56)) exitMatch();
        return;
    }
    if ((ms == MS_CALL || ms == MS_PLAY) && x >= gfx::W - 30 && y < render::HUD_H) { paused = true; touchDown = false; return; }
    switch (ms) {
        case MS_CALL: {
            const bool fourth = M().down == 4;
            const int n = fourth ? 5 : 3, w = fourth ? 98 : 164, gap = 6;
            const int x0 = (gfx::W - (n * w + (n - 1) * gap)) / 2;
            if (y >= CALL_Y && y < CALL_Y + CALL_H) {
                for (int i = 0; i < n; ++i) if (inRect(x, y, x0 + i * (w + gap), CALL_Y, w, CALL_H)) callKey(i);
            }
            break;
        }
        case MS_PLAY: {
            if (canThrow(play)) {
                // tap close to a receiver: throw to him
                int best = -1, bd = 26 * 26;
                for (int t = 0; t < NTARGETS; ++t) {
                    const Player& p = play.p[TARGETS[t]];
                    const int dx = render::sx(p.x, camX) - x, dy = render::sy(p.y) - 7 - y;
                    if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = TARGETS[t]; }
                }
                if (best >= 0) { pendThrow = (int8_t)best; return; }
            } else if (play.phase == PH_RUN && play.carrier >= 0) {
                // tap on the runner: juke
                const Player& c = play.p[play.carrier];
                const int dx = render::sx(c.x, camX) - x, dy = render::sy(c.y) - 7 - y;
                if (dx * dx + dy * dy < 22 * 22) { pendJuke = true; return; }
            }
            touchDown = true; touchX = x; touchY = y;
            break;
        }
        case MS_KICK: kickNow(); break;
        case MS_KICKED: break;
        default: continueCard(); break;
    }
}

void handleTouch(int dir, int px, int py) {
    if ((unsigned)px >= (unsigned)gfx::PW || (unsigned)py >= (unsigned)gfx::PH) {
        if (dir == TOUCH_UP) touchDown = false;
        return;
    }
    const int x = py, y = gfx::PW - 1 - px;           // panel -> landscape
    if (screen == SC_MATCH) { matchTouch(dir, x, y); return; }
    if (dir != TOUCH_DOWN) return;
    switch (screen) {
        case SC_TITLE:
            if (inRect(x, y, 30, 200, 228, BTN_H)) { if (data.hasSeason) screen = SC_HUB; else newFranchise(); }
            else if (inRect(x, y, 270, 200, 228, BTN_H)) { if (data.hasSeason) newFranchise(); else screen = SC_HELP; }
            else if (inRect(x, y, 30, 254, 150, 40)) screen = SC_SETTINGS;
            else if (inRect(x, y, 189, 254, 150, 40)) screen = SC_HELP;
            else if (inRect(x, y, 348, 254, 150, 40)) quit = true;
            break;
        case SC_PICK:
            if (inRect(x, y, 140, 284, 248, 32)) { startSeason(); break; }
            for (int i = 0; i < NTEAMS; ++i) {
                const int cx = PK_X + (i % 6) * (PK_W + PK_GAP), cy = PK_Y + (i / 6) * (PK_H + PK_GAP);
                if (inRect(x, y, cx, cy, PK_W, PK_H)) { if (pickSel == i && pickTapped) startSeason(); else { pickSel = i; pickTapped = true; } break; }
            }
            break;
        case SC_HUB:
            if (inRect(x, y, 10, 240, 170, BTN_H)) hubPlay();
            else if (inRect(x, y, 188, 240, 110, BTN_H)) screen = SC_UPGRADE;
            else if (inRect(x, y, 306, 240, 110, BTN_H)) screen = SC_TABLE;
            else if (inRect(x, y, 424, 240, 94, BTN_H)) screen = SC_TITLE;
            break;
        case SC_UPGRADE:
            for (int i = 0; i < U_COUNT; ++i) if (inRect(x, y, 428, UP_Y0 + i * UP_ROW + 4, 80, UP_ROW - 12)) buy(i);
            if (inRect(x, y, 14, 278, 120, 36)) screen = SC_HUB;
            break;
        case SC_TABLE: screen = SC_HUB; break;
        case SC_SETTINGS:
            if (inRect(x, y, 320, 50, 50, 40)) pace(-1);
            else if (inRect(x, y, 430, 50, 50, 40)) pace(1);
            else if (inRect(x, y, 320, 106, 160, 40)) toggleTimer();
            else if (inRect(x, y, 320, 162, 160, 40)) { data.set.difficulty = (uint8_t)((data.set.difficulty + 1) % 3); persist(); }
            else if (inRect(x, y, 320, 218, 160, 40)) { data.set.showFps ^= 1; persist(); }
            else if (inRect(x, y, 40, 278, 140, 36)) screen = SC_TITLE;
            break;
        case SC_HELP: screen = SC_TITLE; break;
        default: break;
    }
}

// Returns true if at least one event that can change a static screen (a key press or a touch
// down) was handled; key releases, drags and timer events only update input state.
bool pollEvents(bool wait) {
    bool any = false;
    for (int n = 0; n < 32; ++n) {
        struct Input_Event ev;
        uint8_t* raw = (uint8_t*)&ev;
        for (unsigned i = 0; i < sizeof ev; ++i) raw[i] = 0;
        GetInput(&ev, (n == 0 && wait) ? 0xFFFFFFFF : 0, 0x10);
        if (ev.type == EVENT_KEY) {
            if (ev.data.key.direction == KEY_PRESSED) { handleKey(ev.data.key.keyCode); any = true; }
        } else if (ev.type == EVENT_TOUCH) {
            handleTouch(ev.data.touch_single.direction, ev.data.touch_single.p1_x, ev.data.touch_single.p1_y);
            if (ev.data.touch_single.direction == TOUCH_DOWN) any = true;
        } else if (ev.type == EVENT_NONE) {
            break;
        } else if (wait && n == 0) {
            continue;        // timer / action-bar event: keep waiting for real input
        }
        if (quit) break;
    }
    return any;
}

// Is something moving on screen (then the loop runs freely), or can we sleep until input?
bool animating() {
    if (screen != SC_MATCH || paused) return false;
    return ms == MS_PLAY || ms == MS_KICK || ms == MS_KICKED;
}

void draw() {
    switch (screen) {
        case SC_TITLE: drawTitle(); break;
        case SC_PICK: drawPick(); break;
        case SC_HUB: drawHub(); break;
        case SC_UPGRADE: drawUpgrade(); break;
        case SC_TABLE: drawTable(); break;
        case SC_MATCH: drawMatch(); break;
        case SC_SETTINGS: drawSettings(); break;
        case SC_HELP: drawHelp(); break;
    }
}

}  // namespace

int main() {
    gfx::init();
    render::init();
    if (!save::load(data)) save::defaults(data);
    if (data.set.pace < 1 || data.set.pace > 8) data.set.pace = 4;

    while (!quit) {
        if (animating()) {
            pollEvents(false);
            if (quit) break;
            if (animating()) {
                if (ms == MS_PLAY) playFrame(); else kickFrame();
            }
            draw();
            if (msgTimer > 0) --msgTimer;
            LCD_Refresh();
            continue;
        }
        // Static screens: draw once, then sleep until something happens.
        draw();
        LCD_Refresh();
        msgTimer = 0;
        // sleep until a key press / touch: a key release or a drag must not cost a full redraw
        while (!pollEvents(true) && !quit) {}
        if (animating() && data.set.timer) rtcLast = rtcRead();   // do not count the idle time
    }
    if (screen == SC_MATCH && ms != MS_FINAL) data.inMatch = 1;
    persist();
    return 0;
}
