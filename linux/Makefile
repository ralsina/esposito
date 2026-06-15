CC      := gcc
CFLAGS  := -I include -g -Wall -Wextra
LDFLAGS :=
LIBS    := -lSDL2 -lm

# App to build (relative to linux/ or absolute)
APP     ?= ../apps/hello_world/app.c

# Derive app name and directory from path
APP_DIR := $(dir $(APP))
APP_NAME := $(notdir $(patsubst %/,%,$(APP_DIR)))
APP_BIN  := build/$(APP_NAME)

# All .c files in the app directory (supports multi-file apps)
APP_SRCS := $(wildcard $(APP_DIR)*.c)
APP_OBJS := $(addprefix build/,$(notdir $(APP_SRCS:.c=.o)))

# Emulation source files
EMU_SRCS := src/main.c src/text_mode.c src/graphics_mode.c src/os_core.c src/app_config.c src/hardware.c src/wifi.c
EMU_OBJS := $(EMU_SRCS:.c=.o)

# Shared library dependencies from deps file
DEPS_FILE := $(APP_DIR)deps
DEPS :=
LIB_INCS :=
LIB_SRCS :=
LIB_OBJS :=

ifneq ("$(wildcard $(DEPS_FILE))","")
DEPS := $(shell tr '\n' ' ' < $(DEPS_FILE))
LIB_INCS := $(foreach dep,$(DEPS),-I ../libs/$(dep))
LIB_SRCS := $(foreach dep,$(DEPS),$(wildcard ../libs/$(dep)/*.c))
# Namespace objects as build/lib_<dep>_<file>.o
define lib-obj
build/lib_$(notdir $(patsubst %/,%,$(dir $(1))))_$(notdir $(basename $(1))).o
endef
LIB_OBJS := $(foreach src,$(LIB_SRCS),$(call lib-obj,$(src)))
endif

CFLAGS += $(LIB_INCS)

# Link target
all: $(APP_BIN)

$(APP_BIN): $(EMU_OBJS) $(APP_OBJS) $(LIB_OBJS)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS) $(LDFLAGS)

# Build any .c in the app directory
build/%.o: $(APP_DIR)%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

# Build shared library .c files (namespaced as lib_<dep>_<file>.o)
define lib-rule
build/lib_$(notdir $(patsubst %/,%,$(dir $(1))))_$(notdir $(basename $(1))).o: $(1)
	mkdir -p build
	$$(CC) $$(CFLAGS) -c -o $$@ $$<
endef
$(foreach src,$(LIB_SRCS),$(eval $(call lib-rule,$(src))))

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(APP_BIN) $(EMU_OBJS) build/*.o

run: $(APP_BIN)
	./$(APP_BIN) $(APP_NAME)

.PHONY: all clean run
