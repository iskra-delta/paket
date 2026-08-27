/*
   Wrap-safe RTC time calculations used by PAKET's serial transport.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_TIMEBASE_H
#define PAKET_TIMEBASE_H

#include <stdint.h>

typedef struct {
    uint16_t last_ms;
    uint16_t remainder_ms;
    uint8_t tick;
} paket_timebase;

/* Difference between two timer_ms() readings, whose range is 0..59999. */
uint16_t paket_elapsed_ms(uint16_t start, uint16_t now);

/* Initialize and advance an 8-bit 20 ms protocol clock from the Partner RTC. */
void paket_timebase_reset(paket_timebase *clock, uint16_t now);
uint8_t paket_timebase_advance(paket_timebase *clock, uint16_t now);

#endif
