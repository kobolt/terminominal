#include <stdarg.h>
#include <stdio.h>
#include "error.h"

int error_ps2_parity = 0;

void error_log(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

