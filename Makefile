
CC = gcc
CFLAGS = -Iinclude
LDFLAGS = -lcjson -lcurl

SRC = muse.c bot.c
WIN_SRC = wepoll/wepoll.c
OUT = muse

ifeq ($(OS),Windows_NT)
  SRC += $(WIN_SRC)
  LDFLAGS += -lws2_32
endif

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

format:
	clang-format -i *.c *.h

clean:
	rm -f $(OUT){,.exe}

.PHONY: all clean format