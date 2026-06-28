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
CSRCS += ./src/leds.c
CSRCS += ./src/fonts/lv_font_montserrat_14.c
CSRCS += ./src/fonts/lv_font_montserrat_16.c
CSRCS += ./src/fonts/lv_font_montserrat_18.c
CSRCS += ./src/fonts/lv_font_montserrat_20.c
CSRCS += ./src/opnsense.c
CSRCS += ./src/touch.c
CSRCS += ./src/api.c
CSRCS += ./src/button.c

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

# Auto-generated header dependencies (from -MMD): changing a .h rebuilds every
# .o that includes it. Without this, growing a struct in a header left stale
# .o files with mismatched layouts (e.g. api_port read at the wrong offset).
DEPS = $(OBJS:$(OBJEXT)=.d)
-include $(DEPS)

all: default fand

%.o: %.c
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
	@echo "CC $<"

default: $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $(BIN) $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)
	@echo "Built $(BIN)"

# Standalone fan monitor/control daemon (libc only — no LVGL/DRM/curl).
# Bundles an optional web dashboard (fand_api.c + the shared stat collectors);
# pthread for the API thread. Still no external libs, so it stays static-link-safe.
FAND = ug-fand
FAND_SRC = src/ug_fand.c src/fand_api.c src/system_stats.c src/net_stats.c src/disk_stats.c
fand: $(FAND)
$(FAND): $(FAND_SRC) include/fand_api.h include/version.h
	$(CC) -O2 -g0 -Wall -Wextra -Iinclude -pthread -o $(FAND) $(FAND_SRC)
	@echo "Built $(FAND)"

# Host-only mockup renderer: GUI + LVGL with fake data, no DRM/curl needed.
LVGL_OBJS = $(filter-out ./src/%,$(COBJS))
MOCKOBJ = ./test/render_mock.o

FONTOBJS = ./src/fonts/lv_font_montserrat_14.o ./src/fonts/lv_font_montserrat_16.o \
           ./src/fonts/lv_font_montserrat_18.o ./src/fonts/lv_font_montserrat_20.o

mock: $(LVGL_OBJS) ./src/gui.o ./src/i18n.o ./src/settings.o ./src/leds.o ./src/system_stats.o $(FONTOBJS) $(MOCKOBJ)
	$(CC) -o render-mock $(MOCKOBJ) ./src/gui.o ./src/i18n.o ./src/settings.o ./src/leds.o ./src/system_stats.o $(FONTOBJS) $(LVGL_OBJS) -lm -lpthread $(shell pkg-config --libs libdrm)
	@echo "Built render-mock"

install: default
	sudo cp $(BIN) /usr/bin/$(BIN)

clean:
	rm -f $(BIN) $(FAND) render-mock $(AOBJS) $(COBJS) $(MAINOBJ) $(MOCKOBJ) $(DEPS) $(MOCKOBJ:$(OBJEXT)=.d)

run: default
	./$(BIN)

.PHONY: all default fand clean install run
