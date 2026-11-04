// Unit tests for the rules (game.h) and the save format (save.h). PC only.
// The checks are written against the documented API, so they should hold for any sane
// implementation of game.cpp; a failure prints the file:line of the broken expectation.
#include "game.h"
#include "save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace pg;

static int failures = 0;
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
#define CHECKM(c, ...) do { ++checks; if (!(c)) { printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); ++failures; } } while (0)

static const Ratings MID = { 5, 5, 5, 5, 5 };
static const PlayCall CALLS[3] = { CALL_QUICK, CALL_DEEP, CALL_RUN };
static const Input NONE = { 0, 0, -1, false, false, 0, 0 };

static double yds(fx v) { return v / (double)FX1; }

// Players must stay on (or very near) the field: catches overflow / NaN-like garbage.
static bool sane(const Play& pl, int* bad = nullptr) {
    for (int i = 0; i < NP; ++i) {
        const Player& p = pl.p[i];
        if (p.x < -10 * FX1 || p.x > FIELD_LEN + 10 * FX1 || p.y < -10 * FX1 || p.y > FIELD_W + 10 * FX1 ||
            p.vx < -FX1 / 2 || p.vx > FX1 / 2 || p.vy < -FX1 / 2 || p.vy > FX1 / 2) {   // 60 yd/s
            if (bad) *bad = i;
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
static void testRandom() {
    uint32_t a = 12345, b = 12345;
    bool allSame = true, anyZero = false;
    uint32_t first = 0;
    for (int i = 0; i < 1000; ++i) {
        const uint32_t x = rnd(a), y = rnd(b);
        if (x != y) allSame = false;
        if (x == 0) anyZero = true;
        if (i == 0) first = x;
    }
    CHECK(allSame);
    CHECK(!anyZero);
    uint32_t c = 54321;
    CHECK(rnd(c) != first);                 // different seed, different stream
    int hist[7] = {0};
    uint32_t s = 99;
    bool inRange = true;
    for (int i = 0; i < 7000; ++i) {
        const int v = rndRange(s, 7);
        if (v < 0 || v >= 7) { inRange = false; break; }
        ++hist[v];
    }
    CHECK(inRange);
    for (int i = 0; i < 7; ++i) CHECKM(hist[i] > 700 && hist[i] < 1300, "value %d drawn %d times of 7000", i, hist[i]);
    uint32_t t = 7;
    for (int i = 0; i < 100; ++i) CHECK(rndRange(t, 1) == 0);
}

static void testMath() {
    CHECK(isqrt32(0) == 0);
    CHECK(isqrt32(1) == 1);
    CHECK(isqrt32(15) == 3);
    CHECK(isqrt32(16) == 4);
    CHECK(isqrt32(1000000) == 1000);
    CHECK(isqrt32(0xFFFFFFFFu) == 65535);
    uint32_t s = 1;
    for (int i = 0; i < 20000; ++i) {
        const uint32_t v = rnd(s) >> (rnd(s) & 31);
        const uint64_t r = (uint64_t)isqrt32(v);
        if (!(r * r <= v && (r + 1) * (r + 1) > v)) { CHECKM(false, "isqrt32(%u) = %u", v, (unsigned)r); break; }
    }
    CHECK(dist(0, 0) == 0);
    const fx d345 = dist(3 * FX1, 4 * FX1);
    CHECKM(d345 > yd(4.75) && d345 < yd(5.25), "dist(3,4) = %.3f", yds(d345));
    CHECK(dist(-3 * FX1, 4 * FX1) == d345 || (dist(-3 * FX1, 4 * FX1) > yd(4.75) && dist(-3 * FX1, 4 * FX1) < yd(5.25)));
    CHECK(dist(10 * FX1, 0) > yd(9.7) && dist(10 * FX1, 0) < yd(10.3));
    // random vectors up to the field size: within 6 % of the true length
    for (int i = 0; i < 20000; ++i) {
        const fx dx = (fx)(rnd(s) % (2 * FIELD_LEN)) - FIELD_LEN, dy = (fx)(rnd(s) % (2 * FIELD_W)) - FIELD_W;
        const double want = __builtin_sqrt((double)dx * dx + (double)dy * dy);
        const fx got = dist(dx, dy);
        if (want > 64 && (got < want * 0.94 || got > want * 1.06)) {
            CHECKM(false, "dist(%d, %d) = %d, want ~%.0f", dx, dy, got, want);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
static void testSetup() {
    static Play pl;
    for (int ci = 0; ci < 3; ++ci)
        for (int k = 0; k < 4; ++k) {
            const fx los = (fx)(GOAL_OWN + (5 + 22 * k) * FX1);
            const fx ballY = k & 1 ? HASH_TOP : HASH_BOT;
            memset(&pl, 0, sizeof pl);
            setupPlay(pl, los, ballY, los + 10 * FX1, CALLS[ci], MID, 1000u + (uint32_t)k);
            CHECK(pl.los == los);
            CHECK(pl.firstDown == los + 10 * FX1);
            CHECK(pl.phase == PH_LIVE);
            CHECK(pl.result == RES_NONE);
            CHECK(pl.call == CALLS[ci]);
            CHECK(pl.t == 0);
            CHECK(pl.carrier == P_QB);
            CHECK(pl.control == P_QB || (CALLS[ci] == CALL_RUN && pl.control == P_RB));
            CHECK(!pl.thrown && !pl.caught && !pl.crossed);
            CHECK(pl.p[P_QB].role == R_QB && pl.p[P_RB].role == R_RB && pl.p[P_WR1].role == R_WR && pl.p[P_WR2].role == R_WR);
            CHECK(pl.p[P_OL0].role == R_OL && pl.p[P_OL2].role == R_OL && pl.p[P_DL0].role == R_DL && pl.p[P_DL2].role == R_DL);
            CHECK(pl.p[P_LB].role == R_LB && pl.p[P_CB1].role == R_CB && pl.p[P_CB2].role == R_CB && pl.p[P_S1].role == R_S && pl.p[P_S2].role == R_S);
            fx offMean = 0, defMean = 0;
            for (int i = 0; i < NP; ++i) {
                const Player& p = pl.p[i];
                CHECKM(p.x >= 0 && p.x <= FIELD_LEN && p.y >= 0 && p.y <= FIELD_W, "player %d at (%.1f, %.1f)", i, yds(p.x), yds(p.y));
                CHECKM(p.speed > 0 && p.speed < ydps(20), "player %d speed %d", i, p.speed);
                if (isOffense(i)) {
                    CHECKM(p.x <= los + yd(0.5), "offense %d at x %.2f, los %.2f", i, yds(p.x), yds(los));
                    offMean += p.x / NOFF;
                } else {
                    CHECKM(p.x >= los - yd(0.5), "defense %d at x %.2f, los %.2f", i, yds(p.x), yds(los));
                    defMean += p.x / NDEF;
                }
            }
            CHECK(defMean > offMean);
            CHECK(pl.p[P_QB].x < los);              // the QB is behind the line
            CHECK(cameraFocus(pl) >= 0 && cameraFocus(pl) <= FIELD_LEN);
        }
}

// Simulates a play without input: no garbage, and it must end (sack / tackle) within 30 s.
static void testNoInputPlays() {
    static Play pl;
    int ended = 0, total = 0;
    for (int ci = 0; ci < 3; ++ci)
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            const fx los = GOAL_OWN + (fx)((10 + seed * 3) * FX1);
            memset(&pl, 0, sizeof pl);
            setupPlay(pl, los, MID_Y, los + 10 * FX1, CALLS[ci], MID, seed * 7919u);
            int t = 0, bad = -1;
            bool ok = true;
            while (pl.phase != PH_OVER && t < sec(30)) {
                step(pl, NONE);
                ++t;
                if (!sane(pl, &bad)) { ok = false; break; }
            }
            CHECKM(ok, "call %d seed %u: player %d at (%.1f, %.1f) v (%d, %d) after %d ticks", ci, seed, bad,
                   bad >= 0 ? yds(pl.p[bad].x) : 0.0, bad >= 0 ? yds(pl.p[bad].y) : 0.0, bad >= 0 ? pl.p[bad].vx : 0, bad >= 0 ? pl.p[bad].vy : 0, t);
            ++total;
            if (pl.phase == PH_OVER) {
                ++ended;
                CHECK(pl.result != RES_NONE);
                const int y = playYards(pl);
                CHECKM(y > -40 && y <= 100, "play yards %d", y);
                CHECK(pl.endX >= -10 * FX1 && pl.endX <= FIELD_LEN + 10 * FX1);
                CHECK(pl.t >= t - 1 && pl.t <= t + 1);
                // the play stays over and sane while the result banner shows
                for (int i = 0; i < sec(1); ++i) step(pl, NONE);
                CHECK(pl.phase == PH_OVER && sane(pl));
            } else {
                CHECKM(false, "call %d seed %u: no-input play still running after 30 s (phase %d)", ci, seed, pl.phase);
            }
        }
    printf("no-input plays: %d / %d ended within 30 s\n", ended, total);
}

// Same seed + same inputs -> identical bytes.
static void testDeterminism() {
    static Play a, b;
    for (int ci = 0; ci < 3; ++ci) {
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        setupPlay(a, 35 * FX1, MID_Y, 45 * FX1, CALLS[ci], MID, 4242);
        setupPlay(b, 35 * FX1, MID_Y, 45 * FX1, CALLS[ci], MID, 4242);
        CHECK(memcmp(&a, &b, sizeof a) == 0);
        uint32_t r = 777;
        for (int i = 0; i < sec(12); ++i) {
            Input in = NONE;
            const uint32_t v = rnd(r);
            in.mx = (int8_t)((int)(v % 3) - 1);
            in.my = (int8_t)((int)((v >> 4) % 3) - 1);
            in.juke = ((v >> 8) & 63) == 0;
            if (i == sec(1.5) && canThrow(a)) in.throwTo = TARGETS[(v >> 12) % NTARGETS];
            step(a, in);
            step(b, in);
        }
        CHECK(memcmp(&a, &b, sizeof a) == 0);
        // a different seed gives a different play (randomness is actually used)
        memset(&b, 0, sizeof b);
        setupPlay(b, 35 * FX1, MID_Y, 45 * FX1, CALLS[ci], MID, 4243);
        memset(&a, 0, sizeof a);
        setupPlay(a, 35 * FX1, MID_Y, 45 * FX1, CALLS[ci], MID, 4242);
        for (int i = 0; i < sec(3); ++i) { step(a, NONE); step(b, NONE); }
        CHECK(memcmp(&a, &b, sizeof a) != 0);
    }
}

// A pass can be thrown from the pocket and goes in the air.
static void testThrow() {
    static Play pl;
    int thrown = 0, tries = 0;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
        memset(&pl, 0, sizeof pl);
        setupPlay(pl, 40 * FX1, MID_Y, 50 * FX1, CALL_QUICK, MID, seed);
        for (int i = 0; i < sec(0.8) && pl.phase == PH_LIVE; ++i) step(pl, NONE);
        if (pl.phase != PH_LIVE) continue;
        ++tries;
        CHECK(canThrow(pl));
        Input in = NONE;
        in.throwTo = TARGETS[0];
        step(pl, in);
        for (int i = 0; i < 3 && pl.phase == PH_LIVE; ++i) step(pl, NONE);
        if (pl.thrown) ++thrown;
        CHECK(pl.thrown);
        CHECK(pl.phase != PH_LIVE);
        CHECK(!canThrow(pl));
        for (int i = 0; i < sec(30) && pl.phase != PH_OVER; ++i) {
            step(pl, NONE);
            if (!sane(pl)) { CHECK(sane(pl)); break; }
        }
        CHECK(pl.phase == PH_OVER);
        if (pl.phase == PH_OVER) CHECK(pl.result == RES_INCOMPLETE || pl.result == RES_TACKLE || pl.result == RES_OOB ||
                                       pl.result == RES_TD || pl.result == RES_INT || pl.result == RES_SAFETY);
    }
    CHECK(tries >= 5);
    printf("throws: %d thrown of %d attempts\n", thrown, tries);
}

// ---------------------------------------------------------------------------
// Match rules with hand-made play outcomes.
static void fakeOver(Play& pl, Result res, int gainYards) {
    pl.phase = PH_OVER;
    pl.result = res;
    pl.endX = pl.los + gainYards * FX1;
    pl.endY = pl.ballY;
    pl.carrier = res == RES_INCOMPLETE ? -1 : P_RB;
    pl.crossed = gainYards > 0;
    pl.t = sec(5);
}

static void toUserCall(Match& m) {
    for (int guard = 0; guard < 20 && m.next != NX_CALL; ++guard) {
        if (m.next == NX_OPP) oppDrive(m);
        else if (m.next == NX_XP) kickXP(m, 128);
        else if (m.next == NX_HALF) continueMatch(m);
        else break;
    }
}

static void testMatch() {
    static Match m;
    static Play pl;
    memset(&m, 0, sizeof m);
    matchInit(m, 2, 5, MID, 5, 5, 31337);
    CHECK(m.user == 2 && m.opp == 5);
    CHECK(m.score[0] == 0 && m.score[1] == 0);
    CHECK(m.quarter == 1);
    CHECK(m.clock == QUARTER_SECS);
    CHECK(m.next == NX_CALL || m.next == NX_OPP);
    toUserCall(m);
    CHECK(m.next == NX_CALL);
    if (m.next != NX_CALL) return;
    // make sure there is room for the tests below
    m.ballX = GOAL_OWN + 25 * FX1;
    m.ballY = MID_Y;
    m.down = 1;
    m.firstDown = m.ballX + 10 * FX1;
    m.quarter = 1;
    m.clock = 120;
    CHECK(yardsToGo(m) == 10);
    CHECK(ballOnYard(m) == 25);
    const int s0 = m.score[0];

    // gain 4: 2nd & 6
    snap(m, pl, CALL_RUN);
    CHECK(pl.los == m.ballX);
    CHECK(pl.firstDown == m.firstDown);
    CHECK(pl.phase == PH_LIVE);
    int16_t clock = m.clock;
    fakeOver(pl, RES_TACKLE, 4);
    applyPlay(m, pl);
    CHECK(m.next == NX_CALL);
    CHECK(m.down == 2);
    CHECKM(yardsToGo(m) == 6, "yardsToGo %d", yardsToGo(m));
    CHECK(ballOnYard(m) == 29);
    CHECK(!m.last.firstDown && !m.last.turnover);
    CHECK(m.clock <= clock);

    // incomplete: 3rd & 6 from the same spot
    const fx spot = m.ballX;
    snap(m, pl, CALL_QUICK);
    pl.thrown = true;
    fakeOver(pl, RES_INCOMPLETE, 0);
    pl.endX = pl.los;
    applyPlay(m, pl);
    CHECK(m.down == 3);
    CHECK(m.ballX == spot);
    CHECK(yardsToGo(m) == 6);

    // gain >= yardsToGo: first down
    snap(m, pl, CALL_RUN);
    fakeOver(pl, RES_TACKLE, 7);
    applyPlay(m, pl);
    CHECK(m.down == 1);
    CHECK(m.last.firstDown);
    CHECK(yardsToGo(m) == 10);
    CHECK(m.ballX > spot + 6 * FX1);
    CHECK(m.score[0] == s0);

    // a loss: 2nd & 13
    snap(m, pl, CALL_RUN);
    fakeOver(pl, RES_TACKLE, -3);
    applyPlay(m, pl);
    CHECK(m.down == 2);
    CHECK(yardsToGo(m) == 13);

    // three incompletions: turnover on downs, the opponent gets the ball
    for (int i = 0; i < 3; ++i) {
        snap(m, pl, CALL_QUICK);
        pl.thrown = true;
        fakeOver(pl, RES_INCOMPLETE, 0);
        pl.endX = pl.los;
        applyPlay(m, pl);
    }
    CHECK(m.last.turnover);
    CHECK(m.next == NX_OPP);
    CHECK(m.score[0] == s0);

    // touchdown: +6, then the extra point
    toUserCall(m);
    CHECK(m.next == NX_CALL);
    if (m.next != NX_CALL) return;
    m.ballX = GOAL_OPP - 20 * FX1; m.down = 1; m.firstDown = GOAL_OPP - 10 * FX1; m.quarter = 2; m.clock = 100;
    const int u0 = m.score[0], o0 = m.score[1];
    snap(m, pl, CALL_DEEP);
    pl.thrown = pl.caught = true;
    fakeOver(pl, RES_TD, 25);
    applyPlay(m, pl);
    CHECK(m.score[0] == u0 + 6);
    CHECK(m.next == NX_XP);
    kickXP(m, 128);                    // perfect needle
    CHECK(m.score[0] == u0 + 7);
    CHECK(m.next == NX_OPP);
    CHECK(m.score[1] == o0);

    // field goals
    toUserCall(m);
    if (m.next != NX_CALL) { CHECK(m.next == NX_CALL); return; }
    m.quarter = 2; m.clock = 100;
    m.ballX = GOAL_OPP - 30 * FX1; m.down = 4; m.firstDown = m.ballX + 5 * FX1;
    CHECKM(fgDistance(m) >= 46 && fgDistance(m) <= 48, "fgDistance %d at the 30", fgDistance(m));
    CHECK(fgAllowed(m));
    m.ballX = GOAL_OPP - 50 * FX1;
    CHECK(fgDistance(m) > 60 && !fgAllowed(m));
    m.ballX = GOAL_OPP - 10 * FX1;
    CHECK(fgAllowed(m));
    const int u1 = m.score[0];
    kickFG(m, 128);
    CHECK(m.score[0] == u1 + 3);
    CHECK(m.last.kind == K_FG_GOOD);
    CHECK(m.next == NX_OPP);
    toUserCall(m);
    if (m.next != NX_CALL) { CHECK(m.next == NX_CALL); return; }
    m.quarter = 2; m.clock = 100;
    m.ballX = GOAL_OPP - 38 * FX1; m.down = 4; m.firstDown = m.ballX + 5 * FX1;
    const int u2 = m.score[0];
    kickFG(m, 0);                      // needle at the far end: a miss
    CHECK(m.score[0] == u2);
    CHECK(m.last.kind == K_FG_MISS);
    CHECK(m.next == NX_OPP);
    // punt
    toUserCall(m);
    if (m.next != NX_CALL) { CHECK(m.next == NX_CALL); return; }
    m.quarter = 2; m.clock = 100;
    m.ballX = GOAL_OWN + 20 * FX1; m.down = 4; m.firstDown = m.ballX + 8 * FX1;
    const int u3 = m.score[0], o3 = m.score[1];
    punt(m);
    CHECK(m.score[0] == u3 && m.score[1] == o3);
    CHECK(m.last.kind == K_PUNT);
    CHECK(m.next == NX_OPP);
    oppDrive(m);
    CHECK(m.drive.result <= DR_SAFETY);
    CHECK(m.drive.plays < 60);
    CHECK(m.drive.secs >= 0);
}

static void testKickWindow() {
    for (int d = 18; d < 70; ++d) CHECKM(kickWindow(d) >= kickWindow(d + 1), "kickWindow(%d) = %d < kickWindow(%d) = %d", d, kickWindow(d), d + 1, kickWindow(d + 1));
    CHECK(kickWindow(20) > kickWindow(55));
    CHECK(kickWindow(20) > 0 && kickWindow(20) < 128);
    CHECK(kickWindow(60) >= 0);
}

// A full match with a dumb user (runs, punts on 4th) must finish.
static void testFullMatch() {
    static Match m;
    static Play pl;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        memset(&m, 0, sizeof m);
        matchInit(m, 0, (int)seed, MID, 5, 5, seed * 99991u);
        int actions = 0, lastQ = m.quarter;
        bool monotonic = true;
        while (m.next != NX_FINAL && actions < 2000) {
            ++actions;
            switch (m.next) {
                case NX_CALL:
                    if (m.down == 4 && fgAllowed(m) && fgDistance(m) <= 35) kickFG(m, 128);
                    else if (m.down == 4) punt(m);
                    else {
                        snap(m, pl, CALL_RUN);
                        Input in = NONE;
                        in.mx = 1;
                        int t = 0;
                        while (pl.phase != PH_OVER && t < sec(60)) { step(pl, in); ++t; }
                        CHECK(pl.phase == PH_OVER);
                        applyPlay(m, pl);
                    }
                    break;
                case NX_XP: kickXP(m, 128); break;
                case NX_OPP: oppDrive(m); break;
                case NX_HALF: continueMatch(m); break;
                default: CHECKM(false, "bad next %d", m.next); actions = 99999; break;
            }
            if (m.quarter < lastQ) monotonic = false;
            lastQ = m.quarter;
        }
        CHECKM(m.next == NX_FINAL, "match %u not over after %d actions (q %d, %d s)", seed, actions, m.quarter, m.clock);
        CHECK(monotonic);
        CHECK(m.quarter >= 4);
        CHECK(m.score[0] >= 0 && m.score[1] >= 0);
        if (m.score[0] != m.score[1]) CHECK(userWon(m) == (m.score[0] > m.score[1]));
        printf("dumb match %u: %d-%d after %d actions\n", seed, m.score[0], m.score[1], actions);
    }
}

// ---------------------------------------------------------------------------
static void testSchedule() {
    int met[NTEAMS][NTEAMS];
    memset(met, 0, sizeof met);
    for (int w = 0; w < NWEEKS; ++w)
        for (int t = 0; t < NTEAMS; ++t) {
            const int o = weekOpponent(w, t);
            CHECKM(o >= 0 && o < NTEAMS && o != t, "week %d team %d -> %d", w, t, o);
            if (o < 0 || o >= NTEAMS) continue;
            CHECKM(weekOpponent(w, o) == t, "week %d: %d plays %d but %d plays %d", w, t, o, o, weekOpponent(w, o));
            ++met[t][o];
        }
    for (int a = 0; a < NTEAMS; ++a)
        for (int b = 0; b < NTEAMS; ++b)
            if (a != b) CHECKM(met[a][b] == 1, "teams %d and %d meet %d times", a, b, met[a][b]);
}

static int games(const Season& s, int t) { return s.w[t] + s.l[t] + s.t[t]; }

static void playRegular(Season& s, bool userWins) {
    for (int w = 0; w < NWEEKS; ++w) {
        CHECK(s.week == w);
        CHECK(s.stage == SS_REGULAR);
        const int o = opponentOf(s);
        CHECKM(o == weekOpponent(w, s.team), "week %d: opponentOf %d, schedule %d", w, o, weekOpponent(w, s.team));
        const uint16_t pts = s.points;
        const int earned = seasonRecord(s, userWins ? 28 : 3, userWins ? 10 : 24, userWins ? 4 : 0);
        CHECK(earned >= 0);
        CHECK(s.points == pts + earned);
        if (w + 1 < NWEEKS) {
            CHECK(s.week == w + 1);
            int sum = 0;
            for (int t = 0; t < NTEAMS; ++t) { sum += games(s, t); CHECK(games(s, t) == w + 1); }
            CHECK(sum == NTEAMS * (w + 1));
        }
    }
}

static void checkStandings(const Season& s) {
    uint8_t order[NTEAMS];
    memset(order, 0xFF, sizeof order);
    standings(s, order);
    bool seen[NTEAMS] = {false};
    for (int i = 0; i < NTEAMS; ++i) {
        CHECK(order[i] < NTEAMS);
        if (order[i] < NTEAMS) { CHECK(!seen[order[i]]); seen[order[i]] = true; }
    }
    for (int i = 1; i < NTEAMS; ++i) {
        const int a = order[i - 1], b = order[i];
        if (a >= NTEAMS || b >= NTEAMS) continue;
        CHECKM(2 * s.w[a] + s.t[a] >= 2 * s.w[b] + s.t[b], "standings %d (%d-%d-%d) above %d (%d-%d-%d)", a, s.w[a], s.l[a], s.t[a], b, s.w[b], s.l[b], s.t[b]);
    }
}

static void testSeason() {
    static Season s;
    memset(&s, 0, sizeof s);
    seasonNew(s, 3, 2024);
    CHECK(s.team == 3 && s.week == 0 && s.stage == SS_REGULAR && s.year == 1 && s.titles == 0);
    for (int i = 0; i < U_COUNT; ++i) CHECK(s.level[i] >= 1 && s.level[i] <= MAX_LEVEL);
    for (int t = 0; t < NTEAMS; ++t) CHECK(games(s, t) == 0 && s.pf[t] == 0 && s.pa[t] == 0);
    for (int t = 0; t < NTEAMS; ++t) {
        CHECK(oppOffense(s, t) >= 1 && oppOffense(s, t) <= 10);
        CHECK(oppDefense(s, t) >= 1 && oppDefense(s, t) <= 10);
        CHECK(TEAMS[t].city && TEAMS[t].name && TEAMS[t].abbr && strlen(TEAMS[t].abbr) == 3);
    }
    const Ratings r = userRatings(s, opponentOf(s));
    CHECK(r.qb >= 1 && r.qb <= 10 && r.rb >= 1 && r.rb <= 10 && r.wr >= 1 && r.wr <= 10 && r.ol >= 1 && r.ol <= 10 && r.def >= 1 && r.def <= 10);

    // undefeated: makes the playoff, wins the title
    playRegular(s, true);
    for (int t = 0; t < NTEAMS; ++t) CHECK(games(s, t) == NWEEKS);
    CHECK(s.w[3] == NWEEKS);
    CHECK(s.pf[3] == 28 * NWEEKS && s.pa[3] == 10 * NWEEKS);
    checkStandings(s);
    CHECK(s.stage == SS_SEMI);
    int o = opponentOf(s);
    CHECK(o >= 0 && o < NTEAMS && o != 3);
    CHECK(o == s.playoffOpp);
    seasonRecord(s, 21, 14, 3);
    CHECK(s.stage == SS_FINAL);
    o = opponentOf(s);
    CHECK(o >= 0 && o < NTEAMS && o != 3);
    seasonRecord(s, 17, 10, 2);
    CHECK(s.stage == SS_CHAMPION);
    CHECK(s.titles == 1);
    CHECK(opponentOf(s) == -1);
    const uint8_t lv0 = s.level[0];
    seasonNextYear(s);
    CHECK(s.year == 2 && s.week == 0 && s.stage == SS_REGULAR && s.team == 3 && s.titles == 1);
    CHECK(s.level[0] == lv0);
    for (int t = 0; t < NTEAMS; ++t) CHECK(games(s, t) == 0);
    for (int t = 0; t < NTEAMS; ++t) CHECK(oppOffense(s, t) >= 1 && oppOffense(s, t) <= 10);

    // winless: out after the regular season
    playRegular(s, false);
    checkStandings(s);
    CHECK(s.w[3] == 0);
    CHECK(s.stage == SS_OUT);
    CHECK(opponentOf(s) == -1);

    // a playoff loss ends the season
    seasonNew(s, 0, 77);
    playRegular(s, true);
    CHECK(s.stage == SS_SEMI);
    seasonRecord(s, 7, 30, 1);
    CHECK(s.stage == SS_OUT);
    CHECK(opponentOf(s) == -1);
    CHECK(s.titles == 0);

    // upgrades
    seasonNew(s, 5, 1);
    s.points = 60000;
    for (int u = 0; u < U_COUNT; ++u) {
        int n = 0;
        while (upgradeCost(s, u) > 0 && n < 20) {
            const int lv = s.level[u], cost = upgradeCost(s, u);
            const uint16_t pts = s.points;
            CHECK(upgrade(s, u));
            CHECK(s.level[u] == lv + 1);
            CHECK(s.points == pts - cost);
            ++n;
        }
        CHECK(s.level[u] == MAX_LEVEL);
        CHECK(upgradeCost(s, u) == 0);
        CHECK(!upgrade(s, u));
    }
    seasonNew(s, 5, 1);
    s.points = 0;
    CHECK(upgradeCost(s, U_QB) > 0);
    CHECK(!upgrade(s, U_QB));
}

// ---------------------------------------------------------------------------
static void testSave() {
    static save::Data d, e;
    save::defaults(d);
    CHECK(d.set.pace == 4 && d.set.timer == 0 && d.set.showFps == 0 && d.set.difficulty == 1);
    CHECK(d.hasSeason == 0 && d.inMatch == 0);
    d.set.pace = 7; d.set.timer = 1; d.set.showFps = 1; d.set.difficulty = 2;
    d.hasSeason = 1; d.inMatch = 1;
    seasonNew(d.season, 4, 99);
    seasonRecord(d.season, 24, 17, 3);
    d.season.points = 1234;
    matchInit(d.match, 4, opponentOf(d.season), userRatings(d.season, opponentOf(d.season)), 6, 5, 555);
    d.match.score[0] = 14; d.match.score[1] = 10;
    static uint8_t rec[save::RECORD_SIZE], bad[save::RECORD_SIZE];
    save::encode(d, rec);
    CHECK(rec[0] == 'P' && rec[1] == 'G' && rec[2] == 'S' && rec[3] == save::VERSION);
    CHECK(save::decode(rec, e));
    CHECK(e.set.pace == 7 && e.set.timer == 1 && e.set.showFps == 1 && e.set.difficulty == 2);
    CHECK(e.hasSeason == 1 && e.inMatch == 1);
    CHECK(memcmp(&e.season, &d.season, sizeof d.season) == 0);
    CHECK(memcmp(&e.match, &d.match, sizeof d.match) == 0);

    // corruption anywhere is rejected and leaves defaults
    const int spots[] = { 0, 2, 3, 4, 7, 8, 13, save::RECORD_SIZE / 2, save::RECORD_SIZE - 3, save::RECORD_SIZE - 2, save::RECORD_SIZE - 1 };
    for (int sp : spots) {
        memcpy(bad, rec, sizeof rec);
        bad[sp] ^= 0x10;
        e.set.pace = 99;
        CHECKM(!save::decode(bad, e), "flipped byte %d accepted", sp);
        CHECK(e.set.pace == 4 && e.set.difficulty == 1 && e.hasSeason == 0 && e.inMatch == 0);
    }
    // out-of-range values with a valid checksum are rejected too
    struct Bad { const char* what; void (*f)(save::Data&); };
    static const Bad BADS[] = {
        { "pace 0", [](save::Data& x) { x.set.pace = 0; } },
        { "pace 9", [](save::Data& x) { x.set.pace = 9; } },
        { "timer 2", [](save::Data& x) { x.set.timer = 2; } },
        { "showFps 2", [](save::Data& x) { x.set.showFps = 2; } },
        { "difficulty 3", [](save::Data& x) { x.set.difficulty = 3; } },
        { "hasSeason 2", [](save::Data& x) { x.hasSeason = 2; } },
        { "inMatch 2", [](save::Data& x) { x.inMatch = 2; } },
        { "team", [](save::Data& x) { x.season.team = NTEAMS; } },
        { "stage", [](save::Data& x) { x.season.stage = SS_OUT + 1; } },
        { "level 0", [](save::Data& x) { x.season.level[2] = 0; } },
        { "level 11", [](save::Data& x) { x.season.level[4] = MAX_LEVEL + 1; } },
    };
    for (const Bad& b : BADS) {
        save::Data x = d;
        b.f(x);
        save::encode(x, bad);
        CHECKM(!save::decode(bad, e), "%s accepted", b.what);
        CHECK(e.set.pace == 4 && e.hasSeason == 0);
    }
    // a bad season is fine when there is no season
    {
        save::Data x = d;
        x.hasSeason = 0;
        x.season.team = 200;
        save::encode(x, bad);
        CHECK(save::decode(bad, e));
        CHECK(e.hasSeason == 0 && e.set.pace == 7);
    }
    // defaults encode/decode
    save::defaults(d);
    save::encode(d, bad);
    CHECK(save::decode(bad, e));
    CHECK(e.set.pace == 4 && e.hasSeason == 0);

    // the file
    save::defaults(d);
    d.set.pace = 2;
    d.hasSeason = 1;
    seasonNew(d.season, 11, 5);
    CHECK(save::store(d));
    CHECK(save::load(e));
    CHECK(e.set.pace == 2 && e.hasSeason == 1 && memcmp(&e.season, &d.season, sizeof d.season) == 0);
    FILE* f = fopen(PG_SAVE_PATH, "wb");
    if (f) { fwrite(rec, 1, 10, f); fclose(f); }
    CHECK(!save::load(e));                 // truncated file
    CHECK(e.set.pace == 4);
    remove(PG_SAVE_PATH);
    CHECK(!save::load(e));                 // no file
}

int main() {
    testRandom();
    testMath();
    testSave();
    testSchedule();
    testKickWindow();
    testSetup();
    testDeterminism();
    testNoInputPlays();
    testThrow();
    testMatch();
    testSeason();
    testFullMatch();
    if (failures) { printf("%d of %d check(s) failed\n", failures, checks); return 1; }
    printf("all %d unit checks passed\n", checks);
    return 0;
}
