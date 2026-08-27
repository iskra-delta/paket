/*
   Minimal Partner hardware access used by PAKET's serial transport.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_PARTNER_IO_H
#define PAKET_PARTNER_IO_H

#include <stdint.h>

#if defined(__XCC__)
#define PAKET_REGISTER_ABI __sdcccall(1)
#else
#define PAKET_REGISTER_ABI
#endif

uint8_t paket_port_in(uint8_t port) PAKET_REGISTER_ABI;
void paket_port_out(uint8_t port, uint8_t value) PAKET_REGISTER_ABI;

/* Milliseconds within the current minute, in the range 0..59999. */
uint16_t paket_partner_timer_ms(void);

#endif
