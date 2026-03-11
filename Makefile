CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS = -lcjson -lcurl

SRC = muse.c transport.c discord.c bot.c links.c cache.c
WIN_SRC = wepoll/wepoll.c
OUT = muse

ifeq ($(OS),Windows_NT)
    SRC += $(WIN_SRC)
    LDFLAGS += -lws2_32 -lregex
    OUT := $(OUT).exe
endif

all: debug

debug: CFLAGS += -ggdb -O0
debug: $(OUT)

san: SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
san: CFLAGS += -g -O1 $(SAN_FLAGS)
san: LDFLAGS += $(SAN_FLAGS)
san: $(OUT)

release: CFLAGS += -O2 -flto -fno-plt -fstack-protector-strong
release: LDFLAGS += -flto -Wl,-O1 -Wl,--as-needed
ifeq ($(OS),Windows_NT)
release: LDFLAGS += -Wl,--dynamicbase -Wl,--nxcompat
else
release: CFLAGS += -fPIE
release: LDFLAGS += -pie -Wl,-z,relro -Wl,-z,now
endif
release: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

format:
	clang-format -i *.c *.h

clean:
	rm -f muse muse.exe

.PHONY: all debug san release clean format