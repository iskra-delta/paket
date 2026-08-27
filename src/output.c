/*
   Implements a deliberately small console formatter. Only the conversions
   used by PAKET are supported: %s, %u, and %lu.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/output.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if defined(PAKET_CPM)
#include <sys/bdos.h>
#else
#include <stdio.h>
#endif

static void output_raw_character(char character)
{
#if defined(PAKET_CPM)
    (void)bdos(C_WRITE, (uint16_t)(uint8_t)character);
#else
    (void)fputc((unsigned char)character, stdout);
#endif
}

void paket_output_character(char character)
{
    if (character == '\n') {
        output_raw_character('\r');
    }
    output_raw_character(character);
}

void paket_output_text(const char *text)
{
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        paket_output_character(*text++);
    }
}

void paket_output_line(const char *text)
{
    paket_output_text(text);
    paket_output_character('\n');
}

static void output_unsigned(uint32_t value)
{
    static const uint32_t divisors[] = {
        1000000000UL,
        100000000UL,
        10000000UL,
        1000000UL,
        100000UL,
        10000UL,
        1000UL,
        100UL,
        10UL,
        1UL
    };
    size_t index = 0U;
    int started = 0;

    for (index = 0U; index < sizeof(divisors) / sizeof(divisors[0]); ++index) {
        unsigned int digit = 0U;

        while (value >= divisors[index]) {
            value -= divisors[index];
            ++digit;
        }
        if (started || (digit != 0U) || (divisors[index] == 1UL)) {
            paket_output_character((char)('0' + digit));
            started = 1;
        }
    }
}

void paket_output_format(const char *format, ...)
{
    va_list arguments;

    if (format == NULL) {
        return;
    }
    va_start(arguments, format);
    while (*format != '\0') {
        if (*format != '%') {
            paket_output_character(*format++);
            continue;
        }
        ++format;
        if (*format == 's') {
            paket_output_text(va_arg(arguments, const char *));
        } else if (*format == 'u') {
            output_unsigned((uint32_t)va_arg(arguments, unsigned int));
        } else if ((*format == 'l') && (format[1] == 'u')) {
            output_unsigned((uint32_t)va_arg(arguments, unsigned long));
            ++format;
        } else if (*format == '%') {
            paket_output_character('%');
        } else {
            paket_output_character('%');
            if (*format != '\0') {
                paket_output_character(*format);
            }
        }
        if (*format != '\0') {
            ++format;
        }
    }
    va_end(arguments);
}
