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
		DEFINES += -D_POSIX_C_SOURCE=199309L -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700
		OBJECTS += platform_linux
	endif

	ifeq ($(UNAME_S),Darwin)
		OBJECTS += platform_macos
	endif
endif

GEN_DIR = $(OUT_DIR)/gen
LUA_DIST_DIR = $(OUT_DIR)/lua/velvet
GEN_LUA_AUTOGEN = $(GEN_DIR)/velvet_lua_autogen.c
GEN_C_HEADER = $(GEN_DIR)/velvet_api.h
GEN_LUA_SPEC = lua/velvet/spec.lua
GEN_LUA_GENERATOR = lua/velvet/api_gen.lua

LUA_VERSION = lua-5.5.0
LUA_DIR = deps/$(LUA_VERSION)
LUA_LIBS = $(LUA_DIR)/src/liblua.a
LUA_INCLUDE = $(LUA_DIR)/src/
LUA = $(LUA_DIR)/src/lua

BUILD ?= debug

RELEASE_TARGET ?= release
PROFILE_TARGET ?= profile
RELEASE_LTO_TARGET ?= release_lto
INCLUDE_DIR = -I$(abspath .)/include -I$(abspath .)/control_sequences -I$(abspath .)/deps -I$(LUA_INCLUDE) -I$(abspath .)/$(GEN_DIR)
OUT_DIR ?= bin
COMMANDS = vv test keyboard
CMD_DIR = cmd

CMD_OBJECTS  = $(patsubst $(CMD_DIR)/%.c, $(OUT_DIR)/%.c.o, $(COMMANDS:%=$(CMD_DIR)/%.c))
CMD_OUT = $(patsubst %.c.o, %$(BINARY_EXTENSION), $(CMD_OBJECTS))
CMD_DEPS = $(CMD_OBJECTS:.o=.d)

UTF8PROC = deps/utf8proc/libutf8proc.a

DEPS = $(UTF8PROC) $(LUA_LIBS)

OBJECTS += velvet utils collections vte text csi csi_dispatch screen osc io velvet_scene velvet_input velvet_cmd velvet_lua velvet_api
OBJECT_DIR = src
OBJECT_OUT  = $(patsubst $(OBJECT_DIR)/%.c, $(OUT_DIR)/%.c.o, $(OBJECTS:%=$(OBJECT_DIR)/%.c))
OBJECT_DEPS = $(OBJECT_OUT:.o=.d)

CFLAGS = -std=c99 -Wall -Wextra $(INCLUDE_DIR)  -MMD -MP $(DEFINES)
LDFLAGS = -lm

DEBUG_CFLAGS = -O0 -g -fsanitize=address
DEBUG_LDFLAGS = -fsanitize=address

RELEASE_CFLAGS = -Os -mtune=native -march=native -DNDEBUG -DRELEASE_BUILD
RELEASE_LDFLAGS = 

RELEASE_LTO_CFLAGS = -O3 -flto -mtune=native -march=native -DNDEBUG -DRELEASE_BUILD
RELEASE_LTO_LDFLAGS = -flto

PROFILE_CFLAGS = -fprofile-instr-generate -fcoverage-mapping
PROFILE_LDFLAGS = 

ifeq ($(BUILD),debug)
	CFLAGS += $(DEBUG_CFLAGS)
	LDFLAGS += $(DEBUG_LDFLAGS)
	# @true: silenced noop
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
ifeq ($(BUILD),release_lto)
	CFLAGS += $(RELEASE_LTO_CFLAGS)
	LDFLAGS += $(RELEASE_LTO_LDFLAGS)
	STRIP = strip
	OUT_DIR = release_lto
	RELEASE_LTO_TARGET = hack
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

$(OUT_DIR)/%.c.o: $(OBJECT_DIR)/%.c | $(OUT_DIR) $(GEN_C_HEADER)
	@echo $(CC) -c $(CFLAGS) $< -o $@ > $@.txt
	$(CC) -c $(CFLAGS) $< -o $@

$(OUT_DIR)/%.c.o: $(CMD_DIR)/%.c | $(OUT_DIR) $(GEN_LUA_AUTOGEN)
	@echo $(CC) -c $(CFLAGS) $< -o $@ > $@.txt
	$(CC) -c $(CFLAGS) $< -o $@

$(OUT_DIR)/%$(BINARY_EXTENSION): $(OUT_DIR)/%.c.o $(OBJECT_OUT) $(DEPS) | $(OUT_DIR)
	@echo $(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) > $@.txt
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

.PHONY: $(RELEASE_LTO_TARGET)
$(RELEASE_LTO_TARGET):
	@$(MAKE) BUILD=release_lto all

.PHONY: $(PROFILE_TARGET)
$(PROFILE_TARGET):
	@$(MAKE) BUILD=profile all

.PHONY: $(RELEASE_TARGET)
$(RELEASE_TARGET):
	@$(MAKE) BUILD=release all

$(UTF8PROC): 
	UTF8PROC_DEFINES=-DUTF8PROC_STATIC $(MAKE) -C ./deps/utf8proc MAKEFLAGS=

$(LUA_LIBS): $(LUA)

$(LUA):
	$(MAKE) -C $(LUA_DIR) all MAKEFLAGS=

$(GEN_LUA_AUTOGEN): $(GEN_LUA_SPEC) $(GEN_LUA_GENERATOR) $(LUA)
	$(LUA) $(GEN_LUA_GENERATOR) $(GEN_LUA_SPEC) $(GEN_DIR)
$(GEN_C_HEADER): $(GEN_LUA_SPEC) $(GEN_LUA_GENERATOR) $(LUA)
	$(LUA) $(GEN_LUA_GENERATOR) $(GEN_LUA_SPEC) $(GEN_DIR)

-include $(OBJECT_DEPS) $(CMD_DEPS)

