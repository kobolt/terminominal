#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "terminal.h"
#include "eia.h"
#include "error.h"

#define PARAM_MAX 8
#define PARAM_LEN 12

#define ROW_MAX_HARD 24
#define COL_MAX_HARD 132

typedef enum {
  ESCAPE_NONE   = 0,
  ESCAPE_START  = 1,
  ESCAPE_CSI    = 2,
  ESCAPE_HASH   = 3,
  ESCAPE_G0_SET = 4,
  ESCAPE_G1_SET = 5,
} escape_t;



static terminal_char_t screen[ROW_MAX_HARD][COL_MAX_HARD];
static bool screen_changed[ROW_MAX_HARD][COL_MAX_HARD];
static bool tab_stop[COL_MAX_HARD];

static int cursor_row;
static int cursor_col;
static int margin_top;
static int margin_bottom;
static uint8_t cursor_print_attribute;
static bool cursor_outside_scroll;

static uint8_t current_g0_set;
static uint8_t current_g1_set;

static int saved_row;
static int saved_col;
static uint8_t saved_print_attribute;

static escape_t escape;
static char param[PARAM_MAX][PARAM_LEN];
static int param_index;
static bool param_used;

static bool mode_cursor_key_app    = false;
static bool mode_ansi              = true;
static bool mode_column_132        = false;
static bool mode_scrolling_smooth  = false;
static bool mode_screen_reverse    = false;
static bool mode_origin_relative   = false;
static bool mode_wraparound        = false;
static bool mode_auto_repeat       = false;
static bool mode_interlace         = false;
static bool mode_keypad_app        = false;
static bool mode_line_feed         = false;



static inline int col_max(void)
{
  return (mode_column_132) ? 131 : 79; /* 0-Indexed */
}

static inline int row_max(void)
{
  return ROW_MAX_HARD - 1;
}



static inline void param_reset(void)
{
  param_index = 0;
  param_used = false;
  for (int i = 0; i < PARAM_MAX; i++) {
    param[i][0] = '\0';
  }
}

static inline void param_append(char c)
{
  int len = 0;
  while (param[param_index][len] != '\0') {
    len++;
  }
  if (len >= (PARAM_LEN - 1)) {
    error_log("Overflow on parameter length!\n");
    return;
  }
  param[param_index][len] = c;
  param[param_index][len+1] = '\0';
}



static inline void tab_stop_clear(int col)
{
  /* Clear All */
  if (col == -1) {
    for (int i = 0; i < COL_MAX_HARD; i++) {
      tab_stop[i] = false;
    }
    return;
  }

  /* Clear Single */
  if (col > col_max()) {
    return;
  }
  tab_stop[col] = false;
}

static inline void tab_stop_set(int col)
{
  if (col > col_max()) {
    return;
  }
  tab_stop[col] = true;
}

static inline void tab_stop_default(void)
{
  for (int i = 8; i < COL_MAX_HARD; i += 8) {
    tab_stop[i] = true;
  }
}



static inline void screen_set(int row, int col, terminal_char_t c)
{
  if (screen[row][col].byte != c.byte ||
      screen[row][col].attribute != c.attribute) {
    screen[row][col] = c;
    screen_changed[row][col] = true;
  }
}



static inline terminal_char_t screen_get(int row, int col)
{
  return screen[row][col];
}



static inline void erase_in_char(uint8_t row, uint8_t col)
{
  terminal_char_t c;

  if (col > col_max()) {
    return;
  }
  if (row > row_max()) {
    return;
  }

  c.byte = ' ';
  c.attribute = 0;

  screen_set(row, col, c);
}



static void erase_in_line(int p)
{
  int col;

  if (p == 0) {
    /* Erase from the active position to the end of the line, inclusive. */
    for (col = cursor_col; col <= col_max(); col++) {
      erase_in_char(cursor_row, col);
    }

  } else if (p == 1) {
    /* Erase from the start of the line to the active position, inclusive. */
    for (col = 0; col <= cursor_col; col++) {
      erase_in_char(cursor_row, col);
    }

  } else if (p == 2) {
    /* Erase all of the line, inclusive. */
    for (col = 0; col <= col_max(); col++) {
      erase_in_char(cursor_row, col);
    }
  }
}



static void erase_in_display(int p)
{
  int row, col;

  if (p == 0) {
    /* Erase from the active position to the end of the screen, inclusive. */
    for (row = cursor_row + 1; row <= row_max(); row++) {
      for (col = 0; col <= col_max(); col++) {
        erase_in_char(row, col);
      }
    }
    erase_in_line(0);

  } else if (p == 1) {
    /* Erase from start of the screen to the active position, inclusive. */
    for (row = 0; row < cursor_row; row++) {
      for (col = 0; col <= col_max(); col++) {
        erase_in_char(row, col);
      }
    }
    erase_in_line(1);

  } else if (p == 2) {
    /* Erase all of the display. */
    for (row = 0; row <= row_max(); row++) {
      for (col = 0; col <= col_max(); col++) {
        erase_in_char(row, col);
      }
    }
  }
}



static void scroll_up(void)
{
  int row, col;
  for (row = margin_top + 1; row < (margin_bottom + 1); row++) {
    for (col = 0; col <= col_max(); col++) {
      screen_set(row - 1, col, screen_get(row, col));
    }
  }
  cursor_row = margin_bottom;
  erase_in_line(2);
}

static void scroll_down(void)
{
  int row, col;
  for (row = margin_bottom; row > margin_top; row--) {
    for (col = 0; col <= col_max(); col++) {
      screen_set(row, col, screen_get(row - 1, col));
    }
  }
  cursor_row = margin_top;
  erase_in_line(2);
}



static inline void print_char(uint8_t byte)
{
  terminal_char_t c;

  /* Handle cursor wrapping. */
  if (cursor_col > col_max()) {
    if (mode_wraparound) {
      cursor_col = 0;
      cursor_row++;
      if (cursor_row > row_max()) {
        cursor_row = row_max();
      }
    } else {
      cursor_col = col_max();
    }
  }

  c.byte = byte;
  c.attribute = cursor_print_attribute;

  screen_set(cursor_row, cursor_col, c);
  if (byte == ' ' && cursor_col == col_max()) {
    /* Don't move cursor if printing a space at the right margin. */
  } else {
    cursor_col++;
  }
}



static inline void screen_alignment_display(void)
{
  terminal_char_t c;
  int row, col;

  c.byte = 'E';
  c.attribute = 0;

  for (row = 0; row <= row_max(); row++) {
    for (col = 0; col <= col_max(); col++) {
      screen_set(row, col, c);
    }
  }
}



static inline void cursor_activate(void)
{
  terminal_char_t c;

  if (cursor_col > col_max()) {
    return;
  }
  if (cursor_row > row_max()) {
    return;
  }

  c = screen_get(cursor_row, cursor_col);
  c.attribute |= (0x1 << TERMINAL_ATTRIBUTE_REVERSE);
  c.attribute |= (0x1 << TERMINAL_ATTRIBUTE_BLINK);
  screen_set(cursor_row, cursor_col, c);
}



static inline void cursor_deactivate(void)
{
  terminal_char_t c;

  if (cursor_col > col_max()) {
    return;
  }
  if (cursor_row > row_max()) {
    return;
  }

  c = screen_get(cursor_row, cursor_col);
  c.attribute &= ~(0x1 << TERMINAL_ATTRIBUTE_REVERSE);
  c.attribute &= ~(0x1 << TERMINAL_ATTRIBUTE_BLINK);
  screen_set(cursor_row, cursor_col, c);
}



void terminal_init(void)
{
  cursor_row = 0;
  cursor_col = 0;
  cursor_print_attribute = 0;
  cursor_outside_scroll = false;

  current_g0_set = 0;
  current_g1_set = 0;

  saved_row = 0;
  saved_col = 0;
  saved_print_attribute = 0;

  escape = ESCAPE_NONE;

  margin_top = 0;
  margin_bottom = row_max();

  for (int row = 0; row < ROW_MAX_HARD; row++) {
    for (int col = 0; col < COL_MAX_HARD; col++) {
      screen[row][col].byte = ' ';
      screen[row][col].attribute = 0;
      screen_changed[row][col] = true;
    }
  }

  tab_stop_default();

  cursor_activate();
}



void terminal_handle_escape_csi(uint8_t byte)
{
  int param_int, i;

  switch (byte) {
  case 'A': /* CUU - Cursor Up */
    param_int = (param_used) ? atoi(param[0]) : 1;
    param_int = (param_int == 0) ? 1 : param_int; /* Convert zero to one. */
    if (param_int > (margin_top + cursor_row)) {
      cursor_row = margin_top;
    } else {
      cursor_row -= param_int;
    }
    escape = ESCAPE_NONE;
    break;

  case 'B': /* CUD - Cursor Down */
    param_int = (param_used) ? atoi(param[0]) : 1;
    param_int = (param_int == 0) ? 1 : param_int; /* Convert zero to one. */
    if (param_int > (margin_bottom - cursor_row)) {
      cursor_row = margin_bottom;
    } else {
      cursor_row += param_int;
    }
    escape = ESCAPE_NONE;
    break;

  case 'C': /* CUF - Cursor Forward */
    param_int = (param_used) ? atoi(param[0]) : 1;
    param_int = (param_int == 0) ? 1 : param_int; /* Convert zero to one. */
    if (param_int > (col_max() - cursor_col)) {
      cursor_col = col_max();
    } else {
      cursor_col += param_int;
    }
    escape = ESCAPE_NONE;
    break;

  case 'D': /* CUB - Cursor Backward */
    param_int = (param_used) ? atoi(param[0]) : 1;
    param_int = (param_int == 0) ? 1 : param_int; /* Convert zero to one. */
    if (param_int > cursor_col) {
      cursor_col = 0;
    } else {
      cursor_col -= param_int;
    }
    escape = ESCAPE_NONE;
    break;

  case 'c': /* DA - Device Attributes */
    eia_send(0x1B);
    eia_send('[');
    eia_send('?');
    eia_send('1');
    eia_send(';');
    eia_send('0'); /* No options */
    eia_send('c');
    escape = ESCAPE_NONE;
    break;

  case 'g': /* TBC - Tabulation Clear */
    param_int = (param_used) ? atoi(param[0]) : 0;
    if (param_int == 0) {
      tab_stop_clear(cursor_col);
    } else if (param_int == 3) {
      tab_stop_clear(-1);
    }
    escape = ESCAPE_NONE;
    break;

  case 'h': /* SM - Set Mode */
    for (i = 0; i <= param_index; i++) {
      if ((param[i][0]        == '?') && (param[i][1] == '1')) {
        mode_cursor_key_app = true;

      } else if ((param[i][0] == '?') && (param[i][1] == '3')) {
        mode_column_132 = true;
        erase_in_display(2);
        cursor_row = margin_top;
        cursor_col = 0;

      } else if ((param[i][0] == '?') && (param[i][1] == '4')) {
        mode_scrolling_smooth = true;

      } else if ((param[i][0] == '?') && (param[i][1] == '5')) {
        mode_screen_reverse = true;

      } else if ((param[i][0] == '?') && (param[i][1] == '6')) {
        mode_origin_relative = true;
        cursor_row = margin_top;
        cursor_col = 0;

      } else if ((param[i][0] == '?') && (param[i][1] == '7')) {
        mode_wraparound = true;

      } else if ((param[i][0] == '?') && (param[i][1] == '8')) {
        mode_auto_repeat = true;

      } else if ((param[i][0] == '?') && (param[i][1] == '9')) {
        mode_interlace = true;

      } else if ((param[i][0] == '2') && (param[i][1] == '0')) {
        mode_line_feed = true;
      }
    }
    escape = ESCAPE_NONE;
    break;

  case 'l': /* RM - Reset Mode */
    for (i = 0; i <= param_index; i++) {
      if ((param[i][0]        == '?') && (param[i][1] == '1')) {
        mode_cursor_key_app = false;

      } else if ((param[i][0] == '?') && (param[i][1] == '2')) {
        mode_ansi = false;

      } else if ((param[i][0] == '?') && (param[i][1] == '3')) {
        mode_column_132 = false;
        erase_in_display(2);
        cursor_row = margin_top;
        cursor_col = 0;

      } else if ((param[i][0] == '?') && (param[i][1] == '4')) {
        mode_scrolling_smooth = false;

      } else if ((param[i][0] == '?') && (param[i][1] == '5')) {
        mode_screen_reverse = false;

      } else if ((param[i][0] == '?') && (param[i][1] == '6')) {
        mode_origin_relative = false;
        cursor_row = margin_top;
        cursor_col = 0;

      } else if ((param[i][0] == '?') && (param[i][1] == '7')) {
        mode_wraparound = false;

      } else if ((param[i][0] == '?') && (param[i][1] == '8')) {
        mode_auto_repeat = false;

      } else if ((param[i][0] == '?') && (param[i][1] == '9')) {
        mode_interlace = false;

      } else if ((param[i][0] == '2') && (param[i][1] == '0')) {
        mode_line_feed = false;
      }
    }
    escape = ESCAPE_NONE;
    break;

  case 'm': /* SGR - Select Graphic Rendition */
    for (i = 0; i <= param_index; i++) {
      if (param[i][0] == '0') {
        cursor_print_attribute = 0;
      } else if (param[i][0] == 0x00) {
        cursor_print_attribute = 0;
      } else if (param[i][0] == '1') {
        cursor_print_attribute |= (0x1 << TERMINAL_ATTRIBUTE_BOLD);
      } else if (param[i][0] == '4') {
        cursor_print_attribute |= (0x1 << TERMINAL_ATTRIBUTE_UNDERLINE);
      } else if (param[i][0] == '5') {
        cursor_print_attribute |= (0x1 << TERMINAL_ATTRIBUTE_BLINK);
      } else if (param[i][0] == '7') {
        cursor_print_attribute |= (0x1 << TERMINAL_ATTRIBUTE_REVERSE);
      }
    }
    escape = ESCAPE_NONE;
    break;

  case 'r': /* DECSTBM - Set Top and Bottom Margins */
    margin_top =    ((param_used)      ? atoi(param[0]) : 1) - 1;
    margin_bottom = ((param_index > 0) ? atoi(param[1]) : row_max() + 1) - 1;
    margin_top =    (margin_top < 0)    ? 0 : margin_top;
    margin_bottom = (margin_bottom < 0) ? 0 : margin_bottom;
    cursor_row = margin_top;
    cursor_col = 0;
    escape = ESCAPE_NONE;
    break;

  case 'f': /* HVP - Horizontal and Vertical Position */
  case 'H': /* CUP - Cursor Position */
    cursor_row = ((param_used)      ? atoi(param[0]) : 1) - 1;
    cursor_col = ((param_index > 0) ? atoi(param[1]) : 1) - 1;
    cursor_row = (cursor_row < 0) ? 0 : cursor_row; /* Negative to zero. */
    cursor_col = (cursor_col < 0) ? 0 : cursor_col; /* Negative to zero. */

    if (mode_origin_relative) {
      cursor_row += margin_top; /* Compensate for different origin. */
      if (cursor_row < margin_top) {
        cursor_row = margin_top;
      } else if (cursor_row > margin_bottom) {
        cursor_row = margin_bottom;
      }
    } else {
      if (cursor_row < margin_top) {
        cursor_outside_scroll = true;
      } else if (cursor_row > margin_bottom) {
        cursor_outside_scroll = true;
      }
    }

    if (cursor_row > row_max()) {
      cursor_row = row_max();
    } else if (cursor_row < 0) {
      cursor_row = 0;
    }
    if (cursor_col > col_max()) {
      cursor_col = col_max();
    } else if (cursor_col < 0) {
      cursor_col = 0;
    }
    escape = ESCAPE_NONE;
    break;

  case 'J': /* ED - Erase In Display */
    param_int = (param_used) ? atoi(param[0]) : 0;
    erase_in_display(param_int);
    escape = ESCAPE_NONE;
    break;

  case 'K': /* EL - Erase In Line */
    param_int = (param_used) ? atoi(param[0]) : 0;
    erase_in_line(param_int);
    escape = ESCAPE_NONE;
    break;

  case ';':
    param_index++;
    if (param_index >= PARAM_MAX) {
      error_log("Overflow on parameter!\n");
      param_index--;
    }
    break;

  /* Parameter */
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case '?':
    param_append((char)byte);
    param_used = true;
    break;

  case 0x08: /* BS inside CSI sequence. */
    if (cursor_col > 0) {
      cursor_col--;
    }
    break;

  case 0x0B: /* VT inside CSI sequence. */
    if (cursor_row < row_max()) {
      cursor_row++;
    }
    break;

  case 0x0D: /* CR inside CSI sequence. */
    cursor_col = 0;
    break;

  default:
    error_log("Unhandled CSI escape code: 0x%02x\n", byte);
    escape = ESCAPE_NONE;
    break;
  }
}



void terminal_handle_escape_hash(uint8_t byte)
{
  switch (byte) {
  case '8': /* DECALN - Screen Alignment Display */
    screen_alignment_display();
    escape = ESCAPE_NONE;
    break;

  default:
    error_log("Unhandled hash escape code: 0x%02x\n", byte);
    escape = ESCAPE_NONE;
    break;
  }
}



void terminal_handle_escape(uint8_t byte)
{
  if (escape == ESCAPE_CSI) {
    terminal_handle_escape_csi(byte);

  } else if (escape == ESCAPE_HASH) {
    terminal_handle_escape_hash(byte);

  } else if (escape == ESCAPE_G0_SET) {
    current_g0_set = byte;
    escape = ESCAPE_NONE;

  } else if (escape == ESCAPE_G1_SET) {
    current_g1_set = byte;
    escape = ESCAPE_NONE;

  } else {
    switch (byte) {
    case '[':
      escape = ESCAPE_CSI;
      param_reset();
      break;

    case '#':
      escape = ESCAPE_HASH;
      param_reset();
      break;

    case '(':
      escape = ESCAPE_G0_SET;
      break;

    case ')':
      escape = ESCAPE_G1_SET;
      break;

    case '=': /* DECKPAM - Keypad Application Mode */
      mode_keypad_app = true;
      escape = ESCAPE_NONE;
      break;

    case '>': /* DECKPNM - Keypad Numeric Mode */
      mode_keypad_app = false;
      escape = ESCAPE_NONE;
      break;

    case '<': /* VT52 - Enter ANSI Mode */
      mode_ansi = true;
      escape = ESCAPE_NONE;
      break;

    case '7': /* DECSC - Save Cursor */
      cursor_row = saved_row;
      cursor_col = saved_col;
      cursor_print_attribute = saved_print_attribute;
      escape = ESCAPE_NONE;
      break;

    case '8': /* DECRC - Restore Cursor */
      saved_row = cursor_row;
      saved_col = cursor_col;
      saved_print_attribute = cursor_print_attribute;
      escape = ESCAPE_NONE;
      break;

    case 'D': /* IND - Index */
      cursor_row++;
      escape = ESCAPE_NONE;
      break;

    case 'E': /* NEL - Next Line */
      cursor_row++;
      cursor_col = 0;
      escape = ESCAPE_NONE;
      break;

    case 'H': /* HTS -  Horizontal Tabulation Set */
      tab_stop_set(cursor_col);
      escape = ESCAPE_NONE;
      break;

    case 'M': /* RI - Reverse Index */
      cursor_row--;
      escape = ESCAPE_NONE;
      break;

    case 'c': /* RIS - Reset To Initial State */
      terminal_init();
      escape = ESCAPE_NONE;
      break;

    default:
      error_log("Unhandled escape code: 0x%02x\n", byte);
      escape = ESCAPE_NONE;
      break;
    }
  }
}



void terminal_handle_byte(uint8_t byte)
{
  cursor_deactivate();

  if (escape != ESCAPE_NONE) {
    terminal_handle_escape(byte);

  } else {
    switch (byte) {
    case 0x1B: /* ESC */
      escape = ESCAPE_START;
      break;

    case 0x07: /* BEL */
      /* Ringing the bell is not implemented. */
      break;

    case 0x08: /* BS */
      if (cursor_col > 0) {
        cursor_col--;
      }
      break;

    case 0x09: /* HT */
      while (! tab_stop[cursor_col]) {
        cursor_col++;
        if (cursor_col > col_max()) {
          cursor_col = col_max();
          break;
        }
      }
      break;

    case 0x0A: /* LF */
    case 0x0B: /* VT */
    case 0x0C: /* FF */
      cursor_row++;
      if (mode_line_feed) {
        cursor_col = 0;
      }
      break;

    case 0x0D: /* CR */
      cursor_col = 0;
      break;

    case 0x0E: /* SO */
      /* Select G1 character set is not implemented. */
      break;

    case 0x0F: /* SI */
      /* Select G0 character set is not implemented. */
      break;

    case 0x7F: /* DEL */
      /* Ignored. */
      break;

    default:
      print_char(byte);
      break;
    }
  }

  /* Handle scrolling. */
  if (cursor_outside_scroll) {
    if (cursor_row >= margin_top && cursor_row <= margin_bottom) {
      cursor_outside_scroll = false;
    }
  }
  if (! cursor_outside_scroll) {
    if (cursor_row > margin_bottom) {
      scroll_up();
    } else if (cursor_row < margin_top) {
      scroll_down();
    }
  }

  cursor_activate();
}



bool terminal_char_changed(uint8_t row, uint8_t col)
{
  if ((screen[row][col].attribute >> TERMINAL_ATTRIBUTE_BLINK) & 0x1) {
    return true;
  }
  return screen_changed[row][col];
}



terminal_char_t terminal_char_get(uint8_t row, uint8_t col)
{
  terminal_char_t c;
  c.byte = '.';
  c.attribute = 0;

  if (row > row_max()) {
    return c;
  } else if (col > col_max()) {
    return c;
  } else {
    screen_changed[row][col] = false;
    return screen_get(row, col);
  }
}



uint8_t terminal_cursor_key_code(void)
{
  if (mode_ansi) {
    if (mode_cursor_key_app) {
      return 'O';
    } else {
      return '[';
    }
  } else {
    return 0;
  }
}



bool terminal_send_crlf(void)
{
  if (mode_line_feed) {
    return true;
  } else {
    return false;
  }
}



