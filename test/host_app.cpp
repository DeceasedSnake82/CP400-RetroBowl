// Host harness: runs the real app (main.cpp compiled with -DPG_HOST -Dmain=pg_main) against a
// fake 320x528 LCD and scripted input, and writes LANDSCAPE screenshots (528x320, as the player
// sees them) as PPM files (test/ppm2png.py turns them into PNGs).
//
// Script commands (one per line, '#' starts a comment). A "frame" is one pass of the app's
// main loop that polled for input (i.e. one GetInput call that returned EVENT_NONE).
//   key NAME          press a key once (EXE CLEAR UP DOWN LEFT RIGHT PLUS MINUS DOT EQUALS EXP
//                     0..9 TIMES DIVIDE POWER COMMA NEGATIVE X Y Z SHIFT BACKSPACE KEYBOARD
//                     LPAREN RPAREN)
//   tap X Y           touch down + up at LOGICAL (landscape) coordinates
//   down X Y / up     touch down only / release
//   holdkey NAME N    hold a key for N frames: Input_GetKeyState reports it down, and one
//                     press event is sent on the first frame
//   hold N            = holdkey EXE N
//   frames N          let the app run N frames without input
//   shot NAME         save the screen as out/NAME.ppm
//   time NAME         print the time per frame since the last "frames" / "holdkey"
// When the script ends the harness keeps sending CLEAR so the app leaves every screen and
// quits. A run longer than MAX_FRAMES frames is aborted with an error.
#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#undef main
extern int pg_main();

static constexpr long MAX_FRAMES = 200000;

static uint16_t g_vram[320 * 528];
static uint16_t g_lcd[320 * 528];
static long g_refreshes = 0, g_frames = 0;
static uint8_t g_rtc = 0;

static void tooLong() {
    fprintf(stderr, "host_app: the app ran more than %ld frames (stuck, or does not quit on CLEAR)\n", MAX_FRAMES);
    exit(2);
}

static void host_refresh() {
    memcpy(g_lcd, g_vram, sizeof g_lcd);
    g_rtc = (uint8_t)(g_rtc + 4);       // 4 ticks of 1/128 s per refresh = 32 fps
    if (++g_refreshes > MAX_FRAMES * 4) tooLong();
}
static uint16_t* host_vram() { return g_vram; }
uint8_t host_rtc() { return g_rtc; }

static void shot(const char* name) {
    char path[256];
    snprintf(path, sizeof path, "out/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n528 320\n255\n");
    for (int ly = 0; ly < 320; ++ly)
        for (int lx = 0; lx < 528; ++lx) {
            const uint16_t c = g_lcd[lx * 320 + (319 - ly)];
            const uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

struct Key { const char* name; int code; uint16_t scan; };
static const Key KEYS[] = {
    {"EXE", KEYCODE_EXE, ScancodeEXE}, {"CLEAR", KEYCODE_POWER_CLEAR, ScancodeClear},
    {"UP", KEYCODE_UP, ScancodeUp}, {"DOWN", KEYCODE_DOWN, ScancodeDown},
    {"LEFT", KEYCODE_LEFT, ScancodeLeft}, {"RIGHT", KEYCODE_RIGHT, ScancodeRight},
    {"PLUS", KEYCODE_PLUS, ScancodePlus}, {"MINUS", KEYCODE_MINUS, ScancodeMinus},
    {"TIMES", KEYCODE_TIMES, ScancodeTimes}, {"DIVIDE", KEYCODE_DIVIDE, ScancodeDivide},
    {"DOT", KEYCODE_DOT, ScancodeDot}, {"EQUALS", KEYCODE_EQUALS, ScancodeEquals},
    {"EXP", KEYCODE_EXP, ScancodeEXP}, {"POWER", KEYCODE_POWER, ScancodePower},
    {"COMMA", KEYCODE_COMMA, ScancodeComma}, {"NEGATIVE", KEYCODE_NEGATIVE, ScancodeNegative},
    {"LPAREN", KEYCODE_OPEN_PARENTHESIS, ScancodeOpenParenthesis},
    {"RPAREN", KEYCODE_CLOSE_PARENTHESIS, ScancodeCloseParenthesis},
    {"X", KEYCODE_X, ScancodeX}, {"Y", KEYCODE_Y, ScancodeY}, {"Z", KEYCODE_Z, ScancodeZ},
    {"SHIFT", KEYCODE_SHIFT, ScancodeShift}, {"BACKSPACE", KEYCODE_BACKSPACE, ScancodeBackspace},
    {"KEYBOARD", KEYCODE_KEYBOARD, ScancodeKeyboard},
    {"0", KEYCODE_0, Scancode0}, {"1", KEYCODE_1, Scancode1}, {"2", KEYCODE_2, Scancode2},
    {"3", KEYCODE_3, Scancode3}, {"4", KEYCODE_4, Scancode4}, {"5", KEYCODE_5, Scancode5},
    {"6", KEYCODE_6, Scancode6}, {"7", KEYCODE_7, Scancode7}, {"8", KEYCODE_8, Scancode8},
    {"9", KEYCODE_9, Scancode9},
};

static const Key* findKey(const std::string& k) {
    for (const Key& t : KEYS) if (k == t.name) return &t;
    fprintf(stderr, "unknown key %s\n", k.c_str());
    exit(1);
}

struct Cmd { std::string op, a; int n[4]; int line; };
static std::vector<Cmd> script;
static size_t pc = 0;
static int waitFrames = 0, holdFrames = 0;
static int heldScan = -1;                // scancode reported down by Input_GetKeyState, or -1
static struct { int dir, x, y; } pending[8];
static int nPending = 0, iPending = 0;
static long quitFrames = 0;
static double timeStart = 0; static int timeFrames = 0;

static void setKey(Input_Event* e, int code) {
    e->type = EVENT_KEY; e->data.key.direction = KEY_PRESSED; e->data.key.keyCode = (Input_Keycode)code;
}
// logical (landscape) -> panel coordinates
static void setTouch(Input_Event* e, int dir, int lx, int ly) {
    e->type = EVENT_TOUCH; e->data.touch_single.direction = (Input_TouchEventType)dir;
    e->data.touch_single.p1_x = 319 - ly; e->data.touch_single.p1_y = lx;
}

// After each scripted key / touch the next call returns EVENT_NONE, so the app finishes that
// frame (handles the input, redraws) before the script continues.
static bool endFrame = false;

static int nextEvent(Input_Event* e) {
    if (iPending < nPending) {
        setTouch(e, pending[iPending].dir, pending[iPending].x, pending[iPending].y);
        if (++iPending == nPending) endFrame = true;
        return 0;
    }
    nPending = iPending = 0;
    if (endFrame) { endFrame = false; return 0; }
    if (waitFrames > 0) { --waitFrames; ++timeFrames; return 0; }
    if (holdFrames > 0) { if (--holdFrames == 0) heldScan = -1; ++timeFrames; return 0; }
    while (pc < script.size()) {
        const Cmd& c = script[pc++];
        if (c.op == "key") { setKey(e, findKey(c.a)->code); endFrame = true; return 0; }
        if (c.op == "holdkey" || c.op == "hold") {
            const Key* k = findKey(c.op == "hold" ? std::string("EXE") : c.a);
            const int n = c.op == "hold" ? c.n[0] : c.n[1];
            // the press frame is the first held frame; the N-th frame after it releases the key
            heldScan = k->scan;
            holdFrames = n > 1 ? n : 1;
            timeFrames = 1; timeStart = now();
            setKey(e, k->code);
            endFrame = true;
            return 0;
        }
        if (c.op == "tap") { pending[0] = { TOUCH_DOWN, c.n[0], c.n[1] }; pending[1] = { TOUCH_UP, c.n[0], c.n[1] }; nPending = 2; iPending = 0; return nextEvent(e); }
        if (c.op == "down") { pending[0] = { TOUCH_DOWN, c.n[0], c.n[1] }; nPending = 1; iPending = 0; return nextEvent(e); }
        if (c.op == "up") { pending[0] = { TOUCH_UP, 0, 0 }; nPending = 1; iPending = 0; return nextEvent(e); }
        if (c.op == "frames") {
            timeFrames = 0; timeStart = now();
            if (c.n[0] <= 0) continue;
            waitFrames = c.n[0] - 1; ++timeFrames;
            return 0;
        }
        if (c.op == "shot") { shot(c.a.c_str()); continue; }
        if (c.op == "time") {
            const double dt = now() - timeStart;
            printf("%-24s %8.3f ms/frame over %d frames\n", c.a.c_str(), dt * 1000 / (timeFrames ? timeFrames : 1), timeFrames);
            continue;
        }
        fprintf(stderr, "script line %d: bad op %s\n", c.line, c.op.c_str());
        exit(1);
    }
    // Script over: keep sending CLEAR (one per frame) so the app leaves every screen.
    heldScan = -1;
    if (++quitFrames > MAX_FRAMES) tooLong();
    if (quitFrames & 1) setKey(e, KEYCODE_POWER_CLEAR);
    return 0;
}

static int host_getinput(Input_Event* e, uint32_t, uint32_t) {
    memset(e, 0, sizeof *e);
    e->type = EVENT_NONE;
    nextEvent(e);
    if (e->type == EVENT_NONE && ++g_frames > MAX_FRAMES) tooLong();
    return 0;
}

static bool host_keystate(uint16_t* scancode) { return heldScan >= 0 && *scancode == heldScan; }

int (*GetInput)(struct Input_Event*, uint32_t, uint32_t) = host_getinput;
bool (*Input_GetKeyStateU)(uint16_t*) __asm__("Input_GetKeyState") = host_keystate;
void (*LCD_Refresh)() = host_refresh;
uint16_t* (*_FP_LCD_GetVRAMAddress)() __asm__("LCD_GetVRAMAddress") = host_vram;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "script.txt";
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    char line[256];
    int lineNo = 0;
    while (fgets(line, sizeof line, f)) {
        ++lineNo;
        char op[32] = "", a[128] = ""; int n[4] = {0, 0, 0, 0};
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        if (sscanf(line, "%31s", op) != 1) continue;
        Cmd c; c.op = op; c.line = lineNo;
        if (c.op == "key" || c.op == "shot" || c.op == "time") {
            if (sscanf(line, "%*s %127s", a) != 1) { fprintf(stderr, "%s:%d: missing argument\n", path, lineNo); return 1; }
        } else if (c.op == "holdkey") {
            if (sscanf(line, "%*s %127s %d", a, &n[1]) != 2) { fprintf(stderr, "%s:%d: holdkey NAME N\n", path, lineNo); return 1; }
        } else {
            sscanf(line, "%*s %d %d %d %d", &n[0], &n[1], &n[2], &n[3]);
        }
        c.a = a; memcpy(c.n, n, sizeof n);
        if (c.op == "key" || c.op == "holdkey") findKey(c.a);   // fail early on typos
        script.push_back(c);
    }
    fclose(f);
    pg_main();
    printf("done: %ld frames, %ld LCD refreshes\n", g_frames, g_refreshes);
    return 0;
}
