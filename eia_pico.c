#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "pico/util/queue.h"
#include "terminal.h"



void eia_init(void)
{
  gpio_set_function(0, GPIO_FUNC_UART);
  gpio_set_function(1, GPIO_FUNC_UART);

  uart_init(uart0, 115200);

  uart_set_format(uart0, 8, 1, UART_PARITY_NONE); /* 8n1 */
  uart_set_fifo_enabled(uart0, true);
}



int eia_send(uint8_t c)
{
  uart_putc(uart0, c);
}



void eia_update(void)
{
  if (uart_is_readable(uart0)) {
    terminal_handle_byte(uart_getc(uart0));
  }
}



