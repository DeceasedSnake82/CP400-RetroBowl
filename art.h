// Pocket Gridiron - ASCII-art sprites (original art). render.cpp turns them into column runs at init.
//
// Every sprite faces RIGHT (it is mirrored for players facing left). The bottom row is the
// feet row (drawn at sy(y)). Legend (palette slots in render.cpp):
//   .  transparent        k  outline              H  helmet (team c2)     h  helmet highlight
//   m  face mask          s  skin                 J  jersey (team c1)     j  jersey shade
//   n  jersey number      P  pants                w  socks                S  shoes
//   B  ball brown         L  ball highlight       W  ball laces (white)
#pragma once

namespace art {

// --- skill players (QB, RB, WR, TE, LB, CB, S): 9 x 14 --------------------------------
static const char* const SKILL_STAND[] = {
    "...kkk...",
    "..kHhHk..",
    ".kHhHHHk.",
    ".kHHHmmk.",
    "..kHsmk..",
    ".kJJJJJk.",
    "kJJnJJJJk",
    "ksJnJJJsk",
    "kskjjjksk",
    ".kPPPPPk.",
    ".kPPkPPk.",
    ".kwk.kwk.",
    ".kwk.kwk.",
    ".kSSkkSSk",
};
static const char* const SKILL_RUN_A[] = {
    "...kkk...",
    "..kHhHk..",
    ".kHhHHHk.",
    ".kHHHmmk.",
    "..kHsmk..",
    ".kJJJJJk.",
    "kJJnJJJJk",
    "ksJnJJJJk",
    "kskjjjjsk",
    ".kPPPPPkk",
    "kPPk.kPPk",
    "kwk...kwk",
    "kSSk..kwk",
    ".kk...kSS",
};
static const char* const SKILL_RUN_B[] = {
    "...kkk...",
    "..kHhHk..",
    ".kHhHHHk.",
    ".kHHHmmk.",
    "..kHsmk..",
    ".kJJJJJk.",
    "kJJnJJJJk",
    "kJJnJJJsk",
    "ksjjjjksk",
    "kkPPPPPk.",
    "kPPk.kPPk",
    "kwk...kwk",
    "kwk..kSSk",
    "kSS...kk.",
};

// --- linemen (OL, DL): 11 x 14, broader shoulders ------------------------------------
static const char* const BIG_STAND[] = {
    "....kkk....",
    "...kHhHk...",
    "..kHhHHHk..",
    "..kHHHmmk..",
    "..kkHsmkk..",
    ".kJJJJJJJk.",
    "kJJJnnJJJJk",
    "kJJJnnJJJJk",
    "ksJJJJJJJsk",
    "kskjjjjjksk",
    ".kPPPPPPPk.",
    ".kPPPkPPPk.",
    ".kwwk.kwwk.",
    ".kSSSkkSSSk",
};
static const char* const BIG_RUN_A[] = {
    "....kkk....",
    "...kHhHk...",
    "..kHhHHHk..",
    "..kHHHmmk..",
    "..kkHsmkk..",
    ".kJJJJJJJk.",
    "kJJJnnJJJJk",
    "kJJJnnJJJJk",
    "ksJJJJJJJJk",
    "kskjjjjjjsk",
    ".kPPPPPPPkk",
    "kPPPk.kPPPk",
    "kSSk...kwwk",
    ".kk....kSSS",
};
static const char* const BIG_RUN_B[] = {
    "....kkk....",
    "...kHhHk...",
    "..kHhHHHk..",
    "..kHHHmmk..",
    "..kkHsmkk..",
    ".kJJJJJJJk.",
    "kJJJnnJJJJk",
    "kJJJnnJJJJk",
    "kJJJJJJJJsk",
    "ksjjjjjjksk",
    "kkPPPPPPPk.",
    "kPPPk.kPPPk",
    "kwwk...kSSk",
    "kSSS....kk.",
};

// --- a player on the ground (tackled / juked), head to the right: 16 x 6 ---------------
static const char* const DOWN[] = {
    "....kkkkksk.kk..",
    "kkk.kPPkJJJJkHHk",
    "SwwkkPPkJnJJkHhm",
    "SwwkkPPkJnJJkHHm",
    "kkk.kPPkjjjjkHHk",
    "....kkkkksk.kk..",
};

// --- the ball: 7 x 4 (in flight / carried) ---------------------------------------------
static const char* const BALL[] = {
    ".kkkkk.",
    "kBLLBBk",
    "kBWWWBk",
    ".kkkkk.",
};

}  // namespace art
