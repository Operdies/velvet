MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

BINARY_EXTENSION =

ifeq ($(OS),Windows_NT)
	DEFINES += -DWIN32_LEAN_AND_MEAN
	OBJECTS += platform_windows
	BINARY_EXTENSION = .exe
else
	UNAME_S := $(shell uname -s)
	OBJECTS += platform_unix
	ifeq ($(UNAME_S),Linux)
		DEFINES += -D_POSIX_C_SOURCE=199309L -D_DEFAULT_SOURCE
		OBJECTS += platform_linux
	endif

	ifeq ($(UNAME_S),Darwin)
		OBJECTS += platform_macos
	endif
endif


BUILD ?= debug

RELEASE_TARGET ?= release
INCLUDE_DIR = $(abspath .)/include
OUT_DIR ?= bin
COMMANDS = vv test statusbar dump
CMD_DIR = cmd

# Normally I would use CC ?= cc, but there is a bug in gcc causing compilation
# of test.c to fail
CC ?= clang

CMD_OBJECTS  = $(patsubst $(CMD_DIR)/%.c, $(OUT_DIR)/%.c.o, $(COMMANDS:%=$(CMD_DIR)/%.c))
CMD_OUT = $(patsubst %.c.o, %$(BINARY_EXTENSION), $(CMD_OBJECTS))
CMD_DEPS = $(CMD_OBJECTS:.o=.d)

OBJECTS += pane utils collections emulator text csi csi_dispatch grid osc
OBJECT_DIR = src
OBJECT_OUT  = $(patsubst $(OBJECT_DIR)/%.c, $(OUT_DIR)/%.c.o, $(OBJECTS:%=$(OBJECT_DIR)/%.c))
OBJECT_DEPS = $(OBJECT_OUT:.o=.d)

CFLAGS = -std=c23 -Wall -Wextra -I$(INCLUDE_DIR)  -MMD -MP $(DEFINES)
LDFLAGS = 
ifeq ($(BUILD),debug)
	CFLAGS += -O0 -g -fsanitize=address
	LDFLAGS += -fsanitize=address
	# @true: noop which does not get printed
	STRIP = @true
	OUT_DIR = bin
endif
ifeq ($(BUILD),release)
	CFLAGS += -O3 -flto -mtune=native -march=native -DNDEBUG -DRELEASE_BUILD -DASSERTS_UNREACHABLE
	LDFLAGS += -flto
	STRIP = strip
	OUT_DIR = release
	RELEASE_TARGET = hack
endif


.PHONY: all
all: $(CMD_OUT) $(OUT_DIR)

$(OUT_DIR)/%.c.o: $(OBJECT_DIR)/%.c | $(OUT_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OUT_DIR)/%.c.o: $(CMD_DIR)/%.c | $(OUT_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OUT_DIR)/%$(BINARY_EXTENSION): $(OUT_DIR)/%.c.o $(OBJECT_OUT) | $(OUT_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	$(STRIP) $@

$(OUT_DIR):
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf $(OUT_DIR)

.SECONDARY: $(OBJECT_OUT) $(CMD_OBJECTS)

.PHONY: scan scan-release scan-debug
scan-release: 
	scan-build make release
scan-debug: 
	scan-build make all
scan: scan-debug scan-release


.PHONY: $(RELEASE_TARGET)
$(RELEASE_TARGET):
	@$(MAKE) BUILD=release all

-include $(OBJECT_DEPS) $(CMD_DEPS)

