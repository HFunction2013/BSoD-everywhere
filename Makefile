# BSoD-everywhere
#   make        build
#   make tiny   size-first build (what the CI publishes)
#   make test   render frames headlessly, no window

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Isrc/include -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
LDFLAGS ?=
LIBS    ?= -lm

UNAME_S := $(shell uname -s)

ifeq ($(OS),Windows_NT)
BIN   = bsod.exe
LIBS  = -lgdi32 -luser32
else ifeq ($(UNAME_S),Darwin)
BIN   = bsod
LIBS  = -lm
else
BIN   = bsod
LIBS  = -lX11 -lm
endif

SRC = $(wildcard src/*.c)

all: $(BIN)

$(BIN): $(SRC) src/include/bsod.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LIBS)
	@printf "size: %s bytes\n" "`wc -c < $(BIN)`"

tiny: CFLAGS := -std=c11 -Isrc/include -Os -DNDEBUG -fno-asynchronous-unwind-tables \
                -fno-unwind-tables -fno-stack-protector -fomit-frame-pointer \
                -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
tiny: LDFLAGS := -s
tiny: clean $(BIN)

test: $(BIN)
	./$(BIN) --size 1920x1080 --dump-layout --render ref_1080p.ppm
	./$(BIN) --size 1024x768 --render ref_768p.ppm
	./$(BIN) --size 640x480  --render ref_480p.ppm
	./$(BIN) --size 1920x1080 --render ref_stop.ppm --progress 37 \
	    --stop-code IRQL_NOT_LESS_OR_EQUAL --driver nvlddmkm.sys \
	    --params 0x0000000000000000,0x0000000000000002,0x0000000000000001,0xFFFFF80100000000

clean:
	rm -f $(BIN) *.ppm *.bmp probe probe2 probe.c probe2.c

.PHONY: all tiny test clean
