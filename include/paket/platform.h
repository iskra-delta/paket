/*
   Detects which Iskra Delta Partner display configuration is running.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_PLATFORM_H
#define PAKET_PLATFORM_H

typedef enum {
    PAKET_PARTNER_CRT,
    PAKET_PARTNER_GDP
} paket_partner_type;

paket_partner_type paket_detect_partner_type(void);

#endif
