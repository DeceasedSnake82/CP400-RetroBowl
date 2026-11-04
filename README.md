# Pocket Gridiron for the CP400

An original arcade American-football game for the Casio fx-CP400 / ClassPad II, written for
[Hollyhock 3](https://github.com/classpaddev/hollyhock-3). Copy **`PocketGridiron.hh3`** to the calculator and start it from the launcher.

**Hold the calculator sideways** (keypad on the right): the game uses the screen in landscape, 528 x 320.

You coach and quarterback a team through a season. You play every offensive snap yourself; the opponent's
drives are simulated from both teams' ratings. Win games, earn coach points, upgrade your squad and win the
championship. Every team, sprite and screen is original.

## What is in it

* **12 fictional teams**, an 11-week round-robin season, standings, and a 4-team playoff (semi-final, championship game)
* **Three play calls**: QUICK (short routes), DEEP (long routes), RUN (handoff). Receivers run randomised routes from a playbook
  (slant, out, hitch, curl, drag, go, post, corner, dig, seam, flat, wheel...)
* Man coverage with reaction lag, a deep safety, blitzing linebackers, linemen who block, then shed and rush
* Lead passes with QB accuracy, pressure, contested catches, drops, deflections and interceptions
* Ball carriers run forward on their own: you steer and **juke** past tacklers. Broken tackles, sacks, safeties
* **Kick meter** for field goals and extra points, punts, 4th-down decisions, game clock, half-time and overtime
* **Upgrades** for QB, RB, WR, O-line and defense (levels 1-10); the league gets better every second season
* Season and game in progress saved to `\\fls0\PocketGridiron.sav` (you can leave in the middle of a game and resume)

## Controls

| Action | Touch | Keys |
| --- | --- | --- |
| Call a play | QUICK / DEEP / RUN buttons | `1` / `2` / `3` |
| 4th down: punt / field goal | PUNT / FG buttons | `4` / `5` |
| Throw | tap a receiver | `1`-`4` (the number shown over each receiver) |
| Move QB / steer the runner | hold a finger where you want to go | arrow keys (rotated with the screen: physical UP = left, RIGHT = up) |
| Juke | tap the runner | `EXE` |
| Kick | tap | `EXE` when the needle is in the green zone |
| Pause / save & exit | II button (top right) | `CLEAR` |
| Menus | buttons | `EXE` play, `1` upgrades, `2` standings, `=` setup, `.` help, `CLEAR` back |

## Game speed

The Hollyhock SDK has no timer, so the game counts frames by default. In **SETUP**:

* **FRAME timer** (default): every frame advances the game by *TICKS PER FRAME* x 1/120 s. 4 is right for 30 frames per second.
  If the game feels slow on your calculator, raise it; if it feels too fast, lower it.
* **RTC timer** (experimental): reads the CPU's 128 Hz real-time clock counter (`0xA413FEC0`, the same as Pulse Dash), so the game
  always runs at real speed. The game checks that the counter moves before switching and falls back to the FRAME timer if it stops.
  **Not tested on a real calculator yet.**
* **DIFFICULTY**: EASY / NORMAL / HARD changes the opponents' defense and offense.

## Optimisation notes

* No floating point anywhere (`make fpucheck` fails the build if soft-float helpers are linked). Positions are Q12 fixed-point yards.
* The field is drawn by copying one of 10 pre-built 320-pixel rows per screen column (one logical column is one contiguous VRAM
  row in landscape), chosen from a precomputed column-type table: the background costs 528 row copies per frame.
* Sprites are pre-processed into column runs at start-up and drawn straight into VRAM; the 16 players are depth-sorted with an insertion sort.
* Menus, play calling and result cards draw once and then block in `GetInput` until something happens (no busy loop, saves battery).
* The rules are pure C++ (no SDK), so a bot can play thousands of games per second on a PC for balancing.

## Building

You need the Hollyhock 3 toolchain and SDK; the `ghcr.io/classpaddev/hollyhock-3` image has both.

```sh
export SDK_DIR=/sdk        # default
make                       # -> PocketGridiron.elf and PocketGridiron.hh3
make fpucheck              # fails if soft-float code was linked (the CPU has no FPU)
```

GitHub Actions (`.github/workflows/build.yml`) runs the PC tests, builds `PocketGridiron.hh3` in that image and commits it back to the branch.

## Tests (run on the PC, not on the calculator)

```sh
make -C test test SDK_DIR=/path/to/hollyhock-3/sdk     # unit tests + bot matches + bot seasons
make -C test bot BOT_ARGS="1000 7"                     # 1000 bot matches with seed 7, prints balance stats
make -C test shots SDK_DIR=...                         # runs the real UI headlessly, screenshots in test/out/
```

* `test/test_game.cpp`: plays, downs and distance, kicks, clock, overtime, round-robin schedule, standings, upgrades, save format.
* `test/bot.cpp`: a bot that only uses the human controls (`pg::Input`) plays matches and seasons and fails CI on degenerate
  results (scores, stuck plays, players off the field, matches that never end).
* `test/host_app.cpp` runs `main.cpp` unchanged against a fake LCD and scripted input (`test/script.txt`).

## Source layout

| File | |
| --- | --- |
| `game.h/.cpp` | rules: players, routes, coverage, blocking, passing, tackling, match flow, opponent drives, season (no SDK) |
| `render.h/.cpp`, `art.h` | game state to pixels: field templates, sprites, HUD, menu widgets; ASCII-art sprites |
| `gfx.h/.cpp` | landscape drawing on the portrait framebuffer, 5x7 font |
| `save.h/.cpp` | settings, season and match file (record format is pure and unit-tested) |
| `main.cpp` | screens, input, timing, main loop |
| `cp400.md` | field guide for writing CP400 games (SDK, hardware, performance, testing) |
