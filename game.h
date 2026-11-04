// Pocket Gridiron - rules (pure C++: no SDK calls, no drawing, no floating point at run time).
//
// Coordinates: Q12 fixed point YARDS (1 yard = 4096). The user's offense always attacks to
// the RIGHT (+x). x = 0..120 yards: 0..10 own end zone, 10 own goal line, 110 opponent goal
// line, 110..120 opponent end zone. y = 0 (top sideline) .. FIELD_W (bottom sideline).
// One call of step() = one tick of 1/120 s. Everything is deterministic for a given seed.
#pragma once

#include <stdint.h>

namespace pg {

typedef int32_t fx;
constexpr int FXS = 12;
constexpr fx FX1 = 1 << FXS;
constexpr fx yd(double v) { return (fx)(v * FX1); }          // compile-time constants only

constexpr int TICK_HZ = 120;
constexpr int sec(double s) { return (int)(s * TICK_HZ); }    // compile-time constants only
// speed in yards per second -> fx per tick
constexpr int ydps(double v) { return (int)(v * FX1 / TICK_HZ); }

// ---------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------
constexpr fx FIELD_LEN = 120 * FX1;
constexpr fx FIELD_W = yd(160.0 / 3.0);       // 53.33 yards
constexpr fx GOAL_OWN = 10 * FX1;
constexpr fx GOAL_OPP = 110 * FX1;
constexpr fx HASH_TOP = yd(23.58);
constexpr fx HASH_BOT = yd(29.75);
constexpr fx MID_Y = FIELD_W / 2;

// ---------------------------------------------------------------------------
// Teams
// ---------------------------------------------------------------------------
constexpr int NTEAMS = 12;
constexpr uint16_t c565(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

struct TeamDef {
    const char* city;      // "HARBOR CITY"
    const char* name;      // "GULLS"
    const char* abbr;      // 3 letters
    uint16_t c1, c2;       // jersey / helmet colours (RGB565)
    uint8_t off, def;      // base ratings 1..10 (the user's own team uses its upgrades instead)
};
extern const TeamDef TEAMS[NTEAMS];

// Ratings used during one of the user's offensive plays (all 1..10).
struct Ratings {
    uint8_t qb;            // throw accuracy + ball speed
    uint8_t rb;            // runner speed + broken tackles
    uint8_t wr;            // receiver speed + hands
    uint8_t ol;            // pass protection / run blocking time
    uint8_t def;           // OPPONENT defense: speed, reaction, tackling, coverage
};

// ---------------------------------------------------------------------------
// One play (the user is always on offense; defense drives are simulated)
// ---------------------------------------------------------------------------
enum Role : uint8_t { R_QB, R_RB, R_WR, R_TE, R_OL, R_DL, R_LB, R_CB, R_S };

constexpr int NOFF = 8, NDEF = 8, NP = NOFF + NDEF;
enum Slot : int {
    P_QB = 0, P_RB = 1, P_WR1 = 2, P_WR2 = 3, P_SLOT = 4, P_OL0 = 5, P_OL1 = 6, P_OL2 = 7,
    P_DL0 = 8, P_DL1 = 9, P_DL2 = 10, P_LB = 11, P_CB1 = 12, P_CB2 = 13, P_S1 = 14, P_S2 = 15,
};
inline bool isOffense(int i) { return i < NOFF; }

// The receivers the user can throw to with keys 1..4 (index into Play::p).
constexpr int NTARGETS = 4;
constexpr int8_t TARGETS[NTARGETS] = { P_WR1, P_SLOT, P_WR2, P_RB };

enum PState : uint8_t {
    PS_NORMAL,
    PS_ENGAGED,            // blocker and defender locked together (timer = ticks left)
    PS_DOWN,               // on the ground after a missed tackle / juke (timer = ticks left)
    PS_SHED,               // defender just beat a block: cannot be re-engaged (timer)
};

struct Player {
    fx x, y;               // feet position
    fx vx, vy;             // velocity per tick
    fx tx, ty;             // AI goal point (also used for route waypoints)
    int16_t speed;         // max fx per tick
    int16_t timer;         // meaning depends on state
    uint8_t role;
    uint8_t state;
    int8_t mark;           // man-coverage / block assignment (player index) or -1
    uint8_t route;         // route id (receivers)
    uint8_t wp;            // next waypoint of the route
    uint8_t anim;          // leg animation phase (advances with distance run)
    int8_t face;           // +1 facing right, -1 facing left
    int8_t tackleCd;       // ticks before this defender may try another tackle
    int8_t eng;            // engaged blocker / defender while state == PS_ENGAGED
};

struct Ball {
    fx x, y, z;            // z = height above ground
    fx sx, sy;             // throw origin
    fx tx, ty;             // landing point
    int16_t t, T;          // ticks flown / total flight ticks
    int8_t target;         // intended receiver
};

enum PlayCall : uint8_t { CALL_QUICK, CALL_DEEP, CALL_RUN };
enum Phase : uint8_t {
    PH_LIVE,               // QB holds the ball, may throw (or hand off on a run call)
    PH_AIR,                // pass in the air
    PH_RUN,                // a ball carrier is running
    PH_OVER,               // play is dead; `result` says why
};
enum Result : uint8_t { RES_NONE, RES_TACKLE, RES_OOB, RES_INCOMPLETE, RES_TD, RES_INT, RES_SACK, RES_SAFETY };
// Short-lived notices for the HUD (Play::event, shown while eventT > 0).
enum Event : uint8_t { EV_NONE, EV_CATCH, EV_DROP, EV_DEFLECT, EV_JUKE, EV_BROKEN, EV_HANDOFF, EV_PICK };

struct Input {
    int8_t mx, my;         // -1/0/+1 steering in field axes (+x = towards the opponent goal)
    int8_t throwTo;        // -1, or a player index from TARGETS: throw to that receiver
    bool juke;             // juke / spin: a short moment where tackles miss (cooldown)
    bool steer;            // touch steering: run towards (sx, sy) instead of mx/my
    fx sx, sy;
};

struct Play {
    Player p[NP];
    Ball ball;
    Ratings r;
    uint32_t rng;
    fx los;                // line of scrimmage
    fx firstDown;          // first-down marker (x); may be past the goal line = goal to go
    fx ballY;              // y of the snap
    fx endX, endY;         // where the ball became dead
    int16_t t;             // ticks since the snap
    int16_t overT;         // ticks since the play ended
    int16_t jukeT, jukeCd;
    int16_t eventT;
    uint8_t call, phase, result, event;
    int8_t carrier;        // ball carrier or -1 (ball in air / dead)
    int8_t control;        // player the user steers (QB before the throw, then the carrier)
    bool crossed;          // the carrier crossed the line of scrimmage (no forward pass any more)
    bool caught;           // a pass was completed on this play
    bool thrown;           // a pass was thrown on this play
};

void setupPlay(Play& pl, fx los, fx ballY, fx firstDown, PlayCall call, const Ratings& r, uint32_t seed);
void step(Play& pl, const Input& in);          // one tick
bool canThrow(const Play& pl);
fx cameraFocus(const Play& pl);                // x the camera should look at
// Yards gained by the play (from the LOS to endX), rounded towards zero. Valid in PH_OVER.
int playYards(const Play& pl);

// ---------------------------------------------------------------------------
// A match: 4 quarters; the user's drives are played, the opponent's drives are simulated.
// ---------------------------------------------------------------------------
constexpr int QUARTER_SECS = 200;              // game clock per quarter (seconds)
constexpr int RUNOFF_SECS = 16;                // clock run-off after an in-bounds tackle

enum Next : uint8_t {
    NX_CALL,               // user: pick a play (or punt / field goal)
    NX_XP,                 // user: extra-point kick (kick meter)
    NX_OPP,                // opponent drive (call oppDrive, then show m.drive)
    NX_HALF,               // half-time card, then continueMatch()
    NX_FINAL,              // game over
};
enum DriveResult : uint8_t { DR_TD, DR_FG, DR_PUNT, DR_DOWNS, DR_TURNOVER, DR_MISSED_FG, DR_END_HALF, DR_SAFETY };

struct DriveReport {
    uint8_t result;
    uint8_t plays;
    int16_t yards, secs;
};

// What the last user action did (for the result banner).
struct LastPlay {
    uint8_t result;        // Result, or RES_NONE for kicks
    int8_t kind;           // KIND_* below
    int16_t yards;
    bool firstDown;
    bool turnover;         // turnover on downs / interception
};
enum Kind : int8_t { K_PLAY, K_FG_GOOD, K_FG_MISS, K_XP_GOOD, K_XP_MISS, K_PUNT };

struct Match {
    uint8_t user, opp;     // team indices
    Ratings r;             // user offense vs opponent defense
    uint8_t oppOff;        // opponent offense 1..10
    uint8_t userDef;       // user defense 1..10
    int16_t score[2];      // [0] user, [1] opponent
    uint8_t quarter;       // 1..4, 5+ = overtime
    int16_t clock;         // seconds left in the quarter (overtime: untimed)
    uint8_t down;          // 1..4
    fx ballX, ballY;       // next snap spot
    fx firstDown;
    uint8_t next;          // Next
    uint8_t userSecondHalf;// 1: the user receives the second-half kickoff
    uint8_t otPoss;        // overtime: possessions played in this round (0..2)
    uint8_t otRound;
    uint32_t rng;
    // opponent drive pending (set before NX_OPP): where they start, in yards from THEIR goal
    int16_t oppStart;
    DriveReport drive;     // the last simulated opponent drive
    LastPlay last;
    // user stats
    int16_t passAtt, passComp, passYds, rushYds, tds, ints, sacks;
    uint8_t playoff;       // 1: overtime goes on until somebody leads (set after matchInit)
    uint8_t endPending;    // the period ran out during a touchdown: end it after the extra point
};

void matchInit(Match& m, int user, int opp, const Ratings& r, int oppOff, int userDef, uint32_t seed);
void snap(Match& m, Play& pl, PlayCall call);  // set up the next user play from the match state
void applyPlay(Match& m, const Play& pl);      // play is PH_OVER: score, downs, clock, next
void oppDrive(Match& m);                       // next == NX_OPP: simulate it, fills m.drive
void continueMatch(Match& m);                  // next == NX_HALF: start the second half
// Kicks. The kick meter needle runs 0..255; a kick is good if |needle - 128| <= kickWindow(d).
int fgDistance(const Match& m);                // yards (snap spot + 17)
bool fgAllowed(const Match& m);                // in range for a try (<= 60 yards)
int kickWindow(int distYards);
void kickFG(Match& m, int needle);
void kickXP(Match& m, int needle);
void punt(Match& m);
bool userWon(const Match& m);
// "3RD & 7" / "1ST & GOAL" helpers for the HUD
int yardsToGo(const Match& m);                 // 0 = goal to go
int ballOnYard(const Match& m);                // field position 1..99 from the user's goal

// ---------------------------------------------------------------------------
// Season: 11-week round robin, then a 4-team playoff (semi-final, final).
// ---------------------------------------------------------------------------
constexpr int NWEEKS = NTEAMS - 1;
enum Stage : uint8_t { SS_REGULAR, SS_SEMI, SS_FINAL, SS_CHAMPION, SS_OUT };
enum Upgrade : uint8_t { U_QB, U_RB, U_WR, U_OL, U_DEF, U_COUNT };
constexpr int MAX_LEVEL = 10;

struct Season {
    uint8_t team;          // the user's team
    uint8_t week;          // regular-season week 0..NWEEKS-1 (NWEEKS = regular season over)
    uint8_t stage;         // Stage
    uint8_t year;          // 1, 2, ...
    uint8_t level[U_COUNT];// upgrade levels 1..10
    uint8_t pad0;
    uint16_t points;       // coach points to spend on upgrades
    uint16_t titles;       // championships won
    uint8_t w[NTEAMS], l[NTEAMS], t[NTEAMS];
    int16_t pf[NTEAMS], pa[NTEAMS];
    uint8_t playoffOpp;    // opponent in the current playoff round
    uint8_t lastOpp;       // last game (for the season screen)
    int16_t lastUs, lastThem;
    uint8_t lastStage;
    uint8_t otherSemi;     // a team of the other semi-final (playoffs)
    uint32_t rng;
};

void seasonNew(Season& s, int team, uint32_t seed);   // fresh franchise
void seasonNextYear(Season& s);                       // keeps team, levels, points, titles
int opponentOf(const Season& s);                      // -1 when the season is finished
int weekOpponent(int week, int team);                 // round-robin schedule (circle method)
int oppOffense(const Season& s, int team);            // 1..10 including the year bonus
int oppDefense(const Season& s, int team);
Ratings userRatings(const Season& s, int opp);
// Record the user's result, simulate the other games, advance the stage, award coach points.
// Returns the coach points earned.
int seasonRecord(Season& s, int us, int them, int userTds);
void standings(const Season& s, uint8_t order[NTEAMS]);
int upgradeCost(const Season& s, int which);          // 0 = already maxed
bool upgrade(Season& s, int which);

// ---------------------------------------------------------------------------
// Small helpers shared with render / tests
// ---------------------------------------------------------------------------
uint32_t rnd(uint32_t& s);                            // xorshift32
int rndRange(uint32_t& s, int n);                     // 0..n-1 (n > 0)
int isqrt32(uint32_t v);
fx dist(fx dx, fx dy);                                // approx. length, fx

}  // namespace pg
