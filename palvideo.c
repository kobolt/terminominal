#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "palvideo.pio.h"
#include "terminal.h"

#define ROW_MAX 24
#define COL_MAX 80
#define CHAR_WIDTH  11
#define CHAR_HEIGHT 10
#define FRAME_SCANLINES 625
#define FRAME_SECTIONS 80

extern uint8_t _binary_char_rom_start[];



static PIO palvideo_pio;
static uint palvideo_sm;
static uint32_t palvideo_frame[FRAME_SCANLINES][FRAME_SECTIONS];
static uint32_t *palvideo_ap = &palvideo_frame[0][0];



static void palvideo_section_prime(uint32_t data)
{
  static int scanline = 0;
  static int section = 0;

  palvideo_frame[scanline][section] = data;

  section++;
  if (section >= FRAME_SECTIONS) {
    section = 0;
    scanline++;
    if (scanline >= FRAME_SCANLINES) {
      scanline = 0;
    }
  }
}



static void palvideo_vsync_pulse_prime(void)
{
  /* 27.3us / 0.05us = 546 */
  for (int i = 0; i < 34; i++) {
    palvideo_section_prime(0x00000000);
  }

  /* 4.7us / 0.05us = 94 */
  palvideo_section_prime(0x05555555); /* Minus 0.1us (2) */
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
}



static void palvideo_equal_pulse_prime(void)
{
  /* 2.3us / 0.05us = 46 */
  palvideo_section_prime(0x00000000);
  palvideo_section_prime(0x00000000);
  palvideo_section_prime(0x00000005); /* Plus 0.1us (2) */

  /* 29.7us / 0.05us = 594 */
  for (int i = 0; i < 37; i++) {
    palvideo_section_prime(0x55555555);
  }
}



static void palvideo_scanline_prime(int scanline)
{
  /* Line Sync - 4.7us / 0.05us = 94 */
  palvideo_section_prime(0x00000000);
  palvideo_section_prime(0x00000000);
  palvideo_section_prime(0x00000000);
  palvideo_section_prime(0x00000000);
  palvideo_section_prime(0x00000000);

  /* Back Porch - 5.6us / 0.05us = 112 */
  palvideo_section_prime(0x00000005);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x5555555A); /* Plus 0.1us (2) */

  /* Left Border */
  palvideo_section_prime(0xAAAAAAAA);
  palvideo_section_prime(0xAAAAAAAA);
  palvideo_section_prime(0xAAAAAAAA);
  palvideo_section_prime(0xAAAAAAAA);
  palvideo_section_prime(0xAAAAAAAA);

  if (scanline >= 42 && scanline < 282) {
    /* Screen Area */
    for (int i = 0; i < 55; i++) {
      palvideo_section_prime(0x55555555);
    }
  } else {
    /* Top and Bottom Border */
    for (int i = 0; i < 55; i++) {
      palvideo_section_prime(0xAAAAAAAA);
    }
  }

  /* Right Border */
  palvideo_section_prime(0xAAAAAAAA);
  palvideo_section_prime(0xAAAAAAAA);
  palvideo_section_prime(0xAAAAAAAA);
  palvideo_section_prime(0xAAAAAAAA);

  /* Front Porch - 1.65us / 0.05 = 33 */
  palvideo_section_prime(0xAAAAAAA9); /* Plus 0.75us (15) */
  palvideo_section_prime(0x55555555);
  palvideo_section_prime(0x55555555);
}



static void palvideo_frame_prime()
{
  /* 1 -> 5 */
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();

  /* 6 -> 310 */
  for (int i = 6; i <= 310; i++) {
    palvideo_scanline_prime(i - 6);
  }

  /* 311 -> 317 */
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_vsync_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();

  /* 318 -> 622 */
  for (int i = 318; i <= 622; i++) {
    palvideo_scanline_prime(i - 318);
  }

  /* 623 -> 625 */
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
  palvideo_equal_pulse_prime();
}



void palvideo_init(void)
{
  uint offset;

  palvideo_frame_prime();

  palvideo_pio = pio0;
  palvideo_sm = pio_claim_unused_sm(palvideo_pio, true);
  offset = pio_add_program(palvideo_pio, &palvideo_program);
  palvideo_program_init(palvideo_pio, palvideo_sm, offset);

  dma_channel_config c0 = dma_channel_get_default_config(0);
  channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
  channel_config_set_read_increment(&c0, true);
  channel_config_set_write_increment(&c0, false);
  channel_config_set_dreq(&c0, DREQ_PIO0_TX0);
  channel_config_set_chain_to(&c0, 1);

  dma_channel_configure(0, &c0,
    &palvideo_pio->txf[palvideo_sm], &palvideo_frame,
    FRAME_SCANLINES * FRAME_SECTIONS, false);

  dma_channel_config c1 = dma_channel_get_default_config(1);
  channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
  channel_config_set_read_increment(&c1, false);
  channel_config_set_write_increment(&c1, false);
  channel_config_set_chain_to(&c1, 0);

  dma_channel_configure(1, &c1,
    &dma_hw->ch[0].read_addr, &palvideo_ap, 1, false);

  dma_channel_start(0);
}



static inline int palvideo_shade(bool on, terminal_char_t c)
{
  if (on ^ ((c.attribute >> TERMINAL_ATTRIBUTE_REVERSE) & 0x1)) {
    if (((c.attribute >> TERMINAL_ATTRIBUTE_BLINK) & 0x1) && 
      ((time_us_32() % 1000000) > 500000)) {
      return 0b01;
    } else {
      if ((c.attribute >> TERMINAL_ATTRIBUTE_BOLD) & 0x1) {
        return 0b10;
      } else {
        return 0b11;
      }
    }
  } else {
    if (((c.attribute >> TERMINAL_ATTRIBUTE_REVERSE) & 0x1) &&
      (((c.attribute >> TERMINAL_ATTRIBUTE_BLINK) & 0x1) && 
      ((time_us_32() % 1000000) > 500000))) {
      if ((c.attribute >> TERMINAL_ATTRIBUTE_BOLD) & 0x1) {
        return 0b10;
      } else {
        return 0b11;
      }
    } else {
      return 0b01;
    }
  }
}



static inline void palvideo_set_pixel(uint16_t row, uint16_t col,
  int y, int x, int shade)
{
  uint32_t mask;
  int shift, scanline, section;

  scanline = ((row * 10) + y) + 5 + 42; /* First interlace. */
  section = (((col * 11) + x) / 16) + 18;

  shift = 30 - ((((col * 11) + x) % 16) * 2);
  mask = 0b11 << shift;
  palvideo_frame[scanline][section] = 
    (palvideo_frame[scanline][section] & ~mask) | ((shade << shift) & mask);

  scanline = ((row * 10) + y) + 317 + 42; /* Second interlace. */
  palvideo_frame[scanline][section] = 
    (palvideo_frame[scanline][section] & ~mask) | ((shade << shift) & mask);
}



static inline void palvideo_char(uint8_t row, uint8_t col, terminal_char_t c)
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
      palvideo_set_pixel(row, col, y, (7 - x),
        palvideo_shade(on, c));
    }

    char_data = _binary_char_rom_start[offset + 1];
    for (x = 0; x < 3; x++) {
      if (((c.attribute >> TERMINAL_ATTRIBUTE_UNDERLINE) & 0x1)
        && y == (CHAR_HEIGHT - 1)) {
        on = true;
      } else {
        on = (char_data >> x) & 0x1;
      }
      palvideo_set_pixel(row, col, y, (2 - x) + 8,
        palvideo_shade(on, c));
    }
  }
}



void palvideo_update(void)
{
  int row, col;

  for (row = 0; row < ROW_MAX; row++) {
    for (col = 0; col < COL_MAX; col++) {
      if (terminal_char_changed(row, col)) {
        palvideo_char(row, col, terminal_char_get(row, col));
      }
    }
  }
}



