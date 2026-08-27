/*
   Declares the Iskra Delta Partner serial/libsquid adapter used by PAKET.
   The adapter owns one libsquid channel-3 socket and exposes synchronous
   Retro Vault exchanges to the transport-independent client.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_LINK_H
#define PAKET_LINK_H

#include "paket/command.h"
#include <squid_client/base.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int socket_fd;
    uint8_t link_epoch;
    int opened;
} paket_link;

/* Quiesce the optional RTC-to-NMI link for the lifetime of the transient.
   Calling this at main entry prevents an already-rising RTC edge from being
   latched while command parsing is still in progress. */
void paket_link_process_begin(void);
void paket_link_process_end(void);

/*
   Open serial port 2 (SIO1B), 3 (SIO2A), or 4 (SIO2B), then establish squid
   channel 3. A null communication pointer selects that port's default line
   profile. Returns zero on success or -1 on invalid settings/link error.
*/
int paket_link_open(
    paket_link *link,
    unsigned int serial_port_number,
    const paket_communication *communication,
    unsigned int payload_bytes
);

/* Close the channel and restore/release the Partner serial port. */
void paket_link_close(paket_link *link);

/* Return the optimized synchronous service client owned by an open link. */
squid_client_t *paket_link_client(paket_link *link);

#endif
