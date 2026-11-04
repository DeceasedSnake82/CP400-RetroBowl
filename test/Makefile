# Host-side tests (run on your PC / in CI, not on the calculator).
#
#   make -C test test                                   # unit tests + bot seasons
#   make -C test bot BOT_ARGS="1000 7"                  # simulate 1000 matches with seed 7
#   make -C test season                                 # bot plays full seasons
#   make -C test SDK_DIR=/path/to/hollyhock-3/sdk shots # run the real UI headlessly -> test/out/*.png
#
SDK_DIR ?= /sdk
CXX     ?= g++
OUT     ?= out
STD     := -std=gnu++17
WARN    := -Wall -Wextra

GAME_SRC := ../game.cpp
APP_SRC  := host_app.cpp ../main.cpp ../gfx.cpp ../render.cpp ../save.cpp $(GAME_SRC)
APP_FLAGS := -O2 $(WARN) $(STD) -DPG_HOST -Dmain=pg_main -I$(SDK_DIR)/include -I..
HDRS := $(wildcard ../*.h)
BOT_ARGS ?=

.PHONY: test unit bot season shots clean

test: unit bot

unit: $(OUT)/t_game
	$(OUT)/t_game

# Plays many matches with human-like inputs; fails on degenerate scores / stuck plays.
bot: $(OUT)/bot
	$(OUT)/bot $(BOT_ARGS)
	$(OUT)/bot --season

season: $(OUT)/bot
	$(OUT)/bot --season $(BOT_ARGS)

$(OUT)/t_game: test_game.cpp $(GAME_SRC) ../save.cpp $(HDRS) | $(OUT)
	$(CXX) -O2 $(WARN) $(STD) -DPG_HOST -DPG_SAVE_PATH='"out/test.sav"' -I.. -o $@ test_game.cpp $(GAME_SRC) ../save.cpp

$(OUT)/bot: bot.cpp $(GAME_SRC) $(HDRS) | $(OUT)
	$(CXX) -O2 $(WARN) $(STD) -I.. -o $@ bot.cpp $(GAME_SRC)

$(OUT)/app: $(APP_SRC) $(HDRS) | $(OUT)
	$(CXX) $(APP_FLAGS) -o $@ $(APP_SRC)

shots: $(OUT)/app
	rm -f $(OUT)/pocketgridiron.sav $(OUT)/*.ppm
	$(OUT)/app script.txt
	python3 ppm2png.py $(OUT)

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -f $(OUT)/*
