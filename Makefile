UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	DEFINES += -D_POSIX_C_SOURCE=199309L -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700
	OBJECTS += platform_linux
endif

ifeq ($(UNAME_S),Darwin)
	OBJECTS += platform_macos
endif

VELVET_VERSION=$(shell git describe --tags --always)
PREFIX ?= /usr/local
INSTALL_BIN = $(PREFIX)/bin
INSTALL_MAN = $(PREFIX)/share/man
INSTALL_VELVET = $(PREFIX)/share/velvet
INSTALL_LUA = $(INSTALL_VELVET)/lua
INSTALL_BIN2 = $(INSTALL_VELVET)/bin

GEN_DIR = gen
GEN_LUA_AUTOGEN = $(GEN_DIR)/velvet_lua_autogen.c
GEN_C_HEADER = $(GEN_DIR)/velvet_api.h
GEN_LUA_SPEC = lua/velvet/spec.lua
GEN_LUA_GENERATOR = lua/velvet/api_gen.lua

LUA_VERSION = lua-5.5.0
LUA_DIR = deps/$(LUA_VERSION)
LUA_LIBS = $(LUA_DIR)/src/liblua.a
LUA_INCLUDE = $(LUA_DIR)/src/
LUA = $(LUA_DIR)/src/lua

BUILD ?= release

INCLUDE_DIR = -I$(abspath .)/include -I$(abspath .)/deps -I$(LUA_INCLUDE) -I$(abspath .)/$(GEN_DIR)
DEBUG_DIR ?= debug
RELEASE_DIR ?= release
COMMANDS = vv test throughput
CMD_DIR = cmd

DEBUG_CMD_OBJECTS  = $(patsubst $(CMD_DIR)/%.c, $(DEBUG_DIR)/%.c.o, $(COMMANDS:%=$(CMD_DIR)/%.c))
RELEASE_CMD_OBJECTS  = $(patsubst $(CMD_DIR)/%.c, $(RELEASE_DIR)/%.c.o, $(COMMANDS:%=$(CMD_DIR)/%.c))
DEBUG_CMD_OUT = $(DEBUG_CMD_OBJECTS:%.c.o=%)
RELEASE_CMD_OUT = $(RELEASE_CMD_OBJECTS:%.c.o=%)

CMD_DEPS = $(DEBUG_CMD_OBJECTS:.o=.d) $(RELEASE_CMD_OBJECTS:.o=.d)

UTF8PROC = deps/utf8proc/libutf8proc.a
SUBMODULE_INIT = deps/utf8proc/utf8proc.c

BUILD_DEPS = $(UTF8PROC) $(LUA_LIBS)

OBJECTS += velvet utils collections vte text csi csi_dispatch screen osc dcs io velvet_scene velvet_input velvet_cmd velvet_lua velvet_api velvet_alloc platform_unix
OBJECT_DIR = src

DEBUG_OBJECT_OUT = $(patsubst $(OBJECT_DIR)/%.c, $(DEBUG_DIR)/%.c.o, $(OBJECTS:%=$(OBJECT_DIR)/%.c))
RELEASE_OBJECT_OUT = $(patsubst $(OBJECT_DIR)/%.c, $(RELEASE_DIR)/%.c.o, $(OBJECTS:%=$(OBJECT_DIR)/%.c))

OBJECT_DEPS = $(DEBUG_OBJECT_OUT:.o=.d) $(RELEASE_OBJECT_OUT:.o=.d)

DEFINES += -DVELVET_VERSION='"$(VELVET_VERSION)"'

CFLAGS = -std=c99 -Wall -Wextra $(INCLUDE_DIR)  -MMD -MP $(DEFINES)
LDFLAGS = -lm

DEBUG_CFLAGS = $(CFLAGS) -O0 -g -fsanitize=address
DEBUG_LDFLAGS = $(LDFLAGS) -fsanitize=address

RELEASE_CFLAGS = $(CFLAGS) -O2 -DNDEBUG -DRELEASE_BUILD
RELEASE_LDFLAGS = $(LDFLAGS)

.PHONY: all
all: release

$(DEBUG_DIR)/%.c.o: $(OBJECT_DIR)/%.c $(GEN_LUA_AUTOGEN) | $(SUBMODULE_INIT)
	@mkdir -p $(DEBUG_DIR)
	@echo $(CC) -c $(DEBUG_CFLAGS) $< -o $@ > $@.txt
	$(CC) -c $(DEBUG_CFLAGS) $< -o $@

$(DEBUG_DIR)/%.c.o: $(CMD_DIR)/%.c $(GEN_LUA_AUTOGEN) | $(SUBMODULE_INIT)
	@mkdir -p $(DEBUG_DIR)
	@echo $(CC) -c $(DEBUG_CFLAGS) $< -o $@ > $@.txt
	$(CC) -c $(DEBUG_CFLAGS) $< -o $@

$(DEBUG_DIR)/%: $(DEBUG_DIR)/%.c.o $(DEBUG_OBJECT_OUT) $(BUILD_DEPS)
	@mkdir -p $(DEBUG_DIR)
	@echo $(CC) $(DEBUG_CFLAGS) $^ -o $@ $(DEBUG_LDFLAGS) > $@.txt
	$(CC) $(DEBUG_CFLAGS) $^ -o $@ $(DEBUG_LDFLAGS)

$(RELEASE_DIR)/%.c.o: $(OBJECT_DIR)/%.c $(GEN_LUA_AUTOGEN) | $(SUBMODULE_INIT)
	@mkdir -p $(RELEASE_DIR)
	@echo $(CC) -c $(RELEASE_CFLAGS) $< -o $@ > $@.txt
	$(CC) -c $(RELEASE_CFLAGS) $< -o $@

$(RELEASE_DIR)/%.c.o: $(CMD_DIR)/%.c $(GEN_LUA_AUTOGEN) | $(SUBMODULE_INIT)
	@mkdir -p $(RELEASE_DIR)
	@echo $(CC) -c $(RELEASE_CFLAGS) $< -o $@ > $@.txt
	$(CC) -c $(RELEASE_CFLAGS) $< -o $@

$(RELEASE_DIR)/%: $(RELEASE_DIR)/%.c.o $(RELEASE_OBJECT_OUT) $(BUILD_DEPS)
	@mkdir -p $(RELEASE_DIR)
	@echo $(CC) $(RELEASE_CFLAGS) $^ -o $@ $(RELEASE_LDFLAGS) > $@.txt
	$(CC) $(RELEASE_CFLAGS) $^ -o $@ $(RELEASE_LDFLAGS)

.PHONY: release
release: $(RELEASE_CMD_OUT)

.PHONY: debug
debug: $(DEBUG_CMD_OUT)

.PHONY: clean
clean:
	rm -rf $(GEN_DIR) $(DEBUG_DIR) $(RELEASE_DIR)

.SECONDARY: $(DEBUG_OBJECT_OUT) $(RELEASE_OBJECT_OUT) $(DEBUG_CMD_OBJECTS) $(RELEASE_CMD_OBJECTS)

$(UTF8PROC): $(SUBMODULE_INIT)
	UTF8PROC_DEFINES=-DUTF8PROC_STATIC $(MAKE) -C ./deps/utf8proc

$(LUA_LIBS): $(LUA)

$(SUBMODULE_INIT):
	git submodule init
	git submodule update

$(LUA):
	$(MAKE) -C $(LUA_DIR) all

$(GEN_LUA_AUTOGEN): $(GEN_LUA_SPEC) $(GEN_LUA_GENERATOR) $(LUA)
	$(LUA) $(GEN_LUA_GENERATOR) $(GEN_LUA_SPEC) $(GEN_DIR)
$(GEN_C_HEADER): $(GEN_LUA_SPEC) $(GEN_LUA_GENERATOR) $(LUA)
	$(LUA) $(GEN_LUA_GENERATOR) $(GEN_LUA_SPEC) $(GEN_DIR)

INSTALL_BASH_COMPLETION = $(PREFIX)/share/bash-completion/completions
INSTALL_ZSH_COMPLETION = $(PREFIX)/share/zsh/site-functions

install: release
	mkdir -p $(INSTALL_LUA) $(INSTALL_BIN) $(INSTALL_BIN2) $(INSTALL_MAN)/man1 $(INSTALL_MAN)/man3
	mkdir -p $(INSTALL_BASH_COMPLETION) $(INSTALL_ZSH_COMPLETION)
	install -m 755 $(RELEASE_DIR)/vv $(INSTALL_BIN2)/vv
	ln -sf ../share/velvet/bin/vv $(INSTALL_BIN)/vv
	install -m 644 doc/man1/velvet.1 $(INSTALL_MAN)/man1/
	install -m 644 doc/man3/*.3 $(INSTALL_MAN)/man3/
	install -m 644 shell-completion/bash/vv $(INSTALL_BASH_COMPLETION)/vv
	install -m 644 shell-completion/zsh/_vv $(INSTALL_ZSH_COMPLETION)/_vv
	cp -r lua/velvet $(INSTALL_LUA)/

uninstall:
	rm -f $(INSTALL_BIN)/vv
	rm -f $(INSTALL_MAN)/man1/velvet.1
	rm -f $(INSTALL_MAN)/man3/velvet*.3
	rm -f $(INSTALL_BASH_COMPLETION)/vv
	rm -f $(INSTALL_ZSH_COMPLETION)/_vv
	rm -rf $(INSTALL_VELVET)
	
-include $(OBJECT_DEPS) $(CMD_DEPS)

