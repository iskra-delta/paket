/*
   Detects the Partner display board without modifying its state. The GDP
   model has an SCN2674 status register at 39h; an unimplemented port on the
   serial-terminal Partner reads as FFh.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/platform.h"
#include "paket/partner_io.h"

#include <stdint.h>

#if defined(PAKET_CPM)
#define SCN2674_STATUS_PORT 0x39U
#endif

paket_partner_type paket_detect_partner_type(void)
{
#if defined(PAKET_CPM)
    if (paket_port_in(SCN2674_STATUS_PORT) != 0xffU) {
        return PAKET_PARTNER_GDP;
    }
#endif
    return PAKET_PARTNER_CRT;
}
