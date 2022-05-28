#ifndef _TERMINAL_H
#define _TERMINAL_H

#include <stdint.h>
#include <stdbool.h>

#define TERMINAL_ATTRIBUTE_BOLD      1
#define TERMINAL_ATTRIBUTE_UNDERLINE 2
#define TERMINAL_ATTRIBUTE_BLINK     3
#define TERMINAL_ATTRIBUTE_REVERSE   4

typedef struct terminal_char_s {
  uint8_t byte;
  uint8_t attribute;
} terminal_char_t;

void terminal_init(void);
void terminal_handle_byte(uint8_t byte);
terminal_char_t terminal_char_get(uint8_t row, uint8_t col);
bool terminal_char_changed(uint8_t row, uint8_t col);
uint8_t terminal_cursor_key_code(void);
bool terminal_send_crlf(void);

#endif /* _TERMINAL_H */
