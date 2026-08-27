/*
   Declares the compact Retro Vault protocol-v1 encoder and decoder used by
   PAKET. All parsing is byte-oriented so it is independent of Z80 compiler
   structure padding and host byte order.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_RETROVAULT_H
#define PAKET_RETROVAULT_H

#include <stddef.h>
#include <stdint.h>

/* Wire v2 queues a one-byte packet length plus up to 255 plugin bytes. */
#define RV_PACKET_MAX 255U
#define RV_CURSOR_END 0xffffU

#define RV_OP_CAPABILITIES 0x00U
#define RV_OP_LIST         0x01U
#define RV_OP_SEARCH       0x02U
#define RV_OP_INFO         0x03U
#define RV_OP_DOWNLOAD     0x04U
#define RV_RESPONSE_BIT    0x80U

#define RV_STATUS_OK              0x00U
#define RV_STATUS_BAD_REQUEST     0x01U
#define RV_STATUS_API_UNAVAILABLE 0x02U
#define RV_STATUS_NOT_FOUND       0x03U
#define RV_STATUS_TOO_LARGE       0x04U
#define RV_STATUS_INTERNAL_ERROR  0x05U

#define RV_INFO_ID            0x01U
#define RV_INFO_NAME          0x02U
#define RV_INFO_VENDOR        0x03U
#define RV_INFO_PLATFORM_ID   0x04U
#define RV_INFO_PLATFORM_NAME 0x05U
#define RV_INFO_VERSION       0x06U
#define RV_INFO_YEAR          0x07U
#define RV_INFO_RATING        0x08U
#define RV_INFO_DESCRIPTION   0x09U
#define RV_INFO_DOWNLOAD      0x0aU

typedef struct {
    const uint8_t *data;
    uint8_t size;
} rv_text_view;

typedef struct {
    rv_text_view id;
    rv_text_view name;
} rv_catalog_view;

typedef struct {
    uint8_t type;
    rv_text_view value;
} rv_info_view;

typedef struct {
    rv_text_view id;
    rv_text_view label;
    rv_text_view format;
    uint32_t aggregate_size;
    uint8_t file_count;
} rv_download_view;

typedef int (*rv_catalog_visitor)(
    void *context,
    const rv_catalog_view *entry
);

typedef int (*rv_info_visitor)(
    void *context,
    const rv_info_view *entry
);

/* Read a little-endian 16-bit integer from an unaligned byte range. */
uint16_t rv_get_u16(const uint8_t *input);

/* Read a little-endian 32-bit integer from an unaligned byte range. */
uint32_t rv_get_u32(const uint8_t *input);

/* Write a little-endian 16-bit integer to an unaligned byte range. */
void rv_put_u16(uint8_t *output, uint16_t value);

/* Write a little-endian 32-bit integer to an unaligned byte range. */
void rv_put_u32(uint8_t *output, uint32_t value);

/* Build a one-byte CAPABILITIES request, returning its size or -1. */
int rv_build_capabilities(uint8_t *output, size_t output_size);

/* Build a paged LIST request, returning its size or -1. */
int rv_build_list(
    uint16_t cursor,
    const char *platform,
    const char *model,
    uint8_t *output,
    size_t output_size
);

/* Build a paged SEARCH request, returning its size or -1. */
int rv_build_search(
    uint16_t cursor,
    const char *platform,
    const char *query,
    const char *model,
    uint8_t *output,
    size_t output_size
);

/* Build a paged INFO request, returning its size or -1. */
int rv_build_info(
    uint16_t cursor,
    const char *package_id,
    uint8_t *output,
    size_t output_size
);

/* Build a DOWNLOAD chunk request, returning its size or -1. */
int rv_build_download(
    uint32_t offset,
    uint8_t maximum_bytes,
    const char *package_id,
    const char *download_id,
    uint8_t *output,
    size_t output_size
);

/*
   Determine the complete response size from bytes received so far.
   Returns zero when more bytes are required, a positive complete size,
   or -1 when the bytes cannot form a protocol-v1 response.
*/
int rv_response_size(const uint8_t *response, size_t available);

/* Visit and validate every entry in a LIST or SEARCH response page. */
int rv_visit_catalog_page(
    const uint8_t *response,
    size_t response_size,
    uint16_t *next_cursor,
    rv_catalog_visitor visitor,
    void *context
);

/* Visit and validate every TLV in an INFO response page. */
int rv_visit_info_page(
    const uint8_t *response,
    size_t response_size,
    uint16_t *next_cursor,
    rv_info_visitor visitor,
    void *context
);

/* Decode one INFO download-choice value into non-owning byte views. */
int rv_decode_download(
    const uint8_t *value,
    size_t value_size,
    rv_download_view *download
);

#endif
