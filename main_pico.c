#include <stdint.h>
#include "palvideo.h"
#include "ps2kbd.h"
#include "terminal.h"
#include "eia.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

static void main_core1(void)
{
  while (1) {
    palvideo_update();
  }
}

int main(void)
{
  eia_init();
  terminal_init();
  palvideo_init();
  ps2kbd_init();

  multicore_reset_core1();
  multicore_launch_core1(main_core1);

  while (1) {
    eia_update();
  }

  return 0;
}

