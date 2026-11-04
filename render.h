// Game state -> pixels (landscape 528 x 320, see gfx.h). Only render.cpp / gfx.cpp touch VRAM.
#pragma once

#include <stdint.h>
#include "game.h"

namespace render {

// --- field geometry on screen ------------------------------------------------
constexpr int PX_X = 7;                // screen pixels per yard along the field (x)
constexpr int PX_Y = 5;                // screen pixels per yard across the field (y): tilted camera look
constexpr int HUD_H = 22;              // scoreboard strip at the top (ly 0..21)
constexpr int FIELD_TOP = 32;          // ly of the top sideline (y = 0)
constexpr int FIELD_BOT = FIELD_TOP + (int)((pg::FIELD_W * PX_Y) >> pg::FXS);   // ly of the bottom sideline
constexpr pg::fx VIEW_W = (pg::fx)((528 << pg::FXS) / PX_X);                   // yards visible (fx)

void init();                           // builds the field column templates; call once

// world <-> screen (camX = world x shown at screen column 0)
inline int sx(pg::fx x, pg::fx camX) { return (int)(((x - camX) * PX_X) >> pg::FXS); }
inline int sy(pg::fx y) { return FIELD_TOP + (int)((y * PX_Y) >> pg::FXS); }
inline pg::fx wx(int lx, pg::fx camX) { return camX + (pg::fx)((lx << pg::FXS) / PX_X); }
inline pg::fx wy(int ly) { return (pg::fx)(((ly - FIELD_TOP) << pg::FXS) / PX_Y); }

// Smoothly move the camera towards the play's focus; returns the new camX (clamped).
pg::fx followCamera(pg::fx camX, pg::fx focusX);
pg::fx cameraFor(pg::fx focusX);       // the target camX for a focus (no smoothing)

// --- the game screen ----------------------------------------------------------
// Field (stands, grass, lines, end zones, line of scrimmage + first-down marker),
// players sorted by depth, ball + shadow, landing marker, receiver key labels (1..4)
// while the QB can throw, the user-controlled player highlight.
void drawPlay(const pg::Play& pl, const pg::Match& m, pg::fx camX);
// Pre-snap view: field + both formations standing still (uses a Play set up by pg::snap).
void drawScoreboard(const pg::Match& m);                     // HUD strip at the top
void drawBanner(const char* text, uint16_t color);           // big centred message over the field
void drawSubBanner(const char* text, uint16_t color);        // smaller line below the banner

// --- widgets used by the menus (main.cpp) --------------------------------------
void drawMenuBackground(int seed);                           // stadium at night, fills the screen
void drawPanel(int x, int y, int w, int h, uint16_t accent); // dark card with a coloured frame
void drawButton(int x, int y, int w, int h, const char* label, int scale, uint16_t color);
void drawTeamBadge(int cx, int cy, int team, int scale);     // helmet-style emblem in team colours (scale 1..3)
void drawBar(int x, int y, int w, int h, int value, int max, uint16_t color);
// Kick meter: horizontal bar, green zone |needle-128| <= window, needle 0..255
void drawKickMeter(int x, int y, int w, int h, int needle, int window);

}  // namespace render
