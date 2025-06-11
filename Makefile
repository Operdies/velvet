MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

INCLUDE_DIR = $(abspath .)/include
OUT_DIR = bin
COMMANDS = vv
CMD_DIR = cmd
CC = cc
CMD_OBJECTS  = $(patsubst $(CMD_DIR)/%.c, $(OUT_DIR)/%.c.o, $(COMMANDS:%=$(CMD_DIR)/%.c))
CMD_OUT = $(patsubst %.c.o, %, $(CMD_OBJECTS))
CMD_DEPS = $(CMD_OBJECTS:.o=.d)

OBJECTS = 
OBJECT_DIR = src
OBJECT_OUT  = $(patsubst $(OBJECT_DIR)/%.c, $(OUT_DIR)/%.c.o, $(OBJECTS:%=$(OBJECT_DIR)/%.c))
OBJECT_DEPS = $(OBJECT_OUT:.o=.d)

FRAMEWORKS = 

CFLAGS += -std=c23 -O0 -g -Wall -Wextra -I$(INCLUDE_DIR) -MMD -MP
LDFLAGS += $(FRAMEWORKS)


.PHONY: all
all: $(CMD_OUT) $(OUT_DIR)

$(OUT_DIR)/%.c.o: $(OBJECT_DIR)/%.c | $(OUT_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OUT_DIR)/%.c.o: $(CMD_DIR)/%.c | $(OUT_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OUT_DIR)/%: $(OUT_DIR)/%.c.o $(OBJECT_OUT) | $(OUT_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OUT_DIR):
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf $(OUT_DIR)

.SECONDARY: $(OBJECT_OUT) $(CMD_OBJECTS)

RELEASE_DIR = dist
UNITY_SRC = $(RELEASE_DIR)/unity.c


$(RELEASE_DIR):
	mkdir -p $@

$(UNITY_SRC): $(OBJECT_DIR)/*.c $(CMD_DIR)/vv.c | $(RELEASE_DIR)
	cat $^ > $@

$(RELEASE_DIR)/vv: $(UNITY_SRC) | $(RELEASE_DIR)
	$(CC) $(CFLAGS) $(UNITY_SRC) -o $(RELEASE_DIR)/vv $(LDFLAGS)
	strip $(RELEASE_DIR)/vv

.PHONY: release
release: CFLAGS = -std=c23 -O3 -march=native -mtune=native -flto -ffast-math -DNDEBUG -Wall -Wextra -I$(INCLUDE_DIR)
release: LDFLAGS = -flto $(FRAMEWORKS)
release: $(RELEASE_DIR)/vv

.PHONY: scan
scan:
	scan-build make release

-include $(OBJECT_DEPS) $(CMD_DEPS)

