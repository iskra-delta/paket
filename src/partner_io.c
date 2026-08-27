/*
   Partner RTC access owned by PAKET. The two generic port primitives live
   in z80_io.s so the program does not depend on idp-sdk at build or run time.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/partner_io.h"

#define PARTNER_RTC_THOUSANDTHS 0xa0U
#define PARTNER_RTC_HUNDREDTHS  0xa1U
#define PARTNER_RTC_SECONDS     0xa2U

static uint8_t bcd_to_binary(uint8_t value)
{
    return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0fU));
}

uint16_t paket_partner_timer_ms(void)
{
    uint8_t first_seconds;
    uint8_t second_seconds;
    uint8_t first_hundredths;
    uint8_t second_hundredths;
    uint8_t thousandths;

    do {
        first_seconds = paket_port_in(PARTNER_RTC_SECONDS);
        first_hundredths = paket_port_in(PARTNER_RTC_HUNDREDTHS);
        thousandths = paket_port_in(PARTNER_RTC_THOUSANDTHS);
        second_hundredths = paket_port_in(PARTNER_RTC_HUNDREDTHS);
        second_seconds = paket_port_in(PARTNER_RTC_SECONDS);
    } while ((first_seconds != second_seconds) ||
             (first_hundredths != second_hundredths));

    return (uint16_t)(
        (uint16_t)bcd_to_binary(second_seconds) * 1000U +
        (uint16_t)bcd_to_binary(second_hundredths) * 10U +
        (uint16_t)bcd_to_binary((uint8_t)(thousandths >> 4U))
    );
}
