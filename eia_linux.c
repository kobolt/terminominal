#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include "terminal.h"

#define TTY_DEVICE "/dev/ttyS2"
#define TTY_SPEED 115200



static int tty_fd;



static void exit_handler(void)
{
  close(tty_fd);
}



void eia_init(void)
{
  int result;
  struct termios tio;

  tty_fd = open(TTY_DEVICE, O_RDWR | O_NOCTTY);
  if (tty_fd == -1) {
    fprintf(stderr, "open() failed with errno: %d\n", errno);
    exit(1);
  }

  atexit(exit_handler);

  cfmakeraw(&tio);
  cfsetospeed(&tio, B115200);

  result = ioctl(tty_fd, TCSETS, &tio);
  if (result == -1) {
    fprintf(stderr, "ioctl() failed with errno: %d\n", errno);
    exit(1);
  }
}
 


void eia_send(uint8_t c)
{
  fprintf(stderr, ">>> 0x%02x %c\n", c, isprint(c) ? c : ' ');
  write(tty_fd, &c, 1);
}



void eia_update(void)
{
  uint8_t c;
  int result;
  result = read(tty_fd, &c, 1);
  if (result == 1) {
    fprintf(stderr, "< 0x%02x %c\n", c, isprint(c) ? c : ' ');
    terminal_handle_byte(c);
  }
}



