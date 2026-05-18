CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
CFLAGS  += $(shell pkg-config --cflags raylib)
LDLIBS  := $(shell pkg-config --libs raylib)

# macOS frameworks (harmless on Linux; pkg-config usually provides them)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LDLIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreFoundation
endif

SRCS = main.c state.c world.c character.c hud.c ipc.c
OBJS = $(SRCS:.c=.o)
BIN  = game_engine

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
