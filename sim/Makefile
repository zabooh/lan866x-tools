# Makefile for the sync-ADC feasibility simulation.
# GNU make compatible. On this Windows box use `mingw32-make` (plain `make` may
# be absent); on Linux/macOS `make` works as-is.
#
#   mingw32-make            build sim(.exe)
#   mingw32-make run        build + run with defaults -> CSVs in this dir
#   mingw32-make clean      remove build artefacts and CSVs
#
# sync_core.{c,h} is the firmware-portable core; the rest is PC-only shell.

CC      = gcc
CSTD    ?= -std=c99
WARN    ?= -Wall -Wextra -Wpedantic
OPT     ?= -O2
CFLAGS  ?= $(CSTD) $(WARN) $(OPT)
LDLIBS  ?= -lm

SRC     := sim_main.c sync_core.c sim_noise.c
HDR     := sync_core.h sim_noise.h sim_config.h
BIN     := sim

ifeq ($(OS),Windows_NT)
  EXE := $(BIN).exe
  RM  := del /Q
else
  EXE := $(BIN)
  RM  := rm -f
endif

all: $(EXE)

$(EXE): $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o $(EXE) $(LDLIBS)

run: $(EXE)
	./$(EXE)

clean:
	-$(RM) $(EXE) run_summary.csv run_timeseries.csv run_pairwise.csv dither_jitter.csv

.PHONY: all run clean
