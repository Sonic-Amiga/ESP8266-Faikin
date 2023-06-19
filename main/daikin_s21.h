#ifndef _DAIKIN_S21_H
#define _DAIKIN_S21_H

#include <stdint.h>

// Some common S21 definitions

#define	STX	2
#define	ETX	3
#define	ENQ	5
#define	ACK	6
#define	NAK	21

// Calculate packet checksum
static inline uint8_t s21_checksum(uint8_t *buf, int len)
{
   uint8_t c = 0;

   // The checksum excludes STX, checksum field itself, and ETX
   for (int i = 1; i < len - 2; i++)
      c += buf[i];

   // Sees checksum of 03 actually sends as 05 in order not to clash with ETX
   return (c == ACK) ? ENQ : c;
}

#endif
