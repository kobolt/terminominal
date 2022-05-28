#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "terminal.h"
#include "eia.h"

#ifdef COL_132
#define SDLGUI_WIDTH 1452
#else
#define SDLGUI_WIDTH 880
#endif
#define SDLGUI_HEIGHT 240

#define CHAR_WIDTH  11
#define CHAR_HEIGHT 10

extern uint8_t _binary_char_rom_start[];



static SDL_Window *sdlgui_window = NULL;
static SDL_Renderer *sdlgui_renderer = NULL;
static SDL_Texture *sdlgui_texture = NULL;
static SDL_PixelFormat *sdlgui_pixel_format = NULL;
static Uint32 *sdlgui_pixels = NULL;
static int sdlgui_pixel_pitch = 0;
static Uint32 sdlgui_ticks = 0;



static void sdlgui_exit_handler(void)
{
  if (sdlgui_pixel_format != NULL) {
    SDL_FreeFormat(sdlgui_pixel_format);
  }
  if (sdlgui_texture != NULL) {
    SDL_UnlockTexture(sdlgui_texture);
    SDL_DestroyTexture(sdlgui_texture);
  }
  if (sdlgui_renderer != NULL) {
    SDL_DestroyRenderer(sdlgui_renderer);
  }
  if (sdlgui_window != NULL) {
    SDL_DestroyWindow(sdlgui_window);
  }
  SDL_Quit();
}



int sdlgui_init(void)
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "Unable to initalize SDL: %s\n", SDL_GetError());
    return -1;
  }
  atexit(sdlgui_exit_handler);

  if ((sdlgui_window = SDL_CreateWindow("Terminominal",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    SDLGUI_WIDTH, SDLGUI_HEIGHT, 0)) == NULL) {
    fprintf(stderr, "Unable to set video mode: %s\n", SDL_GetError());
    return -1;
  }

  if ((sdlgui_renderer = SDL_CreateRenderer(sdlgui_window, -1, 0)) == NULL) {
    fprintf(stderr, "Unable to create renderer: %s\n", SDL_GetError());
    return -1;
  }

  if ((sdlgui_texture = SDL_CreateTexture(sdlgui_renderer, 
    SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
    SDLGUI_WIDTH, SDLGUI_HEIGHT)) == NULL) {
    fprintf(stderr, "Unable to create texture: %s\n", SDL_GetError());
    return -1;
  }

  if (SDL_LockTexture(sdlgui_texture, NULL,
    (void **)&sdlgui_pixels, &sdlgui_pixel_pitch) != 0) {
    fprintf(stderr, "Unable to lock texture: %s\n", SDL_GetError());
    return -1;
  }

  if ((sdlgui_pixel_format = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888))
    == NULL) {
    fprintf(stderr, "Unable to create pixel format: %s\n", SDL_GetError());
    return -1;
  }

  return 0;
}



static inline void sdlgui_set_pixel(uint16_t y, uint16_t x, uint8_t shade)
{
  sdlgui_pixels[(y * SDLGUI_WIDTH) + x] = 
    SDL_MapRGB(sdlgui_pixel_format, shade, shade, shade);
}



static inline uint8_t sdlgui_shade(bool on, terminal_char_t c)
{
  if (on ^ ((c.attribute >> TERMINAL_ATTRIBUTE_REVERSE) & 0x1)) {
    if (((c.attribute >> TERMINAL_ATTRIBUTE_BLINK) & 0x1) && 
      ((sdlgui_ticks % 1000) > 500)) {
      return 0x0;
    } else {
      if ((c.attribute >> TERMINAL_ATTRIBUTE_BOLD) & 0x1) {
        return 0x7F;
      } else {
        return 0xFF;
      }
    }
  } else {
    if (((c.attribute >> TERMINAL_ATTRIBUTE_REVERSE) & 0x1) &&
      (((c.attribute >> TERMINAL_ATTRIBUTE_BLINK) & 0x1) && 
      ((sdlgui_ticks % 1000) > 500))) {
      if ((c.attribute >> TERMINAL_ATTRIBUTE_BOLD) & 0x1) {
        return 0x7F;
      } else {
        return 0xFF;
      }
    } else {
      return 0x0;
    }
  }
}



static inline void sdlgui_char(uint8_t row, uint8_t col, terminal_char_t c)
{
  int y, x;
  uint8_t char_data;
  int offset;
  bool on;

  for (y = 0; y < CHAR_HEIGHT; y++) {
    offset = (c.byte * CHAR_HEIGHT * 2) + (y * 2);

    char_data = _binary_char_rom_start[offset];
    for (x = 0; x < 8; x++) {
      if (((c.attribute >> TERMINAL_ATTRIBUTE_UNDERLINE) & 0x1)
        && y == (CHAR_HEIGHT - 1)) {
        on = true;
      } else {
        on = (char_data >> x) & 0x1;
      }
      sdlgui_set_pixel((row * CHAR_HEIGHT) + y,
        (col * CHAR_WIDTH) + (7 - x),
        sdlgui_shade(on, c));
    }

    char_data = _binary_char_rom_start[offset + 1];
    for (x = 0; x < 3; x++) {
      if (((c.attribute >> TERMINAL_ATTRIBUTE_UNDERLINE) & 0x1)
        && y == (CHAR_HEIGHT - 1)) {
        on = true;
      } else {
        on = (char_data >> x) & 0x1;
      }
      sdlgui_set_pixel((row * CHAR_HEIGHT) + y,
        (col * CHAR_WIDTH) + (2 - x) + 8,
        sdlgui_shade(on, c));
    }
  }
}



void sdlgui_update(void)
{
  int row, col;
  SDL_Event event;

  while (SDL_PollEvent(&event) == 1) {
    switch (event.type) {
    case SDL_QUIT:
      exit(0);
      break;

    case SDL_TEXTINPUT:
      eia_send(event.text.text[0] & 0xFF);
      break;

    case SDL_KEYDOWN:
      switch (event.key.keysym.sym) {
      case SDLK_RETURN:
        eia_send('\n');
        break;

      case SDLK_ESCAPE:
        eia_send(0x1B);
        break;

      case SDLK_BACKSPACE:
        eia_send(0x08);
        break;

      case SDLK_TAB:
        eia_send(0x09);
        break;

      case SDLK_UP:
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('A');
        break;

      case SDLK_LEFT:
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('D');
        break;

      case SDLK_DOWN:
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('B');
        break;

      case SDLK_RIGHT:
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('C');
        break;
      }
      break;
    }
  }

  if (sdlgui_renderer != NULL) {
    SDL_UnlockTexture(sdlgui_texture);

    SDL_RenderCopy(sdlgui_renderer, sdlgui_texture, NULL, NULL);

    if (SDL_LockTexture(sdlgui_texture, NULL,
      (void **)&sdlgui_pixels, &sdlgui_pixel_pitch) != 0) {
      fprintf(stderr, "Unable to lock texture: %s\n", SDL_GetError());
      exit(0);
    }
  }

  /* Force 60 Hz (NTSC) */
  while ((SDL_GetTicks() - sdlgui_ticks) < 16) {
    SDL_Delay(1);
  }

  for (row = 0; row < (SDLGUI_HEIGHT / CHAR_HEIGHT); row++) {
    for (col = 0; col < (SDLGUI_WIDTH / CHAR_WIDTH); col++) {
      if (terminal_char_changed(row, col)) {
        sdlgui_char(row, col, terminal_char_get(row, col));
      }
    }
  }

  if (sdlgui_renderer != NULL) {
    SDL_RenderPresent(sdlgui_renderer);
  }

  sdlgui_ticks = SDL_GetTicks();
}



