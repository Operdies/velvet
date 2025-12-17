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
PROFILE_TARGET ?= profile
INCLUDE_DIR = -I$(abspath .)/include -I$(abspath .)/control_sequences
OUT_DIR ?= bin
COMMANDS = vv test statusbar keyboard
CMD_DIR = cmd

CMD_OBJECTS  = $(patsubst $(CMD_DIR)/%.c, $(OUT_DIR)/%.c.o, $(COMMANDS:%=$(CMD_DIR)/%.c))
CMD_OUT = $(patsubst %.c.o, %$(BINARY_EXTENSION), $(CMD_OBJECTS))
CMD_DEPS = $(CMD_OBJECTS:.o=.d)

OBJECTS += velvet pty_host utils collections vte text csi csi_dispatch screen osc io velvet_scene velvet_input velvet_cmd
OBJECT_DIR = src
OBJECT_OUT  = $(patsubst $(OBJECT_DIR)/%.c, $(OUT_DIR)/%.c.o, $(OBJECTS:%=$(OBJECT_DIR)/%.c))
OBJECT_DEPS = $(OBJECT_OUT:.o=.d)

CFLAGS = -std=c23 -Wall -Wextra $(INCLUDE_DIR)  -MMD -MP $(DEFINES)
LDFLAGS = 

DEBUG_CFLAGS = -O0 -g -fsanitize=address
DEBUG_LDFLAGS = -fsanitize=address

RELEASE_CFLAGS = -O3 -flto -mtune=native -march=native -DNDEBUG -DRELEASE_BUILD
RELEASE_LDFLAGS = -flto

PROFILE_CFLAGS = -fprofile-instr-generate -fcoverage-mapping
PROFILE_LDFLAGS = 

ifeq ($(BUILD),debug)
	CFLAGS += $(DEBUG_CFLAGS)
	LDFLAGS += $(DEBUG_LDFLAGS)
	# @true: noop which does not get printed
	STRIP = @true
	OUT_DIR = bin
endif
ifeq ($(BUILD),release)
	CFLAGS += $(RELEASE_CFLAGS)
	LDFLAGS += $(RELEASE_LDFLAGS)
	STRIP = strip
	OUT_DIR = release
	RELEASE_TARGET = hack
endif
ifeq ($(BUILD),profile)
	CFLAGS += $(RELEASE_CFLAGS) $(PROFILE_CFLAGS)
	LDFLAGS += $(RELEASE_LDFLAGS) $(PROFILE_LDFLAGS)
	STRIP = @true
	OUT_DIR = profile
	PROFILE_TARGET = hack
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


.PHONY: $(PROFILE_TARGET)
$(PROFILE_TARGET):
	@$(MAKE) BUILD=profile all

.PHONY: $(RELEASE_TARGET)
$(RELEASE_TARGET):
	@$(MAKE) BUILD=release all

-include $(OBJECT_DEPS) $(CMD_DEPS)

