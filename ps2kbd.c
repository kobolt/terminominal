#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "ps2kbd.pio.h"
#include "eia.h"
#include "terminal.h"
#include "error.h"



typedef enum {
  PS2KBD_STATE_IDLE,
  PS2KBD_STATE_BREAK,
  PS2KBD_STATE_EXT,
  PS2KBD_STATE_EXT_BREAK,
  PS2KBD_STATE_PAUSE_1,
  PS2KBD_STATE_PAUSE_2,
  PS2KBD_STATE_PAUSE_3,
  PS2KBD_STATE_PAUSE_4,
  PS2KBD_STATE_PAUSE_5,
  PS2KBD_STATE_PAUSE_6,
  PS2KBD_STATE_PAUSE_7,
  PS2KBD_STATE_PRINT_SCREEN_1,
  PS2KBD_STATE_PRINT_SCREEN_2,
  PS2KBD_STATE_PRINT_SCREEN_BREAK_1,
  PS2KBD_STATE_PRINT_SCREEN_BREAK_2,
  PS2KBD_STATE_PRINT_SCREEN_BREAK_3,
} ps2kbd_state_t;

static ps2kbd_state_t ps2kbd_state = PS2KBD_STATE_IDLE;

static PIO ps2kbd_pio;
static uint ps2kbd_sm;
static uint ps2kbd_prog_offset;

static bool key_pressed[UINT8_MAX + 1];
static bool key_ext_pressed[UINT8_MAX + 1];



#ifdef KEYBOARD_NORWEGIAN
static const int key_to_byte[128] = {
 /*        0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
 /* 0 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9,'|', -1,
 /* 1 */  -1, -1, -1, -1, -1,'q','1', -1, -1, -1,'z','s','a','w','2', -1,
 /* 2 */  -1,'c','x','d','e','4','3', -1, -1,' ','v','f','t','r','5', -1,
 /* 3 */  -1,'n','b','h','g','y','6', -1, -1, -1,'m','j','u','7','8', -1,
 /* 4 */  -1,',','k','i','o','0','9', -1, -1,'.','-','l','ø','p','+', -1,
 /* 5 */  -1, -1,'æ', -1,'å','\\',-1, -1, -1, -1, 13,'¨', -1,'\'',-1, -1,
 /* 6 */  -1,'<', -1, -1, -1, -1,  8, -1, -1,'1', -1,'4','7', -1, -1, -1,
 /* 7 */ '0',',','2','5','6','8', 27, -1, -1,'+','3','-','*','9', -1, -1,
};

static const int shift_key_to_byte[128] = {
 /*        0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f */
 /* 0 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9,'§', -1,
 /* 1 */  -1, -1, -1, -1, -1,'Q','!', -1, -1, -1,'Z','S','A','W','"', -1,
 /* 2 */  -1,'C','X','D','E','¤','#', -1, -1,' ','V','F','T','R','%', -1,
 /* 3 */  -1,'N','B','H','G','Y','&', -1, -1, -1,'M','J','U','/','(', -1,
 /* 4 */  -1,';','K','I','O','=',')', -1, -1,':','_','L','Ø','P','?', -1,
 /* 5 */  -1, -1,'Æ', -1,'Å','`', -1, -1, -1, -1, 13,'^', -1,'*', -1, -1,
 /* 6 */  -1,'>', -1, -1, -1, -1,  8, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 7 */  -1, -1, -1, -1, -1, -1, 27, -1, -1,'+', -1,'-','*', -1, -1, -1,
};

#else /* KEYBOARD_US */

static const int key_to_byte[128] = {
 /*        0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
 /* 0 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9,'`', -1,
 /* 1 */  -1, -1, -1, -1, -1,'q','1', -1, -1, -1,'z','s','a','w','2', -1,
 /* 2 */  -1,'c','x','d','e','4','3', -1, -1,' ','v','f','t','r','5', -1,
 /* 3 */  -1,'n','b','h','g','y','6', -1, -1, -1,'m','j','u','7','8', -1,
 /* 4 */  -1,',','k','i','o','0','9', -1, -1,'.','/','l',';','p','-', -1,
 /* 5 */  -1, -1,'\'',-1,'[','=', -1, -1, -1, -1, 13,']', -1,'\\',-1, -1,
 /* 6 */  -1, -1, -1, -1, -1, -1,  8, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 7 */  -1, -1, -1, -1, -1, -1, 27, -1, -1,'+', -1,'-','*', -1, -1, -1,
};

static const int shift_key_to_byte[128] = {
 /*        0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f */
 /* 0 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9,'~', -1,
 /* 1 */  -1, -1, -1, -1, -1,'Q','!', -1, -1, -1,'Z','S','A','W','@', -1,
 /* 2 */  -1,'C','X','D','E','$','#', -1, -1,' ','V','F','T','R','%', -1,
 /* 3 */  -1,'N','B','H','G','Y','^', -1, -1, -1,'M','J','U','&','*', -1,
 /* 4 */  -1,'<','K','I','O',')','(', -1, -1,'>','?','L',':','P','_', -1,
 /* 5 */  -1, -1,'"', -1,'{','+', -1, -1, -1, -1, 13,'}', -1,'|', -1, -1,
 /* 6 */  -1, -1, -1, -1, -1, -1,  8, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 7 */  -1, -1, -1, -1, -1, -1, 27, -1, -1,'+', -1,'-','*', -1, -1, -1,
};
#endif

static const int ctrl_key_to_byte[128] = {
 /*        0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f */
 /* 0 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 1 */  -1, -1, -1, -1, -1, 17, -1, -1, -1, -1, 26, 19,  1, 23,  0, -1,
 /* 2 */  -1,  3, 24,  4,  5, -1, -1, -1, -1,  0, 22,  6, 20, 18, -1, -1,
 /* 3 */  -1, 14,  2,  8,  7, 25, 30, -1, -1, -1, 13, 10, 21, -1, -1, -1,
 /* 4 */  -1, -1, 11,  9, 15, -1, -1, -1, -1, -1, -1, 12, -1, 16, 31, -1,
 /* 5 */  -1, -1, 28, -1, 27, -1, -1, -1, -1, -1, -1, 29, -1, -1, -1, -1,
 /* 6 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 7 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static const int altgr_key_to_byte[128] = {
 /*        0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f */
 /* 0 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 1 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,'@', -1,
 /* 2 */  -1, -1, -1, -1, -1,'$','£', -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 3 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,'{','[', -1,
 /* 4 */  -1, -1, -1, -1, -1,'}',']', -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 5 */  -1, -1, -1, -1, -1,'\'',-1, -1, -1, -1, -1,'~', -1, -1, -1, -1,
 /* 6 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 /* 7 */  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};



static void ps2kbd_reset_pressed(void)
{
  for (int i = 0; i < (UINT8_MAX + 1); i++) {
    key_pressed[i] = false;
    key_ext_pressed[i] = false;
  }
}



static inline int ps2kbd_parity(uint32_t data)
{
  int ones = 0;
  for (int i = 23; i < 32; i++) {
    if ((data >> i) & 0x1) {
      ones++;
    }
  }
  return ones & 0x1;
}



static void ps2kbd_handle_scancode(uint8_t scancode)
{
  switch (ps2kbd_state) {
  case PS2KBD_STATE_IDLE:
    if (scancode == 0xF0) { /* Break */
      ps2kbd_state = PS2KBD_STATE_BREAK;

    } else if (scancode == 0xE1) { /* Special 'Pause' Key */
      ps2kbd_state = PS2KBD_STATE_PAUSE_1;

    } else { /* Make */
      if (scancode == 0xE0) { /* Extended */
        ps2kbd_state = PS2KBD_STATE_EXT;

      } else {
        key_pressed[scancode] = true;

        switch (scancode) {
        case 0x05: /* F1 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('1');
          eia_send('~');
          break;

        case 0x06: /* F2 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('2');
          eia_send('~');
          break;

        case 0x04: /* F3 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('3');
          eia_send('~');
          break;

        case 0x0C: /* F4 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('4');
          eia_send('~');
          break;

        case 0x03: /* F5 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('5');
          eia_send('~');
          break;

        case 0x0B: /* F6 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('7');
          eia_send('~');
          break;

        case 0x83: /* F7 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('8');
          eia_send('~');
          break;

        case 0x0A: /* F8 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('1');
          eia_send('9');
          eia_send('~');
          break;

        case 0x01: /* F9 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('2');
          eia_send('0');
          eia_send('~');
          break;

        case 0x09: /* F10 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('2');
          eia_send('1');
          eia_send('~');
          break;

        case 0x78: /* F11 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('2');
          eia_send('3');
          eia_send('~');
          break;

        case 0x07: /* F12 */
          eia_send(0x1B);
          eia_send('[');
          eia_send('2');
          eia_send('4');
          eia_send('~');
          break;

#ifdef NUMLOCK_OFF
        case 0x71: /* KP Delete */
          eia_send(0x1B);
          eia_send('[');
          eia_send('3');
          eia_send('~');
          break;

        case 0x70: /* KP Insert */
          eia_send(0x1B);
          eia_send('[');
          eia_send('2');
          eia_send('~');
          break;

        case 0x69: /* KP End */
          eia_send(0x1B);
          eia_send('[');
          eia_send('8');
          eia_send('~');
          break;

        case 0x72: /* KP Down Arrow */
          eia_send(0x1B);
          eia_send('[');
          eia_send('B');
          break;

        case 0x7A: /* KP Page Down */
          eia_send(0x1B);
          eia_send('[');
          eia_send('6');
          eia_send('~');
          break;

        case 0x6B: /* KP Left Arrow */
          eia_send(0x1B);
          eia_send('[');
          eia_send('D');
          break;

        case 0x74: /* KP Right Arrow */
          eia_send(0x1B);
          eia_send('[');
          eia_send('C');
          break;

        case 0x6C: /* KP Home */
          eia_send(0x1B);
          eia_send('[');
          eia_send('7');
          eia_send('~');
          break;

        case 0x75: /* KP Up Arrow */
          eia_send(0x1B);
          eia_send('[');
          eia_send('A');
          break;

        case 0x7D: /* KP Page Up */
          eia_send(0x1B);
          eia_send('[');
          eia_send('5');
          eia_send('~');
          break;
#endif /* NUMLOCK_OFF */

        default:
          if (scancode < 128) {
            if (key_pressed[0x12] || key_pressed[0x59]) { /* Shift */
              if (shift_key_to_byte[scancode] >= 0) {
                eia_send(shift_key_to_byte[scancode]);
                if (shift_key_to_byte[scancode] == '\r' &&
                  terminal_send_crlf()) {
                  eia_send('\n');
                }
              }
            } else if (key_pressed[0x14] || key_ext_pressed[0x14]) { /* Ctrl */
              if (ctrl_key_to_byte[scancode] >= 0) {
                eia_send(ctrl_key_to_byte[scancode]);
              }
            } else if (key_ext_pressed[0x11]) { /* Alt Gr */
              if (altgr_key_to_byte[scancode] >= 0) {
                eia_send(altgr_key_to_byte[scancode]);
              }
            } else {
              if (key_to_byte[scancode] >= 0) {
                eia_send(key_to_byte[scancode]);
                if (key_to_byte[scancode] == '\r' &&
                  terminal_send_crlf()) {
                  eia_send('\n');
                }
              }
            }
          }
          break;
        }
        ps2kbd_state = PS2KBD_STATE_IDLE;
      }
    }
    break;

  case PS2KBD_STATE_BREAK:
    key_pressed[scancode] = false;
    ps2kbd_state = PS2KBD_STATE_IDLE;
    break;

  case PS2KBD_STATE_EXT:
    if (scancode == 0xF0) { /* (Extended) Break */
      ps2kbd_state = PS2KBD_STATE_EXT_BREAK;

    } else if (scancode == 0x12) { /* Special 'Print Screen' Key */
      ps2kbd_state = PS2KBD_STATE_PRINT_SCREEN_1;

    } else {
      key_ext_pressed[scancode] = true;

      switch (scancode) {
      case 0x70: /* Insert */
        eia_send(0x1B);
        eia_send('[');
        eia_send('2');
        eia_send('~');
        break;

      case 0x6C: /* Home */
        eia_send(0x1B);
        eia_send('[');
        eia_send('7');
        eia_send('~');
        break;

      case 0x7D: /* Page Up */
        eia_send(0x1B);
        eia_send('[');
        eia_send('5');
        eia_send('~');
        break;

      case 0x71: /* Delete */
        eia_send(0x1B);
        eia_send('[');
        eia_send('3');
        eia_send('~');
        break;

      case 0x69: /* End */
        eia_send(0x1B);
        eia_send('[');
        eia_send('8');
        eia_send('~');
        break;

      case 0x7A: /* Page Down */
        eia_send(0x1B);
        eia_send('[');
        eia_send('6');
        eia_send('~');
        break;

      case 0x75: /* Up Arrow */
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('A');
        break;

      case 0x6B: /* Left Arrow */
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('D');
        break;

      case 0x72: /* Down Arrow */
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('B');
        break;

      case 0x74: /* Right Arrow */
        eia_send(0x1B);
        if (terminal_cursor_key_code() != 0) {
          eia_send(terminal_cursor_key_code());
        }
        eia_send('C');
        break;

      case 0x4A: /* KP Forward Slash */
        eia_send('/');
        break;

      case 0x5A: /* KP Enter */
        eia_send(0x0D);
        break;
      }
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_EXT_BREAK:
    if (scancode == 0x7C) {
      ps2kbd_state = PS2KBD_STATE_PRINT_SCREEN_BREAK_1;
    } else {
      key_ext_pressed[scancode] = false;
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PAUSE_1:
    if (scancode == 0x14) {
      ps2kbd_state = PS2KBD_STATE_PAUSE_2;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PAUSE_2:
    if (scancode == 0x77) {
      ps2kbd_state = PS2KBD_STATE_PAUSE_3;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PAUSE_3:
    if (scancode == 0xE1) {
      ps2kbd_state = PS2KBD_STATE_PAUSE_4;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PAUSE_4:
    if (scancode == 0xF0) {
      ps2kbd_state = PS2KBD_STATE_PAUSE_5;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PAUSE_5:
    if (scancode == 0x14) {
      ps2kbd_state = PS2KBD_STATE_PAUSE_6;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PAUSE_6:
    if (scancode == 0xF0) {
      ps2kbd_state = PS2KBD_STATE_PAUSE_7;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PAUSE_7:
    if (scancode == 0x77) {
      /* 'Pause' press not implemented. */
    }
    ps2kbd_state = PS2KBD_STATE_IDLE;
    break;

  case PS2KBD_STATE_PRINT_SCREEN_1:
    if (scancode == 0xE0) {
      ps2kbd_state = PS2KBD_STATE_PRINT_SCREEN_2;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PRINT_SCREEN_2:
    if (scancode == 0x7C) {
      /* 'Print Screen' press not implemented. */
    }
    ps2kbd_state = PS2KBD_STATE_IDLE;
    break;

  case PS2KBD_STATE_PRINT_SCREEN_BREAK_1:
    if (scancode == 0xE0) {
      ps2kbd_state = PS2KBD_STATE_PRINT_SCREEN_BREAK_2;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PRINT_SCREEN_BREAK_2:
    if (scancode == 0xF0) {
      ps2kbd_state = PS2KBD_STATE_PRINT_SCREEN_BREAK_3;
    } else {
      ps2kbd_state = PS2KBD_STATE_IDLE;
    }
    break;

  case PS2KBD_STATE_PRINT_SCREEN_BREAK_3:
    if (scancode == 0x12) {
      /* 'Print Screen' release not implemented. */
    }
    ps2kbd_state = PS2KBD_STATE_IDLE;
    break;

  }
}



static void ps2kbd_isr(void)
{
  uint32_t data;

  data = pio_sm_get(ps2kbd_pio, ps2kbd_sm);
  if (ps2kbd_parity(data) == 0) {
    error_ps2_parity++;
    ps2kbd_reset_pressed();

    /* Reset PIO in case bits have shifted. */
    pio_sm_set_enabled(ps2kbd_pio, ps2kbd_sm, false);
    busy_wait_us_32(100);
    ps2kbd_program_init(ps2kbd_pio, ps2kbd_sm, ps2kbd_prog_offset);

  } else {
    ps2kbd_handle_scancode(data >> 23 & 0xFF);
  }

  pio_interrupt_clear(ps2kbd_pio, 0);
}



int ps2kbd_init(void)
{
  ps2kbd_reset_pressed();

  irq_set_exclusive_handler(PIO1_IRQ_0, ps2kbd_isr);
  irq_set_enabled(PIO1_IRQ_0, true);

  ps2kbd_pio = pio1;
  ps2kbd_sm = pio_claim_unused_sm(ps2kbd_pio, true);
  ps2kbd_prog_offset = pio_add_program(ps2kbd_pio, &ps2kbd_program);
  ps2kbd_program_init(ps2kbd_pio, ps2kbd_sm, ps2kbd_prog_offset);

  pio_set_irq0_source_enabled(ps2kbd_pio, pis_interrupt0, true);

  return 0;
}



