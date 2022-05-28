#ifndef _EIA_H
#define _EIA_H

#include <stdint.h>

void eia_init(void);
void eia_send(uint8_t c);
void eia_update(void);

#endif /* _EIA_H */
