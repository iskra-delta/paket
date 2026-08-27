/*
   Implements Retro Vault protocol-v1 request encoding and defensive response
   parsing. Packets are decoded field by field and all advertised lengths are
   checked before any caller-visible view is produced.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/retrovault.h"

#include <string.h>

#if !defined(PAKET_CPM)
static int text_size(const char *text, size_t *size)
{
    size_t length = 0U;

    if ((text == NULL) || (size == NULL)) {
        return -1;
    }
    length = strlen(text);
    if (length > 255U) {
        return -1;
    }
    *size = length;
    return 0;
}
#endif

uint16_t rv_get_u16(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] |
        ((uint16_t)input[1] << 8U));
}

uint32_t rv_get_u32(const uint8_t *input)
{
    return (uint32_t)input[0] |
        ((uint32_t)input[1] << 8U) |
        ((uint32_t)input[2] << 16U) |
        ((uint32_t)input[3] << 24U);
}

void rv_put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xffU);
    output[1] = (uint8_t)(value >> 8U);
}

void rv_put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xffU);
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

#if !defined(PAKET_CPM)
int rv_build_capabilities(uint8_t *output, size_t output_size)
{
    if ((output == NULL) || (output_size < 1U)) {
        return -1;
    }
    output[0] = RV_OP_CAPABILITIES;
    return 1;
}

int rv_build_list(
    uint16_t cursor,
    const char *platform,
    const char *model,
    uint8_t *output,
    size_t output_size
)
{
    size_t platform_size = 0U;
    size_t model_size = 0U;
    size_t required = 0U;

    if ((output == NULL) ||
        (text_size(platform, &platform_size) != 0) ||
        ((model != NULL) && (text_size(model, &model_size) != 0))) {
        return -1;
    }
    required = 4U + platform_size;
    if (model_size > 0U) {
        required += 1U + model_size;
    }
    if ((required > RV_PACKET_MAX) || (required > output_size)) {
        return -1;
    }

    output[0] = RV_OP_LIST;
    rv_put_u16(output + 1U, cursor);
    output[3] = (uint8_t)platform_size;
    if (platform_size > 0U) {
        memcpy(output + 4U, platform, platform_size);
    }
    if (model_size > 0U) {
        output[4U + platform_size] = (uint8_t)model_size;
        memcpy(output + 5U + platform_size, model, model_size);
    }
    return (int)required;
}

int rv_build_search(
    uint16_t cursor,
    const char *platform,
    const char *query,
    const char *model,
    uint8_t *output,
    size_t output_size
)
{
    size_t platform_size = 0U;
    size_t query_size = 0U;
    size_t model_size = 0U;
    size_t required = 0U;
    size_t offset = 0U;

    if ((output == NULL) ||
        (text_size(platform, &platform_size) != 0) ||
        (text_size(query, &query_size) != 0) ||
        ((model != NULL) && (text_size(model, &model_size) != 0)) ||
        (query_size == 0U)) {
        return -1;
    }
    required = 5U + platform_size + query_size;
    if (model_size > 0U) {
        required += 1U + model_size;
    }
    if ((required > RV_PACKET_MAX) || (required > output_size)) {
        return -1;
    }

    output[0] = RV_OP_SEARCH;
    rv_put_u16(output + 1U, cursor);
    output[3] = (uint8_t)platform_size;
    offset = 4U;
    if (platform_size > 0U) {
        memcpy(output + offset, platform, platform_size);
        offset += platform_size;
    }
    output[offset++] = (uint8_t)query_size;
    memcpy(output + offset, query, query_size);
    offset += query_size;
    if (model_size > 0U) {
        output[offset++] = (uint8_t)model_size;
        memcpy(output + offset, model, model_size);
    }
    return (int)required;
}

int rv_build_info(
    uint16_t cursor,
    const char *package_id,
    uint8_t *output,
    size_t output_size
)
{
    size_t id_size = 0U;
    size_t required = 0U;

    if ((output == NULL) ||
        (text_size(package_id, &id_size) != 0) || (id_size == 0U)) {
        return -1;
    }
    required = 4U + id_size;
    if ((required > RV_PACKET_MAX) || (required > output_size)) {
        return -1;
    }

    output[0] = RV_OP_INFO;
    rv_put_u16(output + 1U, cursor);
    output[3] = (uint8_t)id_size;
    memcpy(output + 4U, package_id, id_size);
    return (int)required;
}

int rv_build_download(
    uint32_t offset,
    uint8_t maximum_bytes,
    const char *package_id,
    const char *download_id,
    uint8_t *output,
    size_t output_size
)
{
    size_t package_size = 0U;
    size_t download_size = 0U;
    size_t required = 0U;
    size_t position = 0U;

    if ((output == NULL) ||
        (text_size(package_id, &package_size) != 0) ||
        (text_size(download_id, &download_size) != 0) ||
        (package_size == 0U) || (download_size == 0U)) {
        return -1;
    }
    required = 8U + package_size + download_size;
    if ((required > RV_PACKET_MAX) || (required > output_size)) {
        return -1;
    }

    output[0] = RV_OP_DOWNLOAD;
    rv_put_u32(output + 1U, offset);
    output[5] = maximum_bytes;
    output[6] = (uint8_t)package_size;
    memcpy(output + 7U, package_id, package_size);
    position = 7U + package_size;
    output[position++] = (uint8_t)download_size;
    memcpy(output + position, download_id, download_size);
    return (int)required;
}
#endif

static int catalog_response_size(
    const uint8_t *response,
    size_t available
)
{
    size_t offset = 5U;
    unsigned int index = 0U;

    if (available < 5U) {
        return 0;
    }
    for (index = 0U; index < response[4]; ++index) {
        size_t field_size = 0U;

        if (offset >= available) {
            return 0;
        }
        field_size = response[offset++];
        if (field_size > available - offset) {
            return 0;
        }
        offset += field_size;
        if (offset >= available) {
            return 0;
        }
        field_size = response[offset++];
        if (field_size > available - offset) {
            return 0;
        }
        offset += field_size;
    }
    return (int)offset;
}

static int info_response_size(
    const uint8_t *response,
    size_t available
)
{
    size_t offset = 5U;
    unsigned int index = 0U;

    if (available < 5U) {
        return 0;
    }
    for (index = 0U; index < response[4]; ++index) {
        size_t field_size = 0U;

        if (available - offset < 2U) {
            return 0;
        }
        ++offset;
        field_size = response[offset++];
        if (field_size > available - offset) {
            return 0;
        }
        offset += field_size;
    }
    return (int)offset;
}

int rv_response_size(const uint8_t *response, size_t available)
{
    uint8_t operation = 0U;

    if ((response == NULL) || (available > RV_PACKET_MAX)) {
        return -1;
    }
    if (available < 2U) {
        return 0;
    }
    if ((response[0] & RV_RESPONSE_BIT) == 0U) {
        return -1;
    }
    if (response[1] != RV_STATUS_OK) {
        return 2;
    }

    operation = (uint8_t)(response[0] & (uint8_t)~RV_RESPONSE_BIT);
    switch (operation) {
    case RV_OP_CAPABILITIES:
        return available >= 7U ? 7 : 0;
    case RV_OP_LIST:
    case RV_OP_SEARCH:
        return catalog_response_size(response, available);
    case RV_OP_INFO:
        return info_response_size(response, available);
    case RV_OP_DOWNLOAD:
        if (available < 11U) {
            return 0;
        }
        return (int)(11U + response[10]);
    default:
        return -1;
    }
}

#if !defined(PAKET_CPM)
int rv_visit_catalog_page(
    const uint8_t *response,
    size_t response_size,
    uint16_t *next_cursor,
    rv_catalog_visitor visitor,
    void *context
)
{
    size_t offset = 5U;
    unsigned int index = 0U;
    uint8_t operation = 0U;

    if ((response == NULL) || (next_cursor == NULL) ||
        (response_size < 5U) ||
        (rv_response_size(response, response_size) != (int)response_size) ||
        (response[1] != RV_STATUS_OK)) {
        return -1;
    }
    operation = (uint8_t)(response[0] & (uint8_t)~RV_RESPONSE_BIT);
    if ((operation != RV_OP_LIST) && (operation != RV_OP_SEARCH)) {
        return -1;
    }

    *next_cursor = rv_get_u16(response + 2U);
    for (index = 0U; index < response[4]; ++index) {
        rv_catalog_view entry;

        entry.id.size = response[offset++];
        entry.id.data = response + offset;
        offset += entry.id.size;
        entry.name.size = response[offset++];
        entry.name.data = response + offset;
        offset += entry.name.size;
        if ((visitor != NULL) && (visitor(context, &entry) != 0)) {
            return 1;
        }
    }
    return 0;
}

int rv_visit_info_page(
    const uint8_t *response,
    size_t response_size,
    uint16_t *next_cursor,
    rv_info_visitor visitor,
    void *context
)
{
    size_t offset = 5U;
    unsigned int index = 0U;

    if ((response == NULL) || (next_cursor == NULL) ||
        (response_size < 5U) ||
        (rv_response_size(response, response_size) != (int)response_size) ||
        (response[0] != (RV_OP_INFO | RV_RESPONSE_BIT)) ||
        (response[1] != RV_STATUS_OK)) {
        return -1;
    }

    *next_cursor = rv_get_u16(response + 2U);
    for (index = 0U; index < response[4]; ++index) {
        rv_info_view entry;

        entry.type = response[offset++];
        entry.value.size = response[offset++];
        entry.value.data = response + offset;
        offset += entry.value.size;
        if ((visitor != NULL) && (visitor(context, &entry) != 0)) {
            return 1;
        }
    }
    return 0;
}
#endif

int rv_decode_download(
    const uint8_t *value,
    size_t value_size,
    rv_download_view *download
)
{
    size_t offset = 0U;

    if ((value == NULL) || (download == NULL) || (value_size < 8U)) {
        return -1;
    }

    download->id.size = value[offset++];
    if (download->id.size > value_size - offset) {
        return -1;
    }
    download->id.data = value + offset;
    offset += download->id.size;

    if (offset >= value_size) {
        return -1;
    }
    download->label.size = value[offset++];
    if (download->label.size > value_size - offset) {
        return -1;
    }
    download->label.data = value + offset;
    offset += download->label.size;

    if (offset >= value_size) {
        return -1;
    }
    download->format.size = value[offset++];
    if (download->format.size > value_size - offset) {
        return -1;
    }
    download->format.data = value + offset;
    offset += download->format.size;

    if (value_size - offset != 5U) {
        return -1;
    }
    download->aggregate_size = rv_get_u32(value + offset);
    download->file_count = value[offset + 4U];
    return 0;
}
