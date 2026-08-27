/*
   Declares PAKET's transport-independent Retro Vault client. It walks paged
   catalog and metadata responses, resolves package names, and streams
   downloads without retaining package data in the Partner's small memory.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_CLIENT_H
#define PAKET_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "paket/platform.h"
#include "paket/retrovault.h"

#if defined(PAKET_CPM)
#include <squid_client/retrovault.h>
#endif

#define PAKET_ID_SIZE          64U
#define PAKET_NAME_SIZE        96U
#define PAKET_VENDOR_SIZE      64U
#define PAKET_PLATFORM_SIZE    48U
#define PAKET_VERSION_SIZE     24U
/* Retro Vault uses a one-byte TLV length, so 256 bytes retain every
   possible description plus its terminating zero. */
#define PAKET_DESCRIPTION_SIZE 256U
#define PAKET_FORMAT_SIZE      12U
#define PAKET_DOWNLOAD_MAX     1U

#define PAKET_ERROR_ARGUMENT  (-1)
#define PAKET_ERROR_TRANSPORT (-2)
#define PAKET_ERROR_PROTOCOL  (-3)
#define PAKET_ERROR_NOT_FOUND (-4)
#define PAKET_ERROR_TOO_LONG  (-5)
#define PAKET_ERROR_WRITE     (-6)
#define PAKET_ERROR_SERVER_BASE (-32)

typedef int (*paket_exchange_fn)(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
);

typedef struct {
    char id[PAKET_ID_SIZE];
    char name[PAKET_NAME_SIZE];
} paket_catalog_entry;

typedef struct {
    char id[PAKET_ID_SIZE];
    char label[PAKET_NAME_SIZE];
    char format[PAKET_FORMAT_SIZE];
    uint32_t aggregate_size;
    uint8_t file_count;
} paket_download_choice;

typedef struct {
    char id[PAKET_ID_SIZE];
    char name[PAKET_NAME_SIZE];
    char vendor[PAKET_VENDOR_SIZE];
    char platform_name[PAKET_PLATFORM_SIZE];
    char version[PAKET_VERSION_SIZE];
    char description[PAKET_DESCRIPTION_SIZE];
    uint16_t release_year;
    uint8_t rating;
    uint8_t download_count;
    uint8_t stored_download_count;
    paket_download_choice downloads[PAKET_DOWNLOAD_MAX];
} paket_package_info;

typedef struct {
#if defined(PAKET_CPM)
    squid_client_t *squid;
#else
    paket_exchange_fn exchange;
    void *exchange_context;
    uint8_t packet[RV_PACKET_MAX];
#endif
    paket_partner_type partner_type;
} paket_client;

typedef int (*paket_catalog_visitor)(
    void *context,
    const paket_catalog_entry *entry
);

typedef int (*paket_write_fn)(
    void *context,
    const uint8_t *data,
    size_t data_size,
    uint32_t offset,
    uint32_t total_size
);

/* Initialize the CP/M client around the optimized Squid service client. */
#if defined(PAKET_CPM)
void paket_client_init(
    paket_client *client,
    squid_client_t *squid
);
#else
/* Initialize a hosted client around a mockable synchronous exchange. */
void paket_client_init(
    paket_client *client,
    paket_exchange_fn exchange,
    void *exchange_context
);
#endif

/* Restrict Partner catalog requests to the models this machine can run. */
void paket_client_set_partner_type(
    paket_client *client,
    paket_partner_type partner_type
);

/* Verify that the server exposes Retro Vault protocol version 1. */
int paket_check_capabilities(paket_client *client);

/*
   List or wildcard-search Partner catalog entries.
   A null pattern lists all Partner packages. The result count is optional.
*/
int paket_visit_catalog(
    paket_client *client,
    const char *pattern,
    paket_catalog_visitor visitor,
    void *context,
    unsigned int *result_count
);

/* Resolve an exact case-insensitive package ID or display name. */
int paket_resolve_package(
    paket_client *client,
    const char *package_name,
    paket_catalog_entry *entry
);

/* Fetch all paged metadata for one resolved package ID. */
int paket_fetch_info(
    paket_client *client,
    const char *package_id,
    paket_package_info *info
);

/* Stream one selected download through writer until its total size is met. */
int paket_fetch_download(
    paket_client *client,
    const char *package_id,
    const char *download_id,
    paket_write_fn writer,
    void *writer_context
);

/* Return a localized user-facing description for a public client result. */
const char *paket_result_text(int result);

#endif
