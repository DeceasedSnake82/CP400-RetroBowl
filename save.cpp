#include "save.h"

namespace save {

namespace {
uint16_t checksum(const uint8_t* p, int n) {
    uint16_t a = 0x5A17, b = 0;
    for (int i = 0; i < n; ++i) { a = (uint16_t)(a + p[i]); b = (uint16_t)(b + a); }
    return (uint16_t)(a ^ (b << 3) ^ (b >> 5));
}

void zero(void* p, unsigned n) {
    uint8_t* q = (uint8_t*)p;
    for (unsigned i = 0; i < n; ++i) q[i] = 0;
}

void copy(void* dst, const void* src, unsigned n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (unsigned i = 0; i < n; ++i) d[i] = s[i];
}

constexpr uint16_t SEASON_SIZE = (uint16_t)sizeof(pg::Season);
constexpr uint16_t MATCH_SIZE = (uint16_t)sizeof(pg::Match);
static_assert(sizeof(pg::Season) < 65536 && sizeof(pg::Match) < 65536, "size fields are 16 bit");

bool settingsOk(const Settings& s) {
    return s.pace >= 1 && s.pace <= 8 && s.timer <= 1 && s.showFps <= 1 && s.difficulty <= 2;
}

bool seasonOk(const pg::Season& s) {
    if (s.team >= pg::NTEAMS || s.stage > pg::SS_OUT || s.week > pg::NWEEKS) return false;
    for (int i = 0; i < pg::U_COUNT; ++i)
        if (s.level[i] < 1 || s.level[i] > pg::MAX_LEVEL) return false;
    return true;
}

bool matchOk(const pg::Match& m) {
    return m.user < pg::NTEAMS && m.opp < pg::NTEAMS && m.next <= pg::NX_FINAL && m.playoff <= 1 && m.endPending <= 1;
}
}  // namespace

void defaults(Data& d) {
    zero(&d, sizeof d);
    d.set.pace = 4;
    d.set.timer = 0;
    d.set.showFps = 0;
    d.set.difficulty = 1;
}

void encode(const Data& d, uint8_t out[RECORD_SIZE]) {
    out[0] = 'P'; out[1] = 'G'; out[2] = 'S'; out[3] = VERSION;
    out[4] = (uint8_t)(SEASON_SIZE >> 8); out[5] = (uint8_t)SEASON_SIZE;
    out[6] = (uint8_t)(MATCH_SIZE >> 8);  out[7] = (uint8_t)MATCH_SIZE;
    uint8_t* p = out + HEADER_SIZE;
    p[0] = d.set.pace; p[1] = d.set.timer; p[2] = d.set.showFps; p[3] = d.set.difficulty;
    p[4] = d.hasSeason; p[5] = d.inMatch;
    p += 6;
    copy(p, &d.season, SEASON_SIZE); p += SEASON_SIZE;
    copy(p, &d.match, MATCH_SIZE);
    const uint16_t c = checksum(out, RECORD_SIZE - 2);
    out[RECORD_SIZE - 2] = (uint8_t)(c >> 8);
    out[RECORD_SIZE - 1] = (uint8_t)c;
}

bool decode(const uint8_t in[RECORD_SIZE], Data& d) {
    defaults(d);
    if (in[0] != 'P' || in[1] != 'G' || in[2] != 'S' || in[3] != VERSION) return false;
    if (((in[4] << 8) | in[5]) != SEASON_SIZE || ((in[6] << 8) | in[7]) != MATCH_SIZE) return false;
    const uint16_t c = checksum(in, RECORD_SIZE - 2);
    if (in[RECORD_SIZE - 2] != (uint8_t)(c >> 8) || in[RECORD_SIZE - 1] != (uint8_t)c) return false;
    Data t;
    defaults(t);
    const uint8_t* p = in + HEADER_SIZE;
    t.set.pace = p[0]; t.set.timer = p[1]; t.set.showFps = p[2]; t.set.difficulty = p[3];
    t.hasSeason = p[4]; t.inMatch = p[5];
    p += 6;
    copy(&t.season, p, SEASON_SIZE); p += SEASON_SIZE;
    copy(&t.match, p, MATCH_SIZE);
    if (!settingsOk(t.set) || t.hasSeason > 1 || t.inMatch > 1) return false;
    if (t.hasSeason && !seasonOk(t.season)) return false;
    if (t.inMatch && !matchOk(t.match)) return false;
    d = t;
    return true;
}

}  // namespace save

#ifdef PG_HOST
// PC tests: a plain file.
#include <stdio.h>
#ifndef PG_SAVE_PATH
#define PG_SAVE_PATH "out/pocketgridiron.sav"
#endif

namespace save {

bool load(Data& d) {
    defaults(d);
    FILE* f = fopen(PG_SAVE_PATH, "rb");
    if (!f) return false;
    static uint8_t rec[RECORD_SIZE];
    const size_t n = fread(rec, 1, sizeof rec, f);
    fclose(f);
    return n == sizeof rec && decode(rec, d);
}

bool store(const Data& d) {
    static uint8_t rec[RECORD_SIZE];
    encode(d, rec);
    FILE* f = fopen(PG_SAVE_PATH, "wb");
    if (!f) return false;
    const bool ok = fwrite(rec, 1, sizeof rec, f) == sizeof rec;
    return fclose(f) == 0 && ok;
}

}  // namespace save

#else
#include <sdk/os/file.h>

namespace save {

namespace {
const char* const PATH = "\\\\fls0\\PocketGridiron.sav";     // = \\fls0\PocketGridiron.sav
uint8_t rec[RECORD_SIZE];                                     // static: keep it off the stack
}

bool load(Data& d) {
    defaults(d);
    const int fd = File_Open(PATH, FILE_OPEN_READ);
    if (fd < 0) return false;
    const int n = File_Read(fd, rec, RECORD_SIZE);
    File_Close(fd);
    return n == RECORD_SIZE && decode(rec, d);
}

bool store(const Data& d) {
    encode(d, rec);
    // Fixed-size record: overwriting from the start is enough whether or not the OS truncates.
    int fd = File_Open(PATH, FILE_OPEN_WRITE);
    if (fd < 0) fd = File_Open(PATH, FILE_OPEN_WRITE | FILE_OPEN_CREATE);
    if (fd < 0) return false;
    const int n = File_Write(fd, rec, RECORD_SIZE);
    const int c = File_Close(fd);
    return n == RECORD_SIZE && c >= 0;
}

}  // namespace save
#endif
