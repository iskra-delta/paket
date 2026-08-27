/*
   Declares the command dispatcher for PAKET.COM. It maps zero, one, or two
   user arguments onto catalog listing/search, metadata display, or download.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_APPLICATION_H
#define PAKET_APPLICATION_H

#include "paket/client.h"

/* Print PAKET command syntax to standard output. */
void paket_print_usage(void);

/*
   Execute a parsed CP/M command line through an initialized client.
   argc and argv use the normal C main() convention.
   Returns zero on success or a nonzero process exit status.
*/
int paket_application_run(
    paket_client *client,
    int argc,
    char **argv
);

#endif

