/*
   Provides the CP/M entry point for PAKET.COM. It handles help locally,
   establishes the Partner serial/libsquid connection, validates Retro Vault
   capabilities, and delegates command behavior to the application module.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/application.h"
#include "paket/client.h"
#include "paket/command.h"
#include "paket/link.h"
#include "paket/messages.h"
#include "paket/output.h"

#include <string.h>

static paket_link link;
static paket_client client;

static int help_requested(int argc, char **argv)
{
    if ((argc != 2) || (argv == NULL)) {
        return 0;
    }
    return (strcmp(argv[1], "-h") == 0) ||
        (strcmp(argv[1], "-H") == 0) ||
        (strcmp(argv[1], "--help") == 0) ||
        (strcmp(argv[1], "--HELP") == 0) ||
        (strcmp(argv[1], "/?") == 0);
}

static int run(int argc, char **argv)
{
    paket_options options;
    const paket_communication *communication = NULL;
    int result = 0;

    if (paket_parse_options(&argc, argv, &options) != 0) {
        paket_output_line(paket_message(PAKET_MSG_INVALID_OPTIONS));
        paket_print_usage();
        return 1;
    }
    if (help_requested(argc, argv)) {
        paket_print_usage();
        return 0;
    }
    if ((argc < 1) || (argc > 3)) {
        paket_print_usage();
        return 1;
    }

    paket_output_format(
        paket_message(PAKET_MSG_CONNECTING_FORMAT),
        options.serial_port
    );
    if (options.has_communication) {
        communication = &options.communication;
    }
    if (paket_link_open(
        &link,
        options.serial_port,
        communication,
        options.payload_bytes
    ) != 0) {
        paket_output_text(paket_message(PAKET_MSG_CONNECTION_FAILED));
        paket_output_format(
            paket_message(PAKET_MSG_CANNOT_CONNECT_FORMAT),
            options.serial_port
        );
        return 1;
    }
    paket_client_init(&client, paket_link_client(&link));
    paket_client_set_partner_type(&client, paket_detect_partner_type());
    result = paket_check_capabilities(&client);
    if (result != 0) {
        paket_output_text(paket_message(PAKET_MSG_CONNECTION_FAILED));
        paket_output_format(
            paket_message(PAKET_MSG_ERROR_FORMAT),
            paket_result_text(result)
        );
        paket_link_close(&link);
        return 1;
    }
    paket_output_text(paket_message(PAKET_MSG_CONNECTION_OK));

    result = paket_application_run(&client, argc, argv);
    paket_link_close(&link);
    return result;
}

int main(int argc, char **argv)
{
    int result;

    /* Do this before parsing or console output: a rising RTC NMI edge is
       latched by the Z80 and cannot be cancelled after serial setup starts. */
    paket_link_process_begin();
    result = run(argc, argv);
    paket_link_process_end();
    return result;
}
