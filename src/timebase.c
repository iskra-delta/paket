/*
   Implements the RTC-backed, wrap-safe serial protocol clock.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/timebase.h"

uint16_t paket_elapsed_ms(uint16_t start, uint16_t now)
{
    if (now >= start) {
        return (uint16_t)(now - start);
    }
    return (uint16_t)((60000U - start) + now);
}

void paket_timebase_reset(paket_timebase *clock, uint16_t now)
{
    clock->last_ms = now;
    clock->remainder_ms = 0U;
    clock->tick = 0U;
}

uint8_t paket_timebase_advance(paket_timebase *clock, uint16_t now)
{
    uint16_t accumulated = (uint16_t)(
        clock->remainder_ms + paket_elapsed_ms(clock->last_ms, now)
    );

    clock->last_ms = now;
    clock->tick = (uint8_t)(clock->tick + (uint8_t)(accumulated / 20U));
    clock->remainder_ms = (uint16_t)(accumulated % 20U);
    return clock->tick;
}
