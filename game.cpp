// Pocket Gridiron - rules. See game.h. Integer / fixed-point only (the CPU has no FPU).
#include "game.h"

namespace pg {

// ---------------------------------------------------------------------------
// Teams (all fictional)
// ---------------------------------------------------------------------------
const TeamDef TEAMS[NTEAMS] = {
    { "BAYSIDE",       "GULLS",     "BAY", c565(0, 150, 165),   c565(240, 240, 240), 5, 4 },
    { "IRONWOOD",      "SMELTERS",  "IRW", c565(235, 110, 20),  c565(35, 35, 35),    6, 5 },
    { "NORTHPEAK",     "YETIS",     "NPK", c565(190, 215, 255), c565(20, 40, 115),   4, 6 },
    { "SUNVALE",       "SCORPIONS", "SUN", c565(240, 190, 40),  c565(110, 60, 20),   7, 4 },
    { "RIVERTON",      "OTTERS",    "RIV", c565(140, 90, 50),   c565(240, 230, 200), 3, 4 },
    { "GRANITE FALLS", "RAMPARTS",  "GRF", c565(120, 120, 135), c565(205, 30, 40),   5, 7 },
    { "MOONBAY",       "COMETS",    "MNB", c565(115, 55, 175),  c565(205, 205, 215), 6, 6 },
    { "PRAIRIE",       "TWISTERS",  "PRA", c565(30, 140, 60),   c565(245, 220, 60),  4, 5 },
    { "LAKESHORE",     "PIKES",     "LKS", c565(30, 90, 205),   c565(250, 145, 40),  5, 5 },
    { "REDCLIFF",      "COYOTES",   "RDC", c565(150, 30, 50),   c565(230, 180, 60),  7, 6 },
    { "FROSTFORD",     "GLACIERS",  "FRF", c565(140, 210, 240), c565(10, 30, 75),    6, 7 },
    { "NEON CITY",     "VOLTS",     "NEO", c565(230, 40, 150),  c565(25, 25, 35),    8, 5 },
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
uint32_t rnd(uint32_t& s) {
    uint32_t x = s ? s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s = x;
    return x;
}

int rndRange(uint32_t& s, int n) { return (int)(((uint64_t)rnd(s) * (uint32_t)n) >> 32); }

int isqrt32(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (int)r;
}

fx dist(fx dx, fx dy) {
    uint32_t ax = (uint32_t)(dx < 0 ? -dx : dx), ay = (uint32_t)(dy < 0 ? -dy : dy);
    int s = 0;
    while ((ax | ay) >= 32768u) { ax >>= 1; ay >>= 1; ++s; }
    return (fx)isqrt32(ax * ax + ay * ay) << s;
}

namespace {

inline int iabs(int v) { return v < 0 ? -v : v; }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline bool roll(uint32_t& s, int percent) { return rndRange(s, 100) < percent; }

// yards (fx) -> whole yards, rounded towards zero
inline int whole(fx v) { return v >= 0 ? (int)(v >> FXS) : -(int)((-v) >> FXS); }

// ---------------------------------------------------------------------------
// Tuning (speeds in yards per second, times in seconds), all evaluated at compile time
// ---------------------------------------------------------------------------
namespace k {
constexpr int QB = ydps(6.4), RB = ydps(7.9), WR = ydps(8.0), SLOT = ydps(7.8), OL = ydps(4.8);
constexpr int DL = ydps(5.7), LB = ydps(6.7), CB = ydps(7.75), S = ydps(7.5);
constexpr int PER_LEVEL = ydps(0.13);          // speed per upgrade level / defense rating point
constexpr int ACCEL_DIV = 28;                  // ticks to reach full speed (roughly)
constexpr fx TACKLE_R = yd(1.0);
constexpr fx ENGAGE_R = yd(1.05);
constexpr fx CATCH_R = yd(1.35);
constexpr fx HANDOFF_R = yd(1.2);
constexpr fx SEP_R = yd(0.9);                  // team-mates push apart below this distance
constexpr int JUKE_T = sec(0.3), JUKE_CD = sec(2.2);
constexpr int MAX_PLAY = sec(30);
constexpr int EVENT_T = sec(0.9);
}  // namespace k

// ---------------------------------------------------------------------------
// Routes: waypoints relative to the start, in yards. dy > 0 = towards the middle of the field.
// After the last waypoint the receiver keeps running in that direction (or settles: STOP).
// ---------------------------------------------------------------------------
struct WP { int8_t dx, dy; };
struct Route { uint8_t n; bool stop; WP w[3]; };

enum RouteId : uint8_t {
    RT_GO, RT_SLANT, RT_OUT, RT_HITCH, RT_POST, RT_CORNER, RT_DIG, RT_DRAG, RT_FLAT, RT_CURL,
    RT_SEAM, RT_WHEEL, RT_CHECK, RT_BLOCK, RT_COUNT
};
const Route ROUTES[RT_COUNT] = {
    { 1, false, { { 30, 0 } } },                         // GO
    { 2, false, { { 3, 0 }, { 12, 9 } } },               // SLANT
    { 2, false, { { 6, 0 }, { 6, -14 } } },              // OUT
    { 2, true,  { { 7, 0 }, { 5, 1 } } },                // HITCH
    { 2, false, { { 12, 0 }, { 30, 11 } } },             // POST
    { 2, false, { { 12, 0 }, { 27, -11 } } },            // CORNER
    { 2, false, { { 12, 0 }, { 12, 18 } } },             // DIG
    { 2, false, { { 2, 2 }, { 4, 24 } } },               // DRAG
    { 2, false, { { 1, -5 }, { 3, -14 } } },             // FLAT (RB)
    { 2, true,  { { 11, 0 }, { 9, 2 } } },               // CURL
    { 1, false, { { 28, 1 } } },                         // SEAM
    { 3, false, { { 0, -6 }, { 4, -12 }, { 26, -13 } } },// WHEEL (RB)
    { 2, true,  { { 3, 3 }, { 5, 4 } } },                // CHECK-DOWN (RB)
    { 0, true,  { } },                                   // BLOCK (stays in to protect)
};

const uint8_t QUICK_WR[] = { RT_SLANT, RT_OUT, RT_HITCH, RT_CURL, RT_DRAG, RT_SLANT };
const uint8_t QUICK_SLOT[] = { RT_SLANT, RT_DRAG, RT_HITCH, RT_OUT };
const uint8_t QUICK_RB[] = { RT_FLAT, RT_CHECK, RT_FLAT };
const uint8_t DEEP_WR[] = { RT_GO, RT_POST, RT_CORNER, RT_DIG, RT_GO };
const uint8_t DEEP_SLOT[] = { RT_SEAM, RT_DIG, RT_POST, RT_CORNER };
const uint8_t DEEP_RB[] = { RT_BLOCK, RT_CHECK, RT_WHEEL, RT_BLOCK };

const int WIDE[3] = { P_WR1, P_WR2, P_SLOT };

template <int N> uint8_t pick(uint32_t& s, const uint8_t (&a)[N]) { return a[rndRange(s, N)]; }

// Defensive behaviour per player (kept in Player::route for defenders).
enum DefMode : uint8_t { DM_RUSH, DM_MAN, DM_SPY, DM_DEEP };

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------
fx clampY(fx y) { return clampi(y, yd(0.6), FIELD_W - yd(0.6)); }

// Accelerate towards the desired velocity (dvx, dvy).
void steerVel(Player& p, int dvx, int dvy) {
    const int acc = p.speed / k::ACCEL_DIV + 1;
    const int ax = clampi(dvx - p.vx, -acc, acc), ay = clampi(dvy - p.vy, -acc, acc);
    p.vx += ax;
    p.vy += ay;
}

// Desired velocity towards a point at speed `s`, slowing down for the last stretch.
void seek(Player& p, fx tx, fx ty, int s) {
    const fx dx = tx - p.x, dy = ty - p.y;
    const fx len = dist(dx, dy);
    if (len < yd(0.08)) { steerVel(p, 0, 0); return; }
    int want = len / 6;                            // arrive in about 6 ticks
    if (want > s) want = s;
    steerVel(p, (int)((int64_t)dx * want / len), (int)((int64_t)dy * want / len));
}

void integrate(Player& p) {
    p.x += p.vx;
    p.y += p.vy;
    const int moved = iabs(p.vx) + iabs(p.vy);
    p.anim = (uint8_t)(p.anim + (moved >> 6));
    if (p.vx > 24) p.face = 1;
    else if (p.vx < -24) p.face = -1;
}

// Point at `d` from (ax, ay) on the way to (bx, by).
void towards(fx ax, fx ay, fx bx, fx by, fx d, fx& ox, fx& oy) {
    const fx dx = bx - ax, dy = by - ay;
    const fx len = dist(dx, dy);
    if (len <= d || len == 0) { ox = bx; oy = by; return; }
    ox = ax + (fx)((int64_t)dx * d / len);
    oy = ay + (fx)((int64_t)dy * d / len);
}

int speedFor(int role, const Ratings& r) {
    switch (role) {
        case R_QB: return k::QB + r.qb * (k::PER_LEVEL / 2);
        case R_RB: return k::RB + r.rb * k::PER_LEVEL;
        case R_WR: return k::WR + r.wr * k::PER_LEVEL;
        case R_TE: return k::SLOT + r.wr * k::PER_LEVEL;
        case R_OL: return k::OL + r.ol * (k::PER_LEVEL / 2);
        case R_DL: return k::DL + r.def * (k::PER_LEVEL / 2);
        case R_LB: return k::LB + r.def * k::PER_LEVEL;
        case R_CB: return k::CB + r.def * k::PER_LEVEL;
        default:   return k::S + r.def * k::PER_LEVEL;
    }
}

void place(Play& pl, int i, int role, fx x, fx y) {
    Player& p = pl.p[i];
    p = Player();
    p.x = x; p.y = clampY(y);
    p.tx = p.x; p.ty = p.y;
    p.role = (uint8_t)role;
    p.speed = (int16_t)speedFor(role, pl.r);
    p.mark = -1;
    p.eng = -1;
    p.face = isOffense(i) ? 1 : -1;
    p.anim = (uint8_t)(i * 37);
}

void setEvent(Play& pl, int e) { pl.event = (uint8_t)e; pl.eventT = k::EVENT_T; }

void endPlay(Play& pl, int result, fx x, fx y) {
    if (pl.phase == PH_OVER) return;
    pl.phase = PH_OVER;
    pl.result = (uint8_t)result;
    pl.endX = x;
    pl.endY = y;
    pl.overT = 0;
}

}  // namespace

// Routes store their origin separately: Player::tx/ty is the current waypoint target, so the
// origin is reconstructed from the formation (one per receiver slot) at setup and kept here.
namespace {

struct RouteState { fx ox, oy; int8_t side; };   // origin + "towards the middle" sign

// Kept outside Play so the Play struct stays exactly as game.h documents; it is rebuilt from
// Play data on every setupPlay and is a pure function of it.
void routeTarget(const Play& pl, int i, const RouteState& rs, int wp, fx& tx, fx& ty) {
    const Route& rt = ROUTES[pl.p[i].route];
    if (wp >= rt.n) wp = rt.n - 1;
    tx = rs.ox + rt.w[wp].dx * FX1;
    ty = rs.oy + rs.side * rt.w[wp].dy * FX1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Play setup
// ---------------------------------------------------------------------------
namespace {

// Per-slot route origin and side, derived from the formation.
RouteState routeOrigin(const Play& pl, int i) {
    RouteState rs;
    switch (i) {
        case P_WR1: rs.ox = pl.los - yd(0.5); rs.oy = yd(5); rs.side = 1; break;
        case P_WR2: rs.ox = pl.los - yd(0.5); rs.oy = FIELD_W - yd(5); rs.side = -1; break;
        case P_SLOT: {
            fx y = pl.ballY - yd(10);
            if (y < yd(10)) y = yd(10);
            rs.ox = pl.los - yd(1); rs.oy = y; rs.side = 1; break;
        }
        default: rs.ox = pl.los - yd(5); rs.oy = pl.ballY + yd(2); rs.side = -1; break;   // RB
    }
    return rs;
}

}  // namespace

void setupPlay(Play& pl, fx los, fx ballY, fx firstDown, PlayCall call, const Ratings& r, uint32_t seed) {
    uint8_t* raw = (uint8_t*)&pl;
    for (unsigned i = 0; i < sizeof pl; ++i) raw[i] = 0;
    pl.r = r;
    pl.rng = seed ? seed : 0x1234567u;
    pl.los = los;
    pl.firstDown = firstDown;
    pl.ballY = clampi(ballY, HASH_TOP, HASH_BOT);
    pl.call = (uint8_t)call;
    pl.phase = PH_LIVE;
    pl.carrier = P_QB;
    pl.control = call == CALL_RUN ? (int8_t)P_RB : (int8_t)P_QB;
    const fx by = pl.ballY;
    const fx deep = GOAL_OPP + yd(8);              // never line up behind the end line
    auto dx = [&](fx v) { fx x = los + v; return x > deep ? deep : x; };

    // offense
    place(pl, P_QB, R_QB, los - yd(5), by);
    if (call == CALL_RUN) place(pl, P_RB, R_RB, los - yd(7), by);
    else place(pl, P_RB, R_RB, los - yd(5), by + yd(2));
    for (int i : WIDE) {
        const RouteState rs = routeOrigin(pl, i);
        place(pl, i, i == P_SLOT ? R_TE : R_WR, rs.ox, rs.oy);
    }
    place(pl, P_OL0, R_OL, los - yd(1), by - yd(2.2));
    place(pl, P_OL1, R_OL, los - yd(0.8), by);
    place(pl, P_OL2, R_OL, los - yd(1), by + yd(2.2));
    // defense
    place(pl, P_DL0, R_DL, dx(yd(1)), by - yd(2.4));
    place(pl, P_DL1, R_DL, dx(yd(1)), by + yd(0.3));
    place(pl, P_DL2, R_DL, dx(yd(1)), by + yd(2.4));
    place(pl, P_LB, R_LB, dx(yd(5)), by);
    place(pl, P_CB1, R_CB, dx(yd(6)), yd(5.5));
    place(pl, P_CB2, R_CB, dx(yd(6)), FIELD_W - yd(5.5));
    place(pl, P_S1, R_S, dx(yd(9)), pl.p[P_SLOT].y + yd(1));
    place(pl, P_S2, R_S, dx(yd(15)), MID_Y);

    // assignments
    for (int i = 0; i < 3; ++i) { pl.p[P_OL0 + i].mark = (int8_t)(P_DL0 + i); pl.p[P_DL0 + i].mark = (int8_t)(P_OL0 + i); }
    pl.p[P_CB1].mark = P_WR1;
    pl.p[P_CB2].mark = P_WR2;
    pl.p[P_S1].mark = P_SLOT;
    for (int i = P_DL0; i <= P_DL2; ++i) pl.p[i].route = DM_RUSH;
    pl.p[P_CB1].route = pl.p[P_CB2].route = pl.p[P_S1].route = DM_MAN;
    pl.p[P_S2].route = DM_DEEP;

    // routes
    uint32_t& s = pl.rng;
    if (call == CALL_QUICK) {
        pl.p[P_WR1].route = pick(s, QUICK_WR);
        pl.p[P_WR2].route = pick(s, QUICK_WR);
        pl.p[P_SLOT].route = pick(s, QUICK_SLOT);
        pl.p[P_RB].route = pick(s, QUICK_RB);
    } else if (call == CALL_DEEP) {
        pl.p[P_WR1].route = pick(s, DEEP_WR);
        pl.p[P_WR2].route = pick(s, DEEP_WR);
        pl.p[P_SLOT].route = pick(s, DEEP_SLOT);
        pl.p[P_RB].route = pick(s, DEEP_RB);
    } else {
        pl.p[P_WR1].route = pl.p[P_WR2].route = pl.p[P_SLOT].route = RT_BLOCK;
        pl.p[P_RB].route = RT_BLOCK;
        pl.p[P_WR1].mark = P_CB1;
        pl.p[P_WR2].mark = P_CB2;
        pl.p[P_SLOT].mark = P_LB;
    }
    // linebacker: blitz, cover the back or spy the quarterback
    const bool rbRoute = call != CALL_RUN && pl.p[P_RB].route != RT_BLOCK;
    if (roll(s, 12 + r.def * 2)) pl.p[P_LB].route = DM_RUSH;
    else if (rbRoute) { pl.p[P_LB].route = DM_MAN; pl.p[P_LB].mark = P_RB; }
    else pl.p[P_LB].route = DM_SPY;
    if (!rbRoute && call != CALL_RUN) pl.p[P_RB].mark = P_LB;   // pick up the blitz
    // staggered reaction clocks for the cover players
    for (int i = NOFF; i < NP; ++i) pl.p[i].timer = (int16_t)rndRange(s, 20);
}

bool canThrow(const Play& pl) {
    return pl.phase == PH_LIVE && pl.carrier == P_QB && !pl.crossed && pl.call != CALL_RUN;
}

fx cameraFocus(const Play& pl) {
    switch (pl.phase) {
        case PH_LIVE: return pl.call == CALL_RUN ? pl.los + yd(8) : pl.los + yd(13);
        case PH_AIR: return pl.ball.x + yd(3);
        case PH_RUN: return pl.carrier >= 0 ? pl.p[pl.carrier].x + yd(8) : pl.los;
        default: return pl.endX + yd(4);
    }
}

int playYards(const Play& pl) {
    if (pl.result == RES_INCOMPLETE || pl.result == RES_INT) return 0;
    return whole(pl.endX - pl.los);
}

// ---------------------------------------------------------------------------
// One tick
// ---------------------------------------------------------------------------
namespace {

int lagTicks(const Ratings& r) { return 40 - 3 * r.def; }

// Where a defender should be to cover receiver `m` (with a reaction lag: re-evaluated
// every lagTicks, aiming at where the receiver will be by then).
void coverMan(Play& pl, Player& d, const Player& m, int lag) {
    if (d.timer <= 0) {
        d.timer = (int16_t)lag;
        fx px = m.x + m.vx * lag + yd(1.4);         // a cushion downfield
        fx py = m.y + m.vy * lag;
        py += (py < MID_Y ? yd(0.6) : -yd(0.6));    // inside leverage
        d.tx = px; d.ty = clampY(py);
    }
    (void)pl;
    seek(d, d.tx, d.ty, d.speed);
}

int nearestFree(const Play& pl, fx x, fx y, int from, int to, fx maxD) {
    int best = -1;
    fx bd = maxD;
    for (int i = from; i < to; ++i) {
        const Player& p = pl.p[i];
        if (p.state == PS_DOWN || p.state == PS_ENGAGED) continue;
        const fx d = dist(p.x - x, p.y - y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int holdTicks(Play& pl, bool run) {
    const int base = run ? sec(1.5) : sec(2.2);
    return base + pl.r.ol * sec(0.2) - pl.r.def * sec(0.1) + rndRange(pl.rng, sec(0.5));
}

// Blocker b tries to get in front of defender d (between d and the ball).
void block(Play& pl, int bi, int di, bool run) {
    Player& b = pl.p[bi];
    Player& d = pl.p[di];
    const Player& goal = pl.p[pl.carrier >= 0 ? (int)pl.carrier : (int)P_QB];
    if (b.state == PS_ENGAGED) return;               // moved together with the defender
    fx px, py;
    towards(d.x, d.y, goal.x, goal.y, yd(0.9), px, py);
    seek(b, px, py, b.speed);
    if (d.state == PS_NORMAL && dist(d.x - b.x, d.y - b.y) < k::ENGAGE_R) {
        b.state = d.state = PS_ENGAGED;
        b.mark = (int8_t)di;
        b.eng = (int8_t)di;
        d.eng = (int8_t)bi;
        b.timer = d.timer = (int16_t)holdTicks(pl, run);
    }
}

void defender(Play& pl, int i) {
    Player& d = pl.p[i];
    if (d.state == PS_DOWN) {
        steerVel(d, 0, 0);
        if (--d.timer <= 0) d.state = PS_NORMAL;
        return;
    }
    if (d.state == PS_ENGAGED) {
        Player& b = pl.p[d.eng];
        // locked with a blocker: creep towards the ball (pass) or get pushed back (run)
        const Player& goal = pl.p[pl.carrier >= 0 ? (int)pl.carrier : (int)P_QB];
        const bool runBlock = pl.phase == PH_RUN || pl.call == CALL_RUN;
        const int s = runBlock ? -ydps(0.5) : ydps(0.7);
        const fx dx = goal.x - d.x, dy = goal.y - d.y;
        const fx len = dist(dx, dy);
        if (len > 0) steerVel(d, (int)((int64_t)dx * s / len), (int)((int64_t)dy * s / len));
        b.vx = d.vx; b.vy = d.vy;
        if (--d.timer <= 0) {
            d.state = PS_SHED; d.timer = sec(1.0);
            b.state = PS_NORMAL; b.mark = -1; b.timer = 0;
        }
        return;
    }
    if (d.state == PS_SHED && --d.timer <= 0) d.state = PS_NORMAL;

    const int lag = lagTicks(pl.r);
    switch (pl.phase) {
        case PH_LIVE: {
            const Player& qb = pl.p[pl.carrier >= 0 ? (int)pl.carrier : (int)P_QB];
            if (d.route == DM_RUSH || (pl.call == CALL_RUN && pl.t > sec(0.25))) {
                seek(d, qb.x, qb.y, d.speed);
            } else if (d.route == DM_MAN && d.mark >= 0) {
                --d.timer;
                coverMan(pl, d, pl.p[d.mark], lag);
            } else if (d.route == DM_SPY) {
                seek(d, pl.los + yd(5), clampY(qb.y), d.speed);
            } else {                                  // deep safety: stay behind the deepest receiver
                fx deepest = pl.los + yd(10), dy = MID_Y;
                for (int r : WIDE) {
                    if (pl.p[r].x > deepest - yd(4)) { deepest = pl.p[r].x + yd(4); dy = pl.p[r].y; }
                }
                if (--d.timer <= 0) { d.timer = (int16_t)lag; d.tx = deepest + yd(1); d.ty = (dy + MID_Y) / 2; }
                seek(d, d.tx, d.ty, d.speed);
            }
            break;
        }
        case PH_AIR: {
            // react to the throw after the lag, if the landing point is within reach
            const Ball& b = pl.ball;
            const int left = b.T - b.t;
            const fx reach = (fx)d.speed * (left + 30) + yd(4);
            if (b.t > lag / 2 && dist(b.tx - d.x, b.ty - d.y) < reach) {
                seek(d, b.tx, b.ty, d.speed);
            } else if (d.route == DM_MAN && d.mark >= 0) {
                --d.timer;
                coverMan(pl, d, pl.p[d.mark], lag);
            } else {
                steerVel(d, d.vx, d.vy);
            }
            break;
        }
        case PH_RUN: {
            const Player& c = pl.p[pl.carrier];
            const fx len = dist(c.x - d.x, c.y - d.y);
            int lead = d.speed > 0 ? len / d.speed : 0;   // ticks to get there
            if (lead > sec(0.45)) lead = sec(0.45);
            // pursuit angle: aim ahead of the runner, defenders behind him lose ground
            seek(d, c.x + c.vx * lead, c.y + c.vy * lead, d.speed);
            break;
        }
        default:
            steerVel(d, d.vx * 7 / 8, d.vy * 7 / 8);
            break;
    }
}

void offense(Play& pl, int i, const Input& in) {
    Player& p = pl.p[i];
    if (p.state == PS_DOWN) { steerVel(p, 0, 0); if (--p.timer <= 0) p.state = PS_NORMAL; return; }
    if (p.state == PS_ENGAGED) return;               // moved by its defender
    if (pl.phase == PH_OVER) { steerVel(p, p.vx * 7 / 8, p.vy * 7 / 8); return; }

    // the ball carrier / controlled player
    if (i == pl.carrier && (pl.phase == PH_RUN || i == P_QB)) {
        if (pl.phase == PH_LIVE && pl.call == CALL_RUN) { steerVel(p, 0, 0); return; }   // waiting to hand off
        const bool user = i == pl.control;
        int mx = user ? in.mx : 0, my = user ? in.my : 0;
        if (pl.phase == PH_RUN) {
            if (user && in.steer) { seek(p, in.sx, in.sy, p.speed); return; }
            if (mx == 0) mx = 1;                      // runners run forward on their own
            int s = mx < 0 ? p.speed / 2 : p.speed;
            int vx = mx * s, vy = my * s;
            if (my != 0) { vx = vx * 181 / 256; vy = vy * 181 / 256; }
            steerVel(p, vx, vy);
            return;
        }
        // quarterback in the pocket
        if (user && in.steer) { seek(p, in.sx, in.sy, p.speed); return; }
        if (mx || my) {
            int vx = mx * p.speed, vy = my * p.speed;
            if (mx && my) { vx = vx * 181 / 256; vy = vy * 181 / 256; }
            steerVel(p, vx, vy);
        } else if (pl.t < sec(0.7)) {
            seek(p, pl.los - yd(7), p.y, p.speed);    // drop back
        } else {
            steerVel(p, 0, 0);
        }
        return;
    }

    if (i == P_QB) { steerVel(p, p.vx * 3 / 4, p.vy * 3 / 4); return; }   // ball is gone
    switch (pl.phase) {
        case PH_LIVE:
        case PH_AIR: {
            if (p.role == R_OL) {
                if (p.mark >= 0 && pl.p[p.mark].state != PS_SHED) block(pl, i, p.mark, pl.call == CALL_RUN);
                else {
                    const int d = nearestFree(pl, p.x, p.y, NOFF, NP, yd(4));
                    if (d >= 0 && pl.p[d].state == PS_NORMAL) { p.mark = (int8_t)d; block(pl, i, d, pl.call == CALL_RUN); }
                    else seek(p, pl.los - yd(2), p.y, p.speed);
                }
                return;
            }
            if (i == P_RB && pl.call == CALL_RUN) {
                // take the handoff: run at the quarterback, then through the line
                const Player& qb = pl.p[P_QB];
                seek(p, qb.x + yd(1), qb.y + yd(0.6), p.speed);
                return;
            }
            if (pl.call == CALL_RUN || p.route == RT_BLOCK) {
                const int m = p.mark >= 0 && pl.p[p.mark].state == PS_NORMAL ? p.mark : nearestFree(pl, p.x, p.y, NOFF, NP, yd(6));
                if (m >= 0 && (pl.p[m].route == DM_RUSH || pl.call == CALL_RUN)) block(pl, i, m, pl.call == CALL_RUN);
                else seek(p, pl.p[P_QB].x + yd(1.5), pl.p[P_QB].y + yd(1), p.speed);
                return;
            }
            if (pl.phase == PH_AIR && i == pl.ball.target) {
                // go get the ball (if it can be reached; otherwise keep running the route)
                const int left = pl.ball.T - pl.ball.t;
                if (dist(pl.ball.tx - p.x, pl.ball.ty - p.y) < (fx)p.speed * (left + 20) + yd(2)) {
                    seek(p, pl.ball.tx, pl.ball.ty, p.speed);
                    return;
                }
            }
            // follow the route
            const Route& rt = ROUTES[p.route];
            const RouteState rs = routeOrigin(pl, i);
            fx tx, ty;
            routeTarget(pl, i, rs, p.wp, tx, ty);
            if (p.wp < rt.n && dist(tx - p.x, ty - p.y) < yd(0.7)) ++p.wp;
            if (p.wp >= rt.n) {
                if (rt.stop) { routeTarget(pl, i, rs, rt.n - 1, tx, ty); seek(p, tx, ty, p.speed / 3); return; }
                // extend the last leg
                fx px, py;
                if (rt.n >= 2) { WP a = rt.w[rt.n - 2], b = rt.w[rt.n - 1]; px = (b.dx - a.dx) * FX1; py = rs.side * (b.dy - a.dy) * FX1; }
                else { px = rt.w[0].dx * FX1; py = rs.side * rt.w[0].dy * FX1; }
                tx += px * 2; ty += py * 2;
            }
            if (tx > GOAL_OPP + yd(9)) tx = GOAL_OPP + yd(9);
            seek(p, tx, clampY(ty), p.speed);
            return;
        }
        case PH_RUN: {
            // escort the runner: block the nearest defender that threatens him
            const Player& c = pl.p[pl.carrier];
            if (p.mark >= 0 && pl.p[p.mark].state == PS_NORMAL && dist(pl.p[p.mark].x - c.x, pl.p[p.mark].y - c.y) < yd(10)) {
                block(pl, i, p.mark, true);
                return;
            }
            const int d = nearestFree(pl, c.x, c.y, NOFF, NP, yd(9));
            if (d >= 0 && pl.p[d].state == PS_NORMAL && dist(pl.p[d].x - p.x, pl.p[d].y - p.y) < yd(7)) {
                p.mark = (int8_t)d;
                block(pl, i, d, true);
            } else {
                seek(p, c.x + yd(2), c.y + (p.y < c.y ? -yd(3) : yd(3)), p.speed * 3 / 4);
            }
            return;
        }
        default: return;
    }
}

void throwBall(Play& pl, int target) {
    Player& qb = pl.p[P_QB];
    const Player& r = pl.p[target];
    const int v = ydps(17.0) + pl.r.qb * ydps(0.9);    // ball speed
    // lead the receiver: two iterations of "where will he be when the ball arrives"
    fx ax = r.x, ay = r.y;
    int T = 0;
    for (int it = 0; it < 3; ++it) {
        T = dist(ax - qb.x, ay - qb.y) / v + 1;
        ax = r.x + r.vx * T;
        ay = r.y + r.vy * T;
    }
    const fx len = dist(ax - qb.x, ay - qb.y);
    // accuracy: error grows with distance, shrinks with QB skill, grows under pressure
    int errPct = 10 - (int)pl.r.qb * 8 / 10;          // percent of the throw distance
    if (errPct < 2) errPct = 2;
    fx err = (fx)((int64_t)len * errPct / 100);
    const int rusher = nearestFree(pl, qb.x, qb.y, NOFF, NP, yd(2.5));
    if (rusher >= 0) err += yd(1.5);
    if (pl.t < sec(0.45)) err += yd(0.5);              // rushed release
    if (err > 0) {
        ax += (fx)((int64_t)(rndRange(pl.rng, 513) - 256) * err >> 8);
        ay += (fx)((int64_t)(rndRange(pl.rng, 513) - 256) * err >> 8);
    }
    Ball& b = pl.ball;
    b.sx = b.x = qb.x; b.sy = b.y = qb.y; b.z = yd(2);
    b.tx = ax; b.ty = ay;
    b.T = (int16_t)(dist(ax - qb.x, ay - qb.y) / v + 6);
    b.t = 0;
    b.target = (int8_t)target;
    pl.phase = PH_AIR;
    pl.carrier = -1;
    pl.thrown = true;
}

void resolveCatch(Play& pl) {
    Ball& b = pl.ball;
    if (b.ty < 0 || b.ty > FIELD_W || b.tx > FIELD_LEN || b.tx < 0) { setEvent(pl, EV_NONE); endPlay(pl, RES_INCOMPLETE, pl.los, pl.ballY); return; }
    int br = -1, bd = -1;
    fx dr = yd(99), dd = yd(99);
    for (int t = 0; t < NTARGETS; ++t) {
        const Player& p = pl.p[TARGETS[t]];
        if (p.state == PS_DOWN || p.state == PS_ENGAGED) continue;
        const fx d = dist(p.x - b.tx, p.y - b.ty);
        if (d < dr) { dr = d; br = TARGETS[t]; }
    }
    for (int i = NOFF; i < NP; ++i) {
        const Player& p = pl.p[i];
        if (p.state == PS_DOWN || p.state == PS_ENGAGED) continue;
        const fx d = dist(p.x - b.tx, p.y - b.ty);
        if (d < dd) { dd = d; bd = i; }
    }
    const bool rOk = dr <= k::CATCH_R, dOk = dd <= k::CATCH_R;
    uint32_t& s = pl.rng;
    const int wr = pl.r.wr, def = pl.r.def;
    int outcome = 0;                                    // 0 incomplete, 1 catch, 2 interception
    if (rOk && !dOk) {
        outcome = roll(s, 89 + wr) ? 1 : 0;
        if (!outcome) setEvent(pl, EV_DROP);
    } else if (!rOk && dOk) {
        outcome = roll(s, 10 + def) ? 2 : 0;
        if (!outcome) setEvent(pl, EV_DEFLECT);
    } else if (rOk && dOk) {
        const int margin = clampi((dd - dr) * 30 / FX1, -30, 30);   // receiver closer: positive
        const int pc = clampi(48 + 3 * wr - 2 * def + margin, 10, 92);
        const int pi = 3 + def / 2;
        const int x = rndRange(s, 100);
        outcome = x < pc ? 1 : x < pc + pi ? 2 : 0;
        if (!outcome) setEvent(pl, EV_DEFLECT);
    }
    if (outcome == 1) {
        Player& r = pl.p[br];
        r.x = (r.x + b.tx) / 2;                         // the ball snaps to his hands
        r.y = (r.y + b.ty) / 2;
        pl.carrier = (int8_t)br;
        pl.control = (int8_t)br;
        pl.phase = PH_RUN;
        pl.caught = true;
        if (r.x > pl.los) pl.crossed = true;
        setEvent(pl, EV_CATCH);
        for (int i = 0; i < NOFF; ++i) if (pl.p[i].state != PS_ENGAGED) pl.p[i].mark = -1;
    } else if (outcome == 2) {
        setEvent(pl, EV_PICK);
        endPlay(pl, RES_INT, b.tx, b.ty);
        pl.carrier = (int8_t)bd;
    } else {
        endPlay(pl, RES_INCOMPLETE, pl.los, pl.ballY);
    }
}

void tackles(Play& pl) {
    if (pl.carrier < 0) return;
    Player& c = pl.p[pl.carrier];
    for (int i = NOFF; i < NP; ++i) {
        Player& d = pl.p[i];
        if (d.state == PS_DOWN || d.state == PS_ENGAGED) continue;
        if (d.tackleCd > 0) { --d.tackleCd; continue; }
        if (dist(d.x - c.x, d.y - c.y) > k::TACKLE_R) continue;
        const int skill = c.role == R_RB ? pl.r.rb : c.role == R_QB ? 2 : pl.r.wr - 1;
        if (pl.jukeT > 0 && roll(pl.rng, 50 + 4 * skill - pl.r.def)) {
            d.state = PS_DOWN; d.timer = sec(0.8);
            setEvent(pl, EV_JUKE);
            continue;
        }
        const int p = clampi(80 + 2 * pl.r.def - 3 * skill, 40, 96);
        if (roll(pl.rng, p)) {
            int res = RES_TACKLE;
            if (c.x < GOAL_OWN) res = RES_SAFETY;
            else if (pl.carrier == P_QB && !pl.thrown && pl.phase == PH_LIVE) res = RES_SACK;
            d.vx = c.vx / 2; d.vy = c.vy / 2;
            endPlay(pl, res, c.x, c.y);
            return;
        }
        d.state = PS_DOWN; d.timer = sec(0.5);
        d.tackleCd = 30;
        c.vx = c.vx * 5 / 8; c.vy = c.vy * 5 / 8;
        setEvent(pl, EV_BROKEN);
    }
}

void separate(Play& pl, int from, int to) {
    for (int i = from; i < to; ++i) {
        Player& a = pl.p[i];
        if (a.state == PS_DOWN) continue;
        for (int j = i + 1; j < to; ++j) {
            Player& b = pl.p[j];
            if (b.state == PS_DOWN) continue;
            const fx dx = b.x - a.x, dy = b.y - a.y;
            if (iabs(dx) >= k::SEP_R || iabs(dy) >= k::SEP_R) continue;
            const fx d = dist(dx, dy);
            if (d >= k::SEP_R) continue;
            const fx push = (k::SEP_R - d) / 4;
            fx px, py;
            if (d == 0) { px = push; py = 0; }
            else { px = (fx)((int64_t)dx * push / d); py = (fx)((int64_t)dy * push / d); }
            a.x -= px; a.y -= py; b.x += px; b.y += py;
        }
    }
}

}  // namespace

void step(Play& pl, const Input& in) {
    if (pl.eventT > 0 && --pl.eventT == 0) pl.event = EV_NONE;
    if (pl.phase == PH_OVER) {
        ++pl.overT;
        for (int i = 0; i < NP; ++i) { Player& p = pl.p[i]; steerVel(p, p.vx * 7 / 8, p.vy * 7 / 8); integrate(p); }
        return;
    }
    ++pl.t;
    if (pl.jukeT > 0) --pl.jukeT;
    if (pl.jukeCd > 0) --pl.jukeCd;

    // user actions
    if (in.throwTo >= 0 && canThrow(pl)) {
        for (int t = 0; t < NTARGETS; ++t) if (TARGETS[t] == in.throwTo) { throwBall(pl, in.throwTo); break; }
    }
    if (in.juke && pl.jukeCd == 0 && pl.phase == PH_RUN) {
        pl.jukeT = k::JUKE_T;
        pl.jukeCd = k::JUKE_CD;
        Player& c = pl.p[pl.carrier];
        c.vy += (in.my != 0 ? in.my : (c.y < MID_Y ? 1 : -1)) * (c.speed / 2);
    }

    // handoff on run plays
    if (pl.phase == PH_LIVE && pl.call == CALL_RUN) {
        const Player& qb = pl.p[P_QB];
        Player& rb = pl.p[P_RB];
        if (pl.t > sec(0.25) && dist(rb.x - qb.x, rb.y - qb.y) < k::HANDOFF_R) {
            pl.carrier = P_RB;
            pl.control = P_RB;
            pl.phase = PH_RUN;
            setEvent(pl, EV_HANDOFF);
            for (int i = 0; i < NOFF; ++i) if (pl.p[i].state != PS_ENGAGED && pl.p[i].role != R_OL && i != P_WR1 && i != P_WR2 && i != P_SLOT) pl.p[i].mark = -1;
        }
    }

    for (int i = 0; i < NOFF; ++i) offense(pl, i, in);
    for (int i = NOFF; i < NP; ++i) defender(pl, i);
    for (int i = 0; i < NP; ++i) integrate(pl.p[i]);
    separate(pl, 0, NOFF);
    separate(pl, NOFF, NP);

    // ball
    if (pl.phase == PH_AIR) {
        Ball& b = pl.ball;
        ++b.t;
        b.x = b.sx + (fx)((int64_t)(b.tx - b.sx) * b.t / b.T);
        b.y = b.sy + (fx)((int64_t)(b.ty - b.sy) * b.t / b.T);
        // arc: 4 h u (1 - u), h grows with the length of the throw
        const fx h = yd(1.2) + (fx)(b.T * (FX1 / 24));
        const int u = b.t * 256 / b.T;
        b.z = yd(1.5) * (256 - u) / 256 + (fx)((int64_t)h * 4 * u * (256 - u) >> 16);
        if (b.t >= b.T) resolveCatch(pl);
    } else if (pl.carrier >= 0) {
        const Player& c = pl.p[pl.carrier];
        pl.ball.x = c.x; pl.ball.y = c.y; pl.ball.z = yd(1);
    }

    // carrier: line of scrimmage, touchdown, out of bounds, tackles
    if (pl.carrier >= 0 && pl.phase != PH_OVER) {
        Player& c = pl.p[pl.carrier];
        if (pl.carrier == P_QB && pl.phase == PH_LIVE && c.x > pl.los) {
            pl.crossed = true;
            pl.phase = PH_RUN;                        // QB scramble: now a runner
        }
        if (pl.phase == PH_RUN && c.x > pl.los) pl.crossed = true;
        if (c.x >= GOAL_OPP) { endPlay(pl, RES_TD, c.x, c.y); }
        else if (c.y <= 0 || c.y >= FIELD_W || c.x <= 0) {
            endPlay(pl, c.x < GOAL_OWN ? RES_SAFETY : RES_OOB, c.x, c.y < 0 ? 0 : c.y > FIELD_W ? FIELD_W : c.y);
        } else {
            tackles(pl);
        }
    }
    if (pl.phase != PH_OVER && pl.t > k::MAX_PLAY) {
        if (pl.carrier >= 0) endPlay(pl, RES_TACKLE, pl.p[pl.carrier].x, pl.p[pl.carrier].y);
        else endPlay(pl, RES_INCOMPLETE, pl.los, pl.ballY);
    }
}

// ---------------------------------------------------------------------------
// Match flow
// ---------------------------------------------------------------------------
namespace {

void startUserDrive(Match& m, int yardsFromOwnGoal) {
    yardsFromOwnGoal = clampi(yardsFromOwnGoal, 1, 99);
    m.ballX = GOAL_OWN + yardsFromOwnGoal * FX1;
    m.ballY = MID_Y;
    m.down = 1;
    m.firstDown = m.ballX + 10 * FX1;
    if (m.firstDown > GOAL_OPP) m.firstDown = GOAL_OPP;
    m.next = NX_CALL;
}

// The period is over (end of the 2nd or 4th quarter, or an overtime decision).
void endPeriod(Match& m) {
    m.endPending = 0;
    if (m.quarter == 2) { m.next = NX_HALF; return; }
    if (m.score[0] != m.score[1]) { m.next = NX_FINAL; return; }
    // tied after regulation: overtime, the user has the ball first
    m.quarter = 5;
    m.clock = 0;
    m.otRound = 1;
    m.otPoss = 0;
    startUserDrive(m, 25);
}

// Advance the game clock. Returns true when the half / game ended.
bool runClock(Match& m, int secs) {
    if (m.quarter > 4) return false;
    m.clock = (int16_t)(m.clock - secs);
    if (m.clock > 0) return false;
    if (m.quarter == 2 || m.quarter == 4) { m.clock = 0; return true; }
    ++m.quarter;
    m.clock = (int16_t)(m.clock + QUARTER_SECS);
    if (m.clock <= 0) m.clock = 1;
    return false;
}

// Overtime bookkeeping after each possession.
void otPossession(Match& m) {
    if (m.quarter < 5) return;
    if (++m.otPoss < 2) return;
    if (m.score[0] != m.score[1] || (!m.playoff && m.otRound >= 3)) { m.next = NX_FINAL; return; }
    ++m.otRound;
    m.otPoss = 0;
    startUserDrive(m, 25);
}

// The user's possession is over; the opponent gets the ball `start` yards from their goal.
void toOpponent(Match& m, int start, bool periodOver) {
    m.oppStart = (int16_t)clampi(start, 1, 99);
    m.next = NX_OPP;
    if (periodOver) { endPeriod(m); return; }
    if (m.quarter >= 5) {
        // the user's OT possession: count it (the opponent always gets its answer)
        ++m.otPoss;
    }
}

}  // namespace

void matchInit(Match& m, int user, int opp, const Ratings& r, int oppOff, int userDef, uint32_t seed) {
    uint8_t* raw = (uint8_t*)&m;
    for (unsigned i = 0; i < sizeof m; ++i) raw[i] = 0;
    m.user = (uint8_t)user;
    m.opp = (uint8_t)opp;
    m.r = r;
    m.oppOff = (uint8_t)clampi(oppOff, 1, 10);
    m.userDef = (uint8_t)clampi(userDef, 1, 10);
    m.rng = seed ? seed : 0xC0FFEEu;
    m.quarter = 1;
    m.clock = QUARTER_SECS;
    const bool userFirst = rnd(m.rng) & 1;
    m.userSecondHalf = userFirst ? 0 : 1;
    if (userFirst) startUserDrive(m, 25);
    else { m.oppStart = 25; m.next = NX_OPP; }
}

void snap(Match& m, Play& pl, PlayCall call) {
    setupPlay(pl, m.ballX, m.ballY, m.firstDown, call, m.r, rnd(m.rng));
}

void applyPlay(Match& m, const Play& pl) {
    const int yards = playYards(pl);
    m.last.result = pl.result;
    m.last.kind = K_PLAY;
    m.last.yards = (int16_t)yards;
    m.last.firstDown = false;
    m.last.turnover = false;
    if (pl.thrown) ++m.passAtt;
    if (pl.caught && pl.result != RES_INT) { ++m.passComp; m.passYds = (int16_t)(m.passYds + yards); }
    else if (!pl.thrown && pl.result != RES_SACK) m.rushYds = (int16_t)(m.rushYds + yards);
    if (pl.result == RES_SACK) ++m.sacks;

    int secs = pl.t / TICK_HZ + 1;
    if (pl.result == RES_TACKLE || pl.result == RES_SACK) secs += RUNOFF_SECS;
    const bool over = runClock(m, secs);

    switch (pl.result) {
        case RES_TD:
            m.score[0] = (int16_t)(m.score[0] + 6);
            ++m.tds;
            m.next = NX_XP;
            m.endPending = over ? 1 : 0;
            return;
        case RES_SAFETY:
            m.score[1] = (int16_t)(m.score[1] + 2);
            m.last.turnover = true;
            toOpponent(m, 35, over);
            return;
        case RES_INT: {
            ++m.ints;
            m.last.turnover = true;
            const int start = pl.endX >= GOAL_OPP ? 20 : whole(GOAL_OPP - pl.endX);
            toOpponent(m, start, over);
            return;
        }
        default: break;
    }
    // ordinary play: new spot, downs
    if (pl.result != RES_INCOMPLETE) {
        m.ballX = pl.endX < GOAL_OWN + FX1 / 2 ? GOAL_OWN + FX1 / 2 : pl.endX;
        m.ballY = clampi(pl.endY, HASH_TOP, HASH_BOT);
    }
    if (m.ballX >= m.firstDown) {
        m.down = 1;
        m.firstDown = m.ballX + 10 * FX1;
        if (m.firstDown > GOAL_OPP) m.firstDown = GOAL_OPP;
        m.last.firstDown = true;
        m.next = NX_CALL;
    } else if (++m.down > 4) {
        m.last.turnover = true;
        toOpponent(m, whole(GOAL_OPP - m.ballX), over);
        return;
    } else {
        m.next = NX_CALL;
    }
    if (over) endPeriod(m);
}

int fgDistance(const Match& m) { return whole(GOAL_OPP - m.ballX + FX1 / 2) + 17; }
bool fgAllowed(const Match& m) { return fgDistance(m) <= 60; }

int kickWindow(int d) {
    const int w = 102 - d * 3 / 2;
    return w < 6 ? 6 : w > 90 ? 90 : w;
}

void kickFG(Match& m, int needle) {
    const int d = fgDistance(m);
    const bool good = iabs(needle - 128) <= kickWindow(d);
    m.last = LastPlay();
    m.last.kind = good ? K_FG_GOOD : K_FG_MISS;
    m.last.yards = (int16_t)d;
    const bool over = runClock(m, 5);
    if (good) { m.score[0] = (int16_t)(m.score[0] + 3); toOpponent(m, 25, over); }
    else {
        int start = whole(GOAL_OPP - m.ballX) + 7;
        if (start < 20) start = 20;
        m.last.turnover = true;
        toOpponent(m, start, over);
    }
}

void kickXP(Match& m, int needle) {
    const bool good = iabs(needle - 128) <= kickWindow(20);
    m.last = LastPlay();
    m.last.kind = good ? K_XP_GOOD : K_XP_MISS;
    m.last.yards = 20;
    if (good) ++m.score[0];
    toOpponent(m, 25, m.endPending != 0);
}

void punt(Match& m) {
    const int net = 36 + rndRange(m.rng, 12);
    const fx land = m.ballX + net * FX1;
    m.last = LastPlay();
    m.last.kind = K_PUNT;
    m.last.yards = (int16_t)net;
    const bool over = runClock(m, 8);
    toOpponent(m, land >= GOAL_OPP ? 20 : whole(GOAL_OPP - land), over);
}

void oppDrive(Match& m) {
    uint32_t& s = m.rng;
    const int start = m.oppStart, toGo = 100 - start;
    const int d = (int)m.oppOff - (int)m.userDef;
    const int pTD = clampi(26 + 4 * d + (start - 25) * 6 / 10, 5, 85);
    const int pFG = clampi(20 + d + (start - 25) / 5, 4, 40);
    const int pTO = clampi(11 - d / 2, 3, 20);
    const int x = rndRange(s, 100);
    DriveReport& r = m.drive;
    r = DriveReport();
    int userStart = 25;
    if (x < pTD) {
        r.result = DR_TD;
        r.yards = (int16_t)toGo;
        r.plays = (uint8_t)(3 + toGo / 11 + rndRange(s, 4));
    } else if (x < pTD + pFG) {
        r.yards = (int16_t)clampi(toGo - 5 - rndRange(s, 28), 0, toGo);
        r.plays = (uint8_t)(3 + r.yards / 10 + rndRange(s, 4));
        const int fg = toGo - r.yards + 17;
        if (fg <= 55 && roll(s, 92 - (fg - 20))) r.result = DR_FG;
        else { r.result = DR_MISSED_FG; userStart = clampi(100 - (start + r.yards) + 7, 20, 99); }
    } else if (x < pTD + pFG + pTO) {
        r.result = DR_TURNOVER;
        r.yards = (int16_t)clampi(rndRange(s, 45), 0, toGo - 1);
        r.plays = (uint8_t)(1 + r.yards / 10 + rndRange(s, 4));
        userStart = clampi(100 - (start + r.yards), 1, 99);
    } else {
        const bool downs = roll(s, 12);
        r.result = downs ? DR_DOWNS : DR_PUNT;
        r.yards = (int16_t)clampi(rndRange(s, 35), 0, toGo - 1);
        r.plays = (uint8_t)(3 + r.yards / 8 + rndRange(s, 3));
        const int spot = start + r.yards;              // their field position
        if (downs) userStart = clampi(100 - spot, 1, 99);
        else {
            const int land = spot + 36 + rndRange(s, 12);
            userStart = land >= 100 ? 20 : 100 - land;
        }
    }
    r.secs = (int16_t)(r.plays * (9 + rndRange(s, 6)));
    // the clock can run out first
    bool over = false;
    if (m.quarter <= 4) {
        if (r.secs >= m.clock && (m.quarter == 2 || m.quarter == 4)) {
            r.secs = m.clock;
            if (!((r.result == DR_TD || r.result == DR_FG) && roll(s, 45))) r.result = DR_END_HALF;
        }
        over = runClock(m, r.secs);
    }
    if (r.result == DR_TD) m.score[1] = (int16_t)(m.score[1] + (roll(s, 95) ? 7 : 6));
    else if (r.result == DR_FG) m.score[1] = (int16_t)(m.score[1] + 3);
    if (over) { endPeriod(m); return; }
    startUserDrive(m, userStart);
    if (m.quarter >= 5) otPossession(m);
}

void continueMatch(Match& m) {
    m.quarter = 3;
    m.clock = QUARTER_SECS;
    if (m.userSecondHalf) startUserDrive(m, 25);
    else { m.oppStart = 25; m.next = NX_OPP; }
}

bool userWon(const Match& m) { return m.score[0] > m.score[1]; }

int yardsToGo(const Match& m) {
    if (m.firstDown >= GOAL_OPP) return 0;
    const int y = (int)((m.firstDown - m.ballX + FX1 - 1) >> FXS);
    return y < 1 ? 1 : y;
}

int ballOnYard(const Match& m) { return clampi(whole(m.ballX - GOAL_OWN + FX1 / 2), 1, 99); }

// ---------------------------------------------------------------------------
// Season
// ---------------------------------------------------------------------------
int weekOpponent(int week, int team) {
    const int n = NTEAMS - 1;                         // circle method: team n is fixed
    week %= n;
    if (team == n) return week;
    if (team == week) return n;
    return ((2 * week - team) % n + n) % n;
}

// the league gets a little better every second season
static int yearBonus(const Season& s) { return s.year >= 7 ? 3 : (s.year - 1) / 2; }

int oppOffense(const Season& s, int t) { return clampi(TEAMS[t].off + yearBonus(s), 1, 10); }
int oppDefense(const Season& s, int t) { return clampi(TEAMS[t].def + yearBonus(s), 1, 10); }

Ratings userRatings(const Season& s, int opp) {
    Ratings r;
    r.qb = s.level[U_QB];
    r.rb = s.level[U_RB];
    r.wr = s.level[U_WR];
    r.ol = s.level[U_OL];
    r.def = (uint8_t)(opp >= 0 ? oppDefense(s, opp) : 5);
    return r;
}

namespace {

// Points for a simulated (non-user) game: a handful of drives.
int simPoints(uint32_t& s, int off, int def) {
    int pts = 0;
    const int drives = 5 + rndRange(s, 3);
    const int d = off - def;
    for (int i = 0; i < drives; ++i) {
        const int x = rndRange(s, 100);
        if (x < clampi(24 + 4 * d, 5, 80)) pts += 7;
        else if (x < clampi(24 + 4 * d, 5, 80) + 18) pts += 3;
    }
    return pts;
}

void addResult(Season& s, int a, int b, int pa, int pb) {
    s.pf[a] = (int16_t)(s.pf[a] + pa); s.pa[a] = (int16_t)(s.pa[a] + pb);
    s.pf[b] = (int16_t)(s.pf[b] + pb); s.pa[b] = (int16_t)(s.pa[b] + pa);
    if (pa > pb) { ++s.w[a]; ++s.l[b]; }
    else if (pb > pa) { ++s.w[b]; ++s.l[a]; }
    else { ++s.t[a]; ++s.t[b]; }
}

// Simulated game between two CPU teams; `noTie` for the playoffs. Returns the winner.
int simGame(Season& s, int a, int b, bool record, bool noTie) {
    int pa = simPoints(s.rng, oppOffense(s, a), oppDefense(s, b));
    int pb = simPoints(s.rng, oppOffense(s, b), oppDefense(s, a));
    if (pa == pb && (noTie || roll(s.rng, 80))) { if (rnd(s.rng) & 1) pa += 3; else pb += 3; }
    if (record) addResult(s, a, b, pa, pb);
    return pa >= pb ? a : b;
}

}  // namespace

void seasonNew(Season& s, int team, uint32_t seed) {
    uint8_t* raw = (uint8_t*)&s;
    for (unsigned i = 0; i < sizeof s; ++i) raw[i] = 0;
    s.team = (uint8_t)clampi(team, 0, NTEAMS - 1);
    s.year = 1;
    s.stage = SS_REGULAR;
    for (int i = 0; i < U_COUNT; ++i) s.level[i] = 3;
    s.points = 6;
    s.rng = seed ? seed : 0xBADC0DEu;
}

void seasonNextYear(Season& s) {
    if (s.year < 250) ++s.year;
    s.week = 0;
    s.stage = SS_REGULAR;
    for (int i = 0; i < NTEAMS; ++i) { s.w[i] = s.l[i] = s.t[i] = 0; s.pf[i] = s.pa[i] = 0; }
    s.playoffOpp = 0;
    s.lastStage = SS_REGULAR;
}

int opponentOf(const Season& s) {
    if (s.stage == SS_REGULAR && s.week < NWEEKS) return weekOpponent(s.week, s.team);
    if (s.stage == SS_SEMI || s.stage == SS_FINAL) return s.playoffOpp;
    return -1;
}

void standings(const Season& s, uint8_t order[NTEAMS]) {
    for (int i = 0; i < NTEAMS; ++i) order[i] = (uint8_t)i;
    auto better = [&](int a, int b) {
        const int ka = s.w[a] * 2 + s.t[a], kb = s.w[b] * 2 + s.t[b];
        if (ka != kb) return ka > kb;
        const int da = s.pf[a] - s.pa[a], db = s.pf[b] - s.pa[b];
        if (da != db) return da > db;
        if (s.pf[a] != s.pf[b]) return s.pf[a] > s.pf[b];
        return a < b;
    };
    for (int i = 1; i < NTEAMS; ++i) {
        const uint8_t v = order[i];
        int j = i - 1;
        while (j >= 0 && better(v, order[j])) { order[j + 1] = order[j]; --j; }
        order[j + 1] = v;
    }
}

int seasonRecord(Season& s, int us, int them, int userTds) {
    const int opp = opponentOf(s);
    if (opp < 0) return 0;
    s.lastOpp = (uint8_t)opp;
    s.lastUs = (int16_t)us;
    s.lastThem = (int16_t)them;
    s.lastStage = s.stage;
    const bool win = us > them, tie = us == them;
    int pts = (win ? 3 : tie ? 2 : 1) + (userTds > 0 ? userTds / 2 : 0);
    if (s.stage == SS_REGULAR) {
        addResult(s, s.team, opp, us, them);
        for (int t = 0; t < NTEAMS; ++t) {
            const int o = weekOpponent(s.week, t);
            if (t < o && t != s.team && o != s.team) simGame(s, t, o, true, false);
        }
        if (++s.week >= NWEEKS) {
            uint8_t ord[NTEAMS];
            standings(s, ord);
            int seed = -1;
            for (int i = 0; i < 4; ++i) if (ord[i] == s.team) seed = i;
            if (seed < 0) s.stage = SS_OUT;
            else {
                s.stage = SS_SEMI;
                s.playoffOpp = ord[3 - seed];
                // remember the other semi-final pairing in pad1 (first team) for the final
                int a = -1;
                for (int i = 0; i < 4; ++i) if (ord[i] != s.team && ord[i] != s.playoffOpp) { a = ord[i]; break; }
                s.otherSemi = (uint8_t)a;
                pts += 2;
            }
        }
    } else if (s.stage == SS_SEMI) {
        if (win) {
            // the other semi-final: pad1 vs the remaining top-4 team
            uint8_t ord[NTEAMS];
            standings(s, ord);
            int b = -1;
            for (int i = 0; i < 4; ++i) if (ord[i] != s.team && ord[i] != opp && ord[i] != s.otherSemi) b = ord[i];
            s.playoffOpp = (uint8_t)(b >= 0 ? simGame(s, s.otherSemi, b, false, true) : s.otherSemi);
            s.stage = SS_FINAL;
            pts += 2;
        } else s.stage = SS_OUT;
    } else if (s.stage == SS_FINAL) {
        if (win) { s.stage = SS_CHAMPION; ++s.titles; pts += 5; }
        else s.stage = SS_OUT;
    }
    s.points = (uint16_t)(s.points + pts);
    return pts;
}

int upgradeCost(const Season& s, int which) {
    if (which < 0 || which >= U_COUNT || s.level[which] >= MAX_LEVEL) return 0;
    return s.level[which] + 1;
}

bool upgrade(Season& s, int which) {
    const int c = upgradeCost(s, which);
    if (c == 0 || s.points < c) return false;
    s.points = (uint16_t)(s.points - c);
    ++s.level[which];
    return true;
}

}  // namespace pg
