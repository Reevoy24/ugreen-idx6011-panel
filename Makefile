CC ?= gcc
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}

CFLAGS ?= -O2 -g0 -I$(LVGL_DIR)/ -Iinclude/ \
	-Wall -Wextra \
	-Wno-unused-function \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-Wno-sign-compare \
	$(shell pkg-config --cflags sdl2)

LDFLAGS ?= -lm -lpthread $(shell pkg-config --libs sdl2)

BIN = proxmox_display
BUILD_DIR = build

# Project source files
MAINSRC = ./src/main.c

CSRCS += ./src/display.c
CSRCS += ./src/system_stats.c
CSRCS += ./src/gui.c
CSRCS += ./src/config.c
CSRCS += ./src/backlight.c

# Include LVGL and driver build files
include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

OBJEXT ?= .o

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
MAINOBJ = $(MAINSRC:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS) $(MAINOBJ)

all: default

%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $(BIN) $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)
	@echo "Built $(BIN)"

install: default
	sudo cp $(BIN) /usr/bin/$(BIN)

clean:
	rm -f $(BIN) $(AOBJS) $(COBJS) $(MAINOBJ)

run: default
	./$(BIN)

.PHONY: all default clean install run
