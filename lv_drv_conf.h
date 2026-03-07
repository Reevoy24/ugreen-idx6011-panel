/**
 * @file lv_drv_conf.h
 * Configuration file for lv_drivers v8.3
 */

/* clang-format off */
#if 1 /*Set it to "1" to enable the content*/

#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

/*********************
 * DELAY INTERFACE
 *********************/
#define LV_DRV_DELAY_INCLUDE  <unistd.h>
#define LV_DRV_DELAY_US(us)   usleep(us)
#define LV_DRV_DELAY_MS(ms)   usleep(ms * 1000)

/*********************
 * DISPLAY INTERFACE
 *********************/

/*No SPI/parallel display interface needed for framebuffer*/

/*********************
 * INPUT DEVICE INTERFACE
 *********************/

/*No SPI/parallel input interface needed*/

/*-----------------------------------------
 *  Linux frame buffer device (/dev/fbx)
 *-----------------------------------------*/
#ifndef USE_FBDEV
#  define USE_FBDEV           0
#endif

#if USE_FBDEV
#  define FBDEV_PATH              "/dev/fb0"
#  define FBDEV_DISPLAY_POWER_ON  0
#endif

/*-----------------------------------------
 *  FreeBSD frame buffer device (/dev/fbx)
 *-----------------------------------------*/
#ifndef USE_BSD_FBDEV
#  define USE_BSD_FBDEV       0
#endif

/*-----------------------------------------
 *  DRM/KMS device (/dev/dri/card0)
 *-----------------------------------------*/
#ifndef USE_DRM
#  define USE_DRM             1
#endif

#if USE_DRM
#  define DRM_CARD            "/dev/dri/card0"
#  define DRM_CONNECTOR_ID    -1
#endif

/*------------------------------
 *  SDL2
 *------------------------------*/
#ifndef USE_SDL
#  define USE_SDL             0
#endif

#if USE_SDL
#  define SDL_HOR_RES             258
#  define SDL_VER_RES             960
#  define SDL_ZOOM                1
#  define SDL_DOUBLE_BUFFERED     0
#  define SDL_FULLSCREEN          0
#  define SDL_INCLUDE_PATH        <SDL2/SDL.h>
#endif

#ifndef USE_SDL_GPU
#  define USE_SDL_GPU         0
#endif

/*-------------------------------
 *  Mouse or touchpad as evdev
 *  (for Linux based systems)
 *-------------------------------*/
#ifndef USE_EVDEV
#  define USE_EVDEV           0
#endif

#ifndef USE_BSD_EVDEV
#  define USE_BSD_EVDEV       0
#endif

#if USE_EVDEV || USE_BSD_EVDEV
#  define EVDEV_NAME   "/dev/input/event0"
#  define EVDEV_SWAP_AXES         0
#  define EVDEV_CALIBRATE         0
#endif

/*-------------------------------
 *  libinput (for Linux)
 *-------------------------------*/
#ifndef USE_LIBINPUT
#  define USE_LIBINPUT        0
#endif

/*-------------------------------
 *  XKB
 *-------------------------------*/
#ifndef USE_XKB
#  define USE_XKB             0
#endif

#endif /*LV_DRV_CONF_H*/

#endif /*End of "Content enable"*/
