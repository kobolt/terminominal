#ifndef _ERROR_H
#define _ERROR_H

#include <stdarg.h>

extern int error_ps2_parity;

void error_log(const char *format, ...);

#endif /* _ERROR_H */
