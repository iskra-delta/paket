/*
   Declares PAKET's compact console output helpers. They avoid pulling the
   full stdio formatter into the CP/M transient program.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_OUTPUT_H
#define PAKET_OUTPUT_H

void paket_output_character(char character);
void paket_output_text(const char *text);
void paket_output_line(const char *text);
void paket_output_format(const char *format, ...);

#endif
