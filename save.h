// Settings + season + in-progress match file (\\fls0\PocketGridiron.sav). The record format is
// pure (no SDK) so it can be unit-tested on a PC. A file with a wrong magic, version, struct
// size or checksum, or with out-of-range values, is ignored (the caller gets defaults).
//
// Record layout (all multi-byte header fields big-endian):
//   'P' 'G' 'S' VERSION | sizeof(Season) (16 bit) | sizeof(Match) (16 bit)
//   pace timer showFps difficulty | hasSeason inMatch
//   Season (raw bytes) | Match (raw bytes) | checksum (16 bit)
// Season / Match are POD structs written and read by the same binary, so raw bytes are fine;
// the size fields reject a file written by a build with a different layout.
#pragma once

#include <stdint.h>
#include "game.h"

namespace save {

constexpr uint8_t VERSION = 1;

struct Settings {
    uint8_t pace;           // ticks per frame in frame-timer mode, 1..8 (default 4)
    uint8_t timer;          // 0 = frame based, 1 = RTC
    uint8_t showFps;        // 0 / 1
    uint8_t difficulty;     // 0 easy, 1 normal (default), 2 hard
};

struct Data {
    Settings set;
    uint8_t hasSeason;      // 0 / 1: `season` is a franchise in progress
    uint8_t inMatch;        // 0 / 1: `match` is a game in progress (resume it)
    pg::Season season;
    pg::Match match;
};

constexpr int HEADER_SIZE = 4 + 2 + 2;
constexpr int RECORD_SIZE = HEADER_SIZE + 4 + 2 + (int)sizeof(pg::Season) + (int)sizeof(pg::Match) + 2;

void defaults(Data& d);
void encode(const Data& d, uint8_t out[RECORD_SIZE]);
bool decode(const uint8_t in[RECORD_SIZE], Data& d);   // false: d holds defaults

bool load(Data& d);         // false: no (valid) file, d holds defaults
bool store(const Data& d);  // false: could not write it

}  // namespace save
