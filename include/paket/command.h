/*
   Declares command-line helpers shared by the CP/M application and hosted
   tests. They implement global serial-port option parsing, ASCII wildcard
   matching, bounded search anchors, and Partner drive/user-area paths.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_COMMAND_H
#define PAKET_COMMAND_H

#include <stddef.h>

/* Maximum native CP/M output path, including its terminating zero. */
#define PAKET_TARGET_SIZE 24U

/* Default CLI serial-port number: SIO1 channel B, the LPT port. */
#define PAKET_SERIAL_PORT_DEFAULT 2U

/* Negotiated libsquid DATA payload offered by PAKET.  Sixteen preserves the
   original wire-v2 frame size; 112 is the largest extended frame.  Keep the
   default DATA payload no larger than the Partner driver's 64-byte receive
   ring; this avoids the reproducible flow-control stall seen with 112-byte
   payloads during long binary transfers. */
#define PAKET_PAYLOAD_MIN 16U
#define PAKET_PAYLOAD_MAX 112U
#define PAKET_PAYLOAD_DEFAULT 64U

typedef enum {
    PAKET_PARITY_NONE,
    PAKET_PARITY_ODD,
    PAKET_PARITY_EVEN
} paket_parity;

typedef struct {
    unsigned int baud_rate;
    paket_parity parity;
    unsigned int stop_bits;
} paket_communication;

typedef struct {
    unsigned int serial_port;
    unsigned int payload_bytes;
    int has_communication;
    paket_communication communication;
} paket_options;

/*
   Extract optional "-p 2|3|4", "-c baud-parity-stop", and
   "-m payload-bytes" pairs from argv. Payload bytes must be 16..112.
   Supported baud rates are 2400, 4800, and 9600; parity is N, O, or E;
   stop bits are 1 or 2. With no -c, has_communication remains false so the
   selected port's default profile is used. The remaining arguments are
   compacted in place and argc is updated. Returns zero on success or -1 for
   a missing, invalid, or repeated option.
*/
int paket_parse_options(
    int *argc,
    char **argv,
    paket_options *options
);

/*
   Return nonzero when pattern contains a '*' or '?' wildcard.
*/
int paket_has_wildcards(const char *pattern);

/*
   Match text against an ASCII, case-insensitive wildcard pattern.
   '*' matches any byte range and '?' matches one byte.
*/
int paket_wildcard_match(const char *pattern, const char *text);

/*
   Copy a useful literal substring from a wildcard pattern into output.
   The result is capped at ten bytes so a SEARCH request fits one current
   libsquid data frame. Returns the number of bytes copied.
*/
size_t paket_search_anchor(
    const char *pattern,
    char *output,
    size_t output_size
);

/*
   Convert a Partner destination into the CP/M libc native path form.

   Parameters:
        destination - A path such as "A:/1/" or
                      "A:/1/LUNATIK.COM".
        package_name - Name used to derive an 8.3 file when destination is
                       a drive/user-area directory.
        format       - Preferred download format/extension.
        file_count   - Values greater than one select a ZIP extension.
        output       - Receives a path such as "A:LUNATIK.COM[1]".
        output_size  - Capacity of output.

   Returns:
        Zero on success or -1 for an invalid drive, user area, file name,
        or output buffer.
*/
int paket_make_target_path(
    const char *destination,
    const char *package_name,
    const char *format,
    unsigned int file_count,
    char *output,
    size_t output_size
);

#endif
