# Build UOOM for the host. No SDK, no ARM toolchain, no watch.
#
# The DOOM source list is extracted from the *same* file the device build uses
# (Software/Apps/UOOM-CMake/doom-sources.cmake) so the two builds cannot drift
# apart -- the usual failure mode of a project with a host harness bolted on.
#
#   make            build
#   make run        boot DOOM and dump frames to host/out/
#   make clean

ROOT    := $(CURDIR)/..
DG      := $(ROOT)/third_party/doomgeneric/doomgeneric
LIBS    := $(ROOT)/Software/Libs
OUT     := $(CURDIR)/out
WADDIR  ?= $(CURDIR)/wad

CC      ?= cc
CSTD    := -std=gnu99
WARN    := -Wall -Wextra
OPT     ?= -O1 -g
INC     := -I$(LIBS)/Header -I$(DG) -I$(CURDIR)

# Applied to DOOM's own sources only.
#   printf      -> the port's logger; upstream calls it ~95 times
#   strcasecmp  -> not reliably present on newlib-nano, and it is in the WAD
#                  lookup hot path, so the port owns an implementation
DOOM_DEFS := \
  -DNORMALUNIX=1 \
  -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
  -Dprintf=uoom_printf \
  -Dstrcasecmp=uoom_strcasecmp -Dstrncasecmp=uoom_strncasecmp

UOOM_DEFS := -DUOOM_ON_HOST=1

# Extra -D flags applied to *both* DOOM and the port, so uoom_config.h knobs
# can be swept from the command line:
#   make EXTRA_DEFS="-DUOOM_ZONE_BYTES=$$((1024*1024))" run
EXTRA_DEFS ?=

DOOM_SRCS := $(sort $(shell sed -n 's|^ *$${DOOM_PATH}/\(.*\.c\)$$|$(DG)/\1|p' \
                      $(ROOT)/Software/Apps/UOOM-CMake/doom-sources.cmake))

PORT_SRCS := \
  $(LIBS)/Sources/uoom_video.c \
  $(LIBS)/Sources/uoom_input.c \
  $(LIBS)/Sources/uoom_text.c \
  $(LIBS)/Sources/uoom_file.c \
  $(LIBS)/Sources/uoom_sys.c \
  $(LIBS)/Sources/uoom_libc.c \
  $(LIBS)/Sources/doomgeneric_uoom.c

HOST_SRCS := $(CURDIR)/host_platform.c $(CURDIR)/main.c

DOOM_OBJS := $(patsubst $(DG)/%.c,$(OUT)/doom/%.o,$(DOOM_SRCS))
PORT_OBJS := $(patsubst $(LIBS)/Sources/%.c,$(OUT)/port/%.o,$(PORT_SRCS))
HOST_OBJS := $(patsubst $(CURDIR)/%.c,$(OUT)/host/%.o,$(HOST_SRCS))

BIN := $(OUT)/uoom-host

all: $(BIN)

$(BIN): $(DOOM_OBJS) $(PORT_OBJS) $(HOST_OBJS)
	@echo "  LD  $@"
	@$(CC) $^ -o $@ -lm

# Without these, editing a header rebuilds nothing and you test a stale binary.
# Found the hard way: a deliberately corrupted uoom_qr.h kept passing its test.
DEPFLAGS = -MMD -MP

# DOOM is 1993 C; warnings there are not this project's business.
$(OUT)/doom/%.o: $(DG)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC  doom/$*.c"
	@$(CC) $(CSTD) $(OPT) -w $(DEPFLAGS) $(INC) $(DOOM_DEFS) $(EXTRA_DEFS) -c $< -o $@

# Our own layers get the warnings turned up.
$(OUT)/port/%.o: $(LIBS)/Sources/%.c
	@mkdir -p $(dir $@)
	@echo "  CC  port/$*.c"
	@$(CC) $(CSTD) $(OPT) $(WARN) $(DEPFLAGS) $(INC) $(UOOM_DEFS) $(EXTRA_DEFS) -c $< -o $@

$(OUT)/host/%.o: $(CURDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC  host/$*.c"
	@$(CC) $(CSTD) $(OPT) $(WARN) $(DEPFLAGS) $(INC) $(UOOM_DEFS) $(EXTRA_DEFS) -c $< -o $@

run: $(BIN)
	@mkdir -p $(OUT)/frames
	$(BIN) --wad $(WADDIR) --frames 400 --dump $(OUT)/frames --every 100

clean:
	rm -rf $(OUT)

-include $(DOOM_OBJS:.o=.d) $(PORT_OBJS:.o=.d) $(HOST_OBJS:.o=.d)

.PHONY: all run clean
