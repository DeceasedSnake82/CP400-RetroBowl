// Automated player: plays full matches with pg::Input only (like a human with the keypad) and
// prints averages, so rule / tuning changes can be judged from a terminal and CI catches broken
// rules.
//
//   out/bot [matches] [seed]          exhibition matches across all opponents (default 300)
//   out/bot --season [years] [seed]   one franchise over several seasons (default 6), spending
//                                     coach points on upgrades round-robin
//
// Exit code 1 when things look degenerate: average user points < 3 or > 70, a play running for
// more than 60 s of ticks, players far outside the field, a match or season that never ends.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace pg;

namespace {

uint32_t botRng = 1;                     // the bot's own randomness (separate from the rules)
int rr(int n) { return rndRange(botRng, n); }

int errors = 0;
void fail(const char* what, int a = 0, int b = 0) {
    if (errors < 20) printf("ERROR: %s (%d, %d)\n", what, a, b);
    ++errors;
}

struct Stats {
    long games, wins, ties, userPts, oppPts;
    long plays, yards, passAtt, passComp, tds, ints, sacks, runs, passes;
    long results[8];
    long fgAtt, fgMade, xpAtt, xpMade, punts, goForIt, goForItMade;
    long oppDrives, driveRes[8];
    long ticks, maxTicks;
};
Stats st;

const char* const RES_NAMES[8] = { "none", "tackle", "out of bounds", "incomplete", "touchdown", "interception", "sack", "safety" };
const char* const DRIVE_NAMES[8] = { "TD", "FG", "punt", "downs", "turnover", "missed FG", "end of half", "safety" };

bool onField(const Play& pl) {
    for (int i = 0; i < NP; ++i) {
        const Player& p = pl.p[i];
        if (p.x < -10 * FX1 || p.x > FIELD_LEN + 10 * FX1 || p.y < -10 * FX1 || p.y > FIELD_W + 10 * FX1) return false;
    }
    return true;
}

// Distance from player i to the nearest defender that is still on its feet.
fx nearestDefender(const Play& pl, int i, int* who = nullptr, bool aheadOnly = false) {
    fx best = 0x7FFFFFFF;
    const Player& a = pl.p[i];
    for (int j = NOFF; j < NP; ++j) {
        const Player& d = pl.p[j];
        if (d.state == PS_DOWN) continue;
        if (aheadOnly && d.x < a.x - FX1) continue;
        const fx dd = dist(d.x - a.x, d.y - a.y);
        if (dd < best) { best = dd; if (who) *who = j; }
    }
    return best;
}

int sgn(fx v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

// --- the controller ------------------------------------------------------------
Input control(const Play& pl) {
    Input in = {};
    in.throwTo = -1;
    if (pl.phase == PH_LIVE) {
        const Player& qb = pl.p[P_QB];
        // QB: step away from a rusher who is close
        int rusher = -1;
        const fx rd = nearestDefender(pl, P_QB, &rusher);
        if (rusher >= 0 && rd < 3 * FX1) {
            in.my = (int8_t)(pl.p[rusher].y > qb.y ? -1 : 1);
            in.mx = (int8_t)(pl.p[rusher].x > qb.x ? -1 : 0);
        }
        if (pl.call != CALL_RUN && canThrow(pl) && pl.t >= sec(0.5)) {
            int bestT = -1;
            fx bestD = -1;
            for (int k = 0; k < NTARGETS; ++k) {
                const int r = TARGETS[k];
                const Player& p = pl.p[r];
                if (p.x <= pl.los) continue;                    // only receivers past the line
                fx d = nearestDefender(pl, r);
                if (pl.call == CALL_DEEP) d += (p.x - pl.los) / 8;  // deep call: prefer depth
                if (d > bestD) { bestD = d; bestT = r; }
            }
            const bool open = bestT >= 0 && nearestDefender(pl, bestT) >= 3 * FX1;
            const bool late = pl.t >= sec(2.5) + rr(sec(0.3));
            if (open || late) {
                if (bestT < 0) bestT = TARGETS[rr(NTARGETS)];   // nobody past the line: throw it anyway
                in.throwTo = (int8_t)bestT;
            }
        }
        return in;
    }
    if (pl.phase == PH_AIR) {
        // run the intended receiver to the landing spot
        in.steer = true;
        in.sx = pl.ball.tx;
        in.sy = pl.ball.ty;
        return in;
    }
    if (pl.phase == PH_RUN && pl.carrier >= 0 && isOffense(pl.carrier)) {
        const Player& c = pl.p[pl.carrier];
        in.mx = 1;
        int who = -1;
        const fx d = nearestDefender(pl, pl.carrier, &who, true);
        if (who >= 0 && d < 8 * FX1) {
            const fx dy = pl.p[who].y - c.y;
            if (dy > FX1 * 3 / 2) in.my = 0;
            else if (dy < -FX1 * 3 / 2) in.my = 0;
            else if (dy != 0) in.my = (int8_t)-sgn(dy);
            else in.my = (int8_t)(c.y < MID_Y ? 1 : -1);    // straight at us: cut to the open side
        }
        // stay off the sidelines
        if (c.y < 2 * FX1) in.my = 1;
        else if (c.y > FIELD_W - 2 * FX1) in.my = -1;
        if (who >= 0 && d < FX1 * 3 / 2 && pl.jukeCd == 0) in.juke = true;
    }
    return in;
}

PlayCall pickCall(const Match& m) {
    const int ytg = yardsToGo(m) ? yardsToGo(m) : 100 - ballOnYard(m);
    const int r = rr(100);
    if (ytg <= 3) return r < 50 ? CALL_RUN : r < 90 ? CALL_QUICK : CALL_DEEP;
    if (ytg >= 8) return r < 45 ? CALL_QUICK : r < 80 ? CALL_DEEP : CALL_RUN;
    return r < 40 ? CALL_QUICK : r < 65 ? CALL_DEEP : CALL_RUN;
}

int needle() { return 128 + rr(17) - 8; }

// Plays one user play to the end. Returns false on a fatal problem.
bool playOne(Match& m, PlayCall call) {
    static Play pl;
    snap(m, pl, call);
    int t = 0;
    bool offField = false;
    while (pl.phase != PH_OVER && t < sec(60)) {
        step(pl, control(pl));
        ++t;
        if (!offField && !onField(pl)) { offField = true; fail("player far outside the field", t, pl.phase); }
    }
    if (pl.phase != PH_OVER) { fail("play lasted more than 60 s", call, pl.phase); return false; }
    st.ticks += t;
    if (t > st.maxTicks) st.maxTicks = t;
    ++st.plays;
    st.yards += playYards(pl);
    if (pl.result < 8) ++st.results[pl.result];
    if (pl.thrown) { ++st.passAtt; if (pl.caught) ++st.passComp; }
    if (call == CALL_RUN) ++st.runs; else ++st.passes;
    if (pl.result == RES_TD) ++st.tds;
    if (pl.result == RES_INT) ++st.ints;
    if (pl.result == RES_SACK) ++st.sacks;
    applyPlay(m, pl);
    return !offField;
}

// Plays a full match; returns false if it did not end properly.
bool playMatch(Match& m) {
    int actions = 0;
    while (m.next != NX_FINAL) {
        if (++actions > 3000) { fail("match does not end", m.quarter, m.clock); return false; }
        switch (m.next) {
            case NX_CALL:
                if (m.down == 4) {
                    const int ytg = yardsToGo(m) ? yardsToGo(m) : 100 - ballOnYard(m);
                    const bool fg = fgAllowed(m) && fgDistance(m) <= 45;
                    if (ytg <= 1 && ballOnYard(m) >= 35) { ++st.goForIt; const int d = m.down; playOne(m, CALL_RUN); if (m.down == 1 && d == 4) ++st.goForItMade; }
                    else if (fg) { ++st.fgAtt; const int s = m.score[0]; kickFG(m, needle()); if (m.score[0] > s) ++st.fgMade; }
                    else if (ytg <= 2 && ballOnYard(m) >= 45) { ++st.goForIt; playOne(m, pickCall(m)); if (m.down == 1) ++st.goForItMade; }
                    else { ++st.punts; punt(m); }
                } else if (!playOne(m, pickCall(m))) return false;
                break;
            case NX_XP: { ++st.xpAtt; const int s = m.score[0]; kickXP(m, needle()); if (m.score[0] > s) ++st.xpMade; break; }
            case NX_OPP:
                oppDrive(m);
                ++st.oppDrives;
                if (m.drive.result < 8) ++st.driveRes[m.drive.result];
                break;
            case NX_HALF: continueMatch(m); break;
            default: fail("bad next state", m.next); return false;
        }
    }
    ++st.games;
    st.userPts += m.score[0];
    st.oppPts += m.score[1];
    if (m.score[0] == m.score[1]) ++st.ties;
    else if (userWon(m)) ++st.wins;
    if (m.score[0] < 0 || m.score[1] < 0 || m.score[0] > 200 || m.score[1] > 200) fail("silly score", m.score[0], m.score[1]);
    return true;
}

void report() {
    const double g = st.games ? (double)st.games : 1.0, p = st.plays ? (double)st.plays : 1.0;
    printf("games %ld: user %.1f pts, opponent %.1f pts, wins %.1f %%, ties %.1f %%\n", st.games, st.userPts / g, st.oppPts / g,
           100.0 * st.wins / g, 100.0 * st.ties / g);
    printf("per game: %.1f plays (%.1f runs, %.1f passes), %.2f TDs, %.2f INTs, %.2f sacks, %.2f punts\n", st.plays / g,
           st.runs / g, st.passes / g, st.tds / g, st.ints / g, st.sacks / g, st.punts / g);
    printf("completion %.1f %% (%ld / %ld), %.2f yards per play, avg play %.1f s, longest %.1f s\n",
           st.passAtt ? 100.0 * st.passComp / st.passAtt : 0.0, st.passComp, st.passAtt, st.yards / p,
           st.ticks / p / TICK_HZ, st.maxTicks / (double)TICK_HZ);
    printf("kicks: FG %ld / %ld, XP %ld / %ld; 4th down tries %ld (%ld converted)\n", st.fgMade, st.fgAtt, st.xpMade, st.xpAtt,
           st.goForIt, st.goForItMade);
    printf("play results:");
    for (int i = 1; i < 8; ++i) printf(" %s %.1f%%", RES_NAMES[i], 100.0 * st.results[i] / p);
    printf("\n");
    const double od = st.oppDrives ? (double)st.oppDrives : 1.0;
    printf("opponent drives %.1f per game:", st.oppDrives / g);
    for (int i = 0; i < 8; ++i) printf(" %s %.1f%%", DRIVE_NAMES[i], 100.0 * st.driveRes[i] / od);
    printf("\n");
}

void checkAverages() {
    if (!st.games) { fail("no games played"); return; }
    const double up = st.userPts / (double)st.games, op = st.oppPts / (double)st.games;
    if (up < 3 || up > 70) fail("average user points out of range", (int)up);
    if (op > 70) fail("average opponent points out of range", (int)op);
    if (st.plays < st.games * 4) fail("too few plays per game", (int)(st.plays / st.games));
    if (st.passAtt == 0 || st.passComp == 0) fail("no completed passes");
}

int exhibition(int matches, uint32_t seed) {
    static Season s;
    static Match m;
    for (int g = 0; g < matches; ++g) {
        const int user = g % NTEAMS;
        const int opp = (user + 1 + (g / NTEAMS) % (NTEAMS - 1)) % NTEAMS;
        seasonNew(s, user, seed + (uint32_t)g * 7919u);
        // vary the user's strength a little like a mid-franchise team
        for (int u = 0; u < U_COUNT; ++u) s.level[u] = (uint8_t)(1 + (g + u) % 6);
        matchInit(m, user, opp, userRatings(s, opp), oppOffense(s, opp), s.level[U_DEF], seed * 31u + (uint32_t)g);
        if (!playMatch(m)) break;
    }
    report();
    checkAverages();
    return errors ? 1 : 0;
}

int seasons(int years, uint32_t seed) {
    static Season s;
    static Match m;
    seasonNew(s, (int)(seed % NTEAMS), seed);
    int nextUp = 0;
    long titles = 0;
    for (int y = 0; y < years; ++y) {
        int games = 0;
        while (opponentOf(s) >= 0) {
            if (++games > NWEEKS + 4) { fail("season does not end", s.week, s.stage); break; }
            const int opp = opponentOf(s);
            const bool playoff = s.stage == SS_SEMI || s.stage == SS_FINAL;
            matchInit(m, s.team, opp, userRatings(s, opp), oppOffense(s, opp), s.level[U_DEF], rnd(botRng));
            m.playoff = playoff ? 1 : 0;
            if (!playMatch(m)) return 1;
            if (playoff && m.score[0] == m.score[1]) fail("playoff game ended in a tie", m.score[0], m.score[1]);
            const int stage = s.stage;
            seasonRecord(s, m.score[0], m.score[1], m.tds);
            if (s.stage < stage && !(stage == SS_CHAMPION)) fail("season stage went backwards", stage, s.stage);
            // spend coach points round-robin
            for (int tries = 0; tries < U_COUNT * 4; ++tries) {
                const int c = upgradeCost(s, nextUp);
                if (c > 0 && s.points >= c) { if (!upgrade(s, nextUp)) fail("upgrade refused", nextUp, c); }
                else if (c > 0) break;
                nextUp = (nextUp + 1) % U_COUNT;
            }
        }
        const int t = s.team;
        static const char* const STAGE[] = { "regular", "lost semi-final", "lost final", "CHAMPION", "missed playoffs" };
        const char* how = s.stage == SS_CHAMPION ? STAGE[3] : s.stage == SS_OUT && s.lastStage == SS_SEMI ? STAGE[1]
                        : s.stage == SS_OUT && s.lastStage == SS_FINAL ? STAGE[2] : s.stage == SS_OUT ? STAGE[4] : "?";
        uint8_t order[NTEAMS];
        standings(s, order);
        int rank = 0;
        for (int i = 0; i < NTEAMS; ++i) if (order[i] == t) rank = i + 1;
        printf("year %d %-6s %2d-%2d-%d  pf %4d pa %4d  rank %2d  %-16s levels %d/%d/%d/%d/%d  points %u\n", s.year,
               TEAMS[t].abbr, s.w[t], s.l[t], s.t[t], s.pf[t], s.pa[t], rank, how, s.level[0], s.level[1], s.level[2],
               s.level[3], s.level[4], s.points);
        if (s.stage == SS_CHAMPION) ++titles;
        if (s.w[t] + s.l[t] + s.t[t] != NWEEKS) fail("regular season game count", s.w[t] + s.l[t] + s.t[t]);
        seasonNextYear(s);
    }
    printf("titles %ld in %d years (season counter %u)\n", titles, years, s.titles);
    report();
    checkAverages();
    return errors ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    int a = 1;
    bool seasonMode = false;
    if (argc > 1 && strcmp(argv[1], "--season") == 0) { seasonMode = true; ++a; }
    const int n = argc > a ? atoi(argv[a]) : (seasonMode ? 6 : 300);
    const uint32_t seed = argc > a + 1 ? (uint32_t)strtoul(argv[a + 1], nullptr, 10) : 1u;
    botRng = seed * 2654435761u + 1;
    if (!botRng) botRng = 1;
    const int rc = seasonMode ? seasons(n, seed) : exhibition(n, seed);
    if (rc) printf("bot: %d problem(s) found\n", errors);
    return rc;
}
