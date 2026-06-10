CC ?= gcc
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}

CFLAGS ?= -O2 -g0 -DLV_CONF_INCLUDE_SIMPLE -I$(LVGL_DIR)/ -Iinclude/ \
	-Wall -Wextra \
	-Wno-unused-function \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-Wno-sign-compare \
	$(shell pkg-config --cflags libdrm) \
	$(shell pkg-config --cflags libcurl)

LDFLAGS ?= -lm -lpthread $(shell pkg-config --libs libdrm) $(shell pkg-config --libs libcurl)

BIN = ug-paneld
BUILD_DIR = build

# Project source files
MAINSRC = ./src/main.c

CSRCS += ./src/display.c
CSRCS += ./src/system_stats.c
CSRCS += ./src/net_stats.c
CSRCS += ./src/disk_stats.c
CSRCS += ./src/pve_stats.c
CSRCS += ./src/gpu_stats.c
CSRCS += ./src/gui.c
CSRCS += ./src/i18n.c
CSRCS += ./src/settings.c
CSRCS += ./src/config.c
CSRCS += ./src/backlight.c
CSRCS += ./src/opnsense.c
CSRCS += ./src/touch.c
CSRCS += ./src/api.c

# Include LVGL build files (v9 includes drivers)
include $(LVGL_DIR)/lvgl/lvgl.mk

# Clear Arm
ASRCS :=

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

# Host-only mockup renderer: GUI + LVGL with fake data, no DRM/curl needed.
LVGL_OBJS = $(filter-out ./src/%,$(COBJS))
MOCKOBJ = ./test/render_mock.o

mock: $(LVGL_OBJS) ./src/gui.o ./src/i18n.o ./src/settings.o $(MOCKOBJ)
	$(CC) -o render-mock $(MOCKOBJ) ./src/gui.o ./src/i18n.o ./src/settings.o $(LVGL_OBJS) -lm -lpthread $(shell pkg-config --libs libdrm)
	@echo "Built render-mock"

install: default
	sudo cp $(BIN) /usr/bin/$(BIN)

clean:
	rm -f $(BIN) render-mock $(AOBJS) $(COBJS) $(MAINOBJ) $(MOCKOBJ)

run: default
	./$(BIN)

.PHONY: all default clean install run
