#include "display.h"
#include "lv_drv_conf.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DISP_BUF_SIZE (128 * 1024)

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[DISP_BUF_SIZE];
static lv_disp_drv_t disp_drv;

static SDL_Window *sdl_window;
static SDL_Renderer *sdl_renderer;
static SDL_Texture *sdl_texture;
static uint32_t *tft_fb;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t hres = drv->hor_res;
    int32_t vres = drv->ver_res;

    if (area->x2 < 0 || area->y2 < 0 || area->x1 > hres - 1 || area->y1 > vres - 1) {
        lv_disp_flush_ready(drv);
        return;
    }

    uint32_t w = lv_area_get_width(area);
    for (int32_t y = area->y1; y <= area->y2 && y < vres; y++) {
        memcpy(&tft_fb[y * hres + area->x1], color_p, w * sizeof(uint32_t));
        color_p += w;
    }

    if (lv_disp_flush_is_last(drv)) {
        SDL_UpdateTexture(sdl_texture, NULL, tft_fb, hres * sizeof(uint32_t));
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);
    }

    lv_disp_flush_ready(drv);
}

int display_init(int *width, int *height)
{
    lv_init();

    // Force X11 backend with software renderer (bypass GLX)
    // This is the same as mini_screen, and we do this before SDL Init as otherwise application hangs
    setenv("SDL_VIDEODRIVER", "x11", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    sdl_window = SDL_CreateWindow("Custom Display",
                                  0, 0,
                                  SDL_HOR_RES, SDL_VER_RES,
                                  SDL_WINDOW_SHOWN);
    if (!sdl_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RaiseWindow(sdl_window);
    SDL_ShowWindow(sdl_window);

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);
    if (!sdl_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    sdl_texture = SDL_CreateTexture(sdl_renderer,
                                    SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STATIC,
                                    SDL_HOR_RES, SDL_VER_RES);
    if (!sdl_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    tft_fb = malloc(sizeof(uint32_t) * SDL_HOR_RES * SDL_VER_RES);
    if (!tft_fb) {
        fprintf(stderr, "Failed to allocate framebuffer\n");
        return -1;
    }
    memset(tft_fb, 0, SDL_HOR_RES * SDL_VER_RES * sizeof(uint32_t));

    if (width) *width = SDL_HOR_RES;
    if (height) *height = SDL_VER_RES;

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = flush_cb;
    disp_drv.hor_res  = SDL_HOR_RES;
    disp_drv.ver_res  = SDL_VER_RES;

    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    if (!disp) {
        fprintf(stderr, "Failed to register display driver\n");
        return -1;
    }

    return 0;
}

lv_disp_t *display_get(void)
{
    return lv_disp_get_default();
}

void display_render(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) { }
    lv_task_handler();
}

void display_close(void)
{
    free(tft_fb);
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}
