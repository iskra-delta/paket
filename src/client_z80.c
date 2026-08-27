/*
   Partner build of PAKET's catalog client. Protocol serialization, packet
   exchange and page iteration come from squid-server's hand-written Z80
   client archive; this file retains only PAKET policy and presentation data.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/client.h"

#include "paket/command.h"
#include "paket/messages.h"

#include <squid_client/base.h>
#include <squid_client/retrovault.h>

#include <string.h>

#define retro_vault_protocol_version 1U
#define retro_vault_required_features 0x0fU
#define retro_vault_page_header_size 5U
#define retro_vault_download_header_size 11U
#define paket_download_chunk_size 244U

#define partner_platform_id "idp"
#define partner_crt_model_id "p"

typedef struct {
    const char *pattern;
    paket_catalog_visitor visitor;
    void *visitor_context;
    unsigned int count;
    int stopped;
    int error;
} catalog_visit_context;

typedef struct {
    const char *name;
    paket_catalog_entry *result;
    int priority;
} resolve_context;

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8U);
}

static uint32_t read_u32(const uint8_t *input)
{
    return (uint32_t)input[0] |
        ((uint32_t)input[1] << 8U) |
        ((uint32_t)input[2] << 16U) |
        ((uint32_t)input[3] << 24U);
}

static char ascii_upper(char character)
{
    if ((character >= 'a') && (character <= 'z')) {
        return (char)(character - 'a' + 'A');
    }
    return character;
}

static int text_equals(const char *left, const char *right)
{
    if ((left == NULL) || (right == NULL)) {
        return 0;
    }
    while ((*left != '\0') && (*right != '\0')) {
        if (ascii_upper(*left) != ascii_upper(*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return (*left == '\0') && (*right == '\0');
}

static int copy_bytes(
    char *output,
    size_t output_size,
    const squid_client_bytes_t *bytes,
    int strict
)
{
    size_t copy_size;

    if ((output == NULL) || (output_size == 0U) || (bytes == NULL)) {
        return -1;
    }
    if (strict && ((size_t)bytes->size >= output_size)) {
        return -1;
    }
    copy_size = bytes->size;
    if (copy_size >= output_size) {
        copy_size = output_size - 1U;
    }
    if (copy_size != 0U) {
        memcpy(output, bytes->data, copy_size);
    }
    output[copy_size] = '\0';
    return 0;
}

static int map_squid_result(int result)
{
    if (result == 0) {
        return 0;
    }
    if (result > 0) {
        return PAKET_ERROR_SERVER_BASE - result;
    }
    switch (result) {
    case SQUID_CLIENT_ERROR_ARGUMENT:
        return PAKET_ERROR_ARGUMENT;
    case SQUID_CLIENT_ERROR_PROTOCOL:
    case SQUID_CLIENT_ERROR_OVERFLOW:
        return PAKET_ERROR_PROTOCOL;
    case SQUID_CLIENT_ERROR_LINK:
    case SQUID_CLIENT_ERROR_IO:
    case SQUID_CLIENT_ERROR_CANCELLED:
    default:
        return PAKET_ERROR_TRANSPORT;
    }
}

void paket_client_init(paket_client *client, squid_client_t *squid)
{
    if (client == NULL) {
        return;
    }
    memset(client, 0, sizeof(*client));
    client->squid = squid;
    client->partner_type = PAKET_PARTNER_CRT;
}

void paket_client_set_partner_type(
    paket_client *client,
    paket_partner_type partner_type
)
{
    if (client == NULL) {
        return;
    }
    client->partner_type = partner_type == PAKET_PARTNER_GDP
        ? PAKET_PARTNER_GDP
        : PAKET_PARTNER_CRT;
}

int paket_check_capabilities(paket_client *client)
{
    uint8_t *packet;
    int response_size;

    if ((client == NULL) || (client->squid == NULL) ||
        (client->squid->packet == NULL)) {
        return PAKET_ERROR_ARGUMENT;
    }
    packet = client->squid->packet;
    packet[1] = RV_OP_CAPABILITIES;
    response_size = squid_client_exchange(client->squid, 1U);
    if (response_size < 0) {
        return map_squid_result(response_size);
    }
    if ((response_size < 2) ||
        (packet[0] != (RV_OP_CAPABILITIES | RV_RESPONSE_BIT))) {
        return PAKET_ERROR_PROTOCOL;
    }
    if (packet[1] != RV_STATUS_OK) {
        return PAKET_ERROR_SERVER_BASE - packet[1];
    }
    if ((response_size != 7) ||
        (packet[2] != retro_vault_protocol_version) ||
        ((packet[3] & retro_vault_required_features) !=
         retro_vault_required_features) ||
        (packet[4] != retro_vault_page_header_size) ||
        (packet[5] != retro_vault_download_header_size) ||
        (packet[6] == 0U)) {
        return PAKET_ERROR_PROTOCOL;
    }
    return 0;
}

static int visit_catalog_entry(
    catalog_visit_context *visit,
    const squid_client_retro_entry_t *view
)
{
    paket_catalog_entry entry;

    if ((visit == NULL) || (view == NULL)) {
        return 1;
    }
    if ((copy_bytes(entry.id, sizeof(entry.id), &view->id, 1) != 0) ||
        (copy_bytes(entry.name, sizeof(entry.name), &view->name, 0) != 0)) {
        visit->error = PAKET_ERROR_TOO_LONG;
        return 1;
    }
    if ((visit->pattern != NULL) &&
        !paket_wildcard_match(visit->pattern, entry.id) &&
        !paket_wildcard_match(visit->pattern, entry.name)) {
        return 0;
    }
    ++visit->count;
    if ((visit->visitor != NULL) &&
        (visit->visitor(visit->visitor_context, &entry) != 0)) {
        visit->stopped = 1;
        return 1;
    }
    return 0;
}

static int visit_catalog_model(
    paket_client *client,
    const char *query,
    const char *model,
    int search,
    catalog_visit_context *visit
)
{
    uint16_t cursor = 0U;

    for (;;) {
        squid_client_retro_list_t page;
        squid_client_retro_entry_t entry;
        int result;

        if (search) {
            result = squid_client_retro_search(
                client->squid, partner_platform_id, model, query,
                cursor, &page
            );
        } else {
            result = squid_client_retro_list(
                client->squid, partner_platform_id, model, cursor, &page
            );
        }
        if (result != 0) {
            return map_squid_result(result);
        }
        for (;;) {
            result = squid_client_retro_list_next(&page, &entry);
            if (result < 0) {
                return map_squid_result(result);
            }
            if (result == 0) {
                break;
            }
            if (visit_catalog_entry(visit, &entry) != 0) {
                break;
            }
        }
        if (visit->error != 0) {
            return visit->error;
        }
        if (visit->stopped || (page.next_cursor == RV_CURSOR_END)) {
            return 0;
        }
        if (page.next_cursor == cursor) {
            return PAKET_ERROR_PROTOCOL;
        }
        cursor = page.next_cursor;
    }
}

int paket_visit_catalog(
    paket_client *client,
    const char *pattern,
    paket_catalog_visitor visitor,
    void *context,
    unsigned int *result_count
)
{
    catalog_visit_context visit;
    char query[11];
    int search = 0;
    int result;

    if ((client == NULL) || (client->squid == NULL) ||
        ((pattern != NULL) && (*pattern == '\0'))) {
        return PAKET_ERROR_ARGUMENT;
    }
    memset(&visit, 0, sizeof(visit));
    visit.pattern = pattern;
    visit.visitor = visitor;
    visit.visitor_context = context;
    query[0] = '\0';
    if ((pattern != NULL) &&
        (paket_search_anchor(pattern, query, sizeof(query)) > 0U)) {
        search = 1;
    }
    result = visit_catalog_model(
        client,
        query,
        client->partner_type == PAKET_PARTNER_GDP
            ? NULL : partner_crt_model_id,
        search,
        &visit
    );
    if (result != 0) {
        return result;
    }
    if (result_count != NULL) {
        *result_count = visit.count;
    }
    return 0;
}

static int resolve_entry(void *context, const paket_catalog_entry *entry)
{
    resolve_context *resolve = context;
    int priority = 0;

    if ((resolve == NULL) || (entry == NULL)) {
        return 1;
    }
    if (text_equals(resolve->name, entry->id)) {
        priority = 2;
    } else if (text_equals(resolve->name, entry->name)) {
        priority = 1;
    }
    if (priority > resolve->priority) {
        *resolve->result = *entry;
        resolve->priority = priority;
    }
    return priority == 2;
}

int paket_resolve_package(
    paket_client *client,
    const char *package_name,
    paket_catalog_entry *entry
)
{
    resolve_context resolve;
    int result;

    if ((client == NULL) || (package_name == NULL) ||
        (*package_name == '\0') || (entry == NULL) ||
        paket_has_wildcards(package_name)) {
        return PAKET_ERROR_ARGUMENT;
    }
    memset(entry, 0, sizeof(*entry));
    resolve.name = package_name;
    resolve.result = entry;
    resolve.priority = 0;
    result = paket_visit_catalog(
        client, package_name, resolve_entry, &resolve, NULL
    );
    if (result != 0) {
        return result;
    }
    return resolve.priority > 0 ? 0 : PAKET_ERROR_NOT_FOUND;
}

static int decode_download(
    const squid_client_bytes_t *value,
    paket_download_choice *choice
)
{
    const uint8_t *cursor;
    const uint8_t *end;
    squid_client_bytes_t field;

    if ((value == NULL) || (choice == NULL)) {
        return -1;
    }
    cursor = value->data;
    end = cursor + value->size;
    if (cursor >= end) {
        return -1;
    }
    field.size = *cursor++;
    if ((size_t)(end - cursor) < field.size) {
        return -1;
    }
    field.data = cursor;
    cursor += field.size;
    if (copy_bytes(choice->id, sizeof(choice->id), &field, 1) != 0 ||
        cursor >= end) {
        return -1;
    }
    field.size = *cursor++;
    if ((size_t)(end - cursor) < field.size) {
        return -1;
    }
    field.data = cursor;
    cursor += field.size;
    if (copy_bytes(choice->label, sizeof(choice->label), &field, 0) != 0 ||
        cursor >= end) {
        return -1;
    }
    field.size = *cursor++;
    if ((size_t)(end - cursor) < (size_t)field.size + 5U) {
        return -1;
    }
    field.data = cursor;
    cursor += field.size;
    (void)copy_bytes(choice->format, sizeof(choice->format), &field, 0);
    choice->aggregate_size = read_u32(cursor);
    cursor += 4;
    choice->file_count = *cursor++;
    return cursor == end ? 0 : -1;
}

static int collect_info_value(
    paket_package_info *info,
    const squid_client_retro_value_t *entry
)
{
    switch (entry->type) {
    case RV_INFO_ID:
        return copy_bytes(info->id, sizeof(info->id), &entry->value, 1) == 0
            ? 0 : PAKET_ERROR_TOO_LONG;
    case RV_INFO_NAME:
        (void)copy_bytes(info->name, sizeof(info->name), &entry->value, 0);
        break;
    case RV_INFO_VENDOR:
        (void)copy_bytes(info->vendor, sizeof(info->vendor), &entry->value, 0);
        break;
    case RV_INFO_PLATFORM_NAME:
        (void)copy_bytes(
            info->platform_name, sizeof(info->platform_name), &entry->value, 0
        );
        break;
    case RV_INFO_VERSION:
        (void)copy_bytes(
            info->version, sizeof(info->version), &entry->value, 0
        );
        break;
    case RV_INFO_YEAR:
        if (entry->value.size != 2U) {
            return PAKET_ERROR_PROTOCOL;
        }
        info->release_year = read_u16(entry->value.data);
        break;
    case RV_INFO_RATING:
        if (entry->value.size != 1U) {
            return PAKET_ERROR_PROTOCOL;
        }
        info->rating = entry->value.data[0];
        break;
    case RV_INFO_DESCRIPTION:
        (void)copy_bytes(
            info->description, sizeof(info->description), &entry->value, 0
        );
        break;
    case RV_INFO_DOWNLOAD:
        if (info->download_count < 255U) {
            ++info->download_count;
        }
        if (info->stored_download_count < PAKET_DOWNLOAD_MAX) {
            paket_download_choice *choice =
                &info->downloads[info->stored_download_count];
            if (decode_download(&entry->value, choice) != 0) {
                return PAKET_ERROR_PROTOCOL;
            }
            ++info->stored_download_count;
        }
        break;
    default:
        break;
    }
    return 0;
}

int paket_fetch_info(
    paket_client *client,
    const char *package_id,
    paket_package_info *info
)
{
    uint16_t cursor = 0U;

    if ((client == NULL) || (client->squid == NULL) ||
        (package_id == NULL) || (*package_id == '\0') || (info == NULL)) {
        return PAKET_ERROR_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));
    for (;;) {
        squid_client_retro_info_t page;
        squid_client_retro_value_t value;
        int result = squid_client_retro_info(
            client->squid, package_id, cursor, &page
        );
        if (result != 0) {
            return map_squid_result(result);
        }
        for (;;) {
            result = squid_client_retro_info_next(&page, &value);
            if (result < 0) {
                return map_squid_result(result);
            }
            if (result == 0) {
                break;
            }
            result = collect_info_value(info, &value);
            if (result != 0) {
                return result;
            }
        }
        if (page.next_cursor == RV_CURSOR_END) {
            break;
        }
        if (page.next_cursor == cursor) {
            return PAKET_ERROR_PROTOCOL;
        }
        cursor = page.next_cursor;
    }
    return info->id[0] != '\0' ? 0 : PAKET_ERROR_PROTOCOL;
}

int paket_fetch_download(
    paket_client *client,
    const char *package_id,
    const char *download_id,
    paket_write_fn writer,
    void *writer_context
)
{
    uint32_t offset = 0U;
    uint32_t total_size = 0U;
    int have_total = 0;

    if ((client == NULL) || (client->squid == NULL) ||
        (package_id == NULL) || (download_id == NULL) || (writer == NULL)) {
        return PAKET_ERROR_ARGUMENT;
    }
    for (;;) {
        squid_client_retro_download_t chunk;
        int result = squid_client_retro_download(
            client->squid, package_id, download_id, offset,
            paket_download_chunk_size, &chunk
        );
        if (result != 0) {
            return map_squid_result(result);
        }
        if ((chunk.offset != offset) || (offset > chunk.total_size) ||
            ((uint32_t)chunk.data.size > chunk.total_size - offset) ||
            (have_total && (chunk.total_size != total_size)) ||
            ((chunk.data.size == 0U) && (offset != chunk.total_size))) {
            return PAKET_ERROR_PROTOCOL;
        }
        total_size = chunk.total_size;
        have_total = 1;
        if (writer(
            writer_context, chunk.data.data, chunk.data.size,
            offset, total_size
        ) != 0) {
            return PAKET_ERROR_WRITE;
        }
        offset += chunk.data.size;
        if (offset == total_size) {
            return 0;
        }
    }
}

const char *paket_result_text(int result)
{
    int status = PAKET_ERROR_SERVER_BASE - result;

    if ((result > PAKET_ERROR_SERVER_BASE) ||
        (status < (int)RV_STATUS_BAD_REQUEST) ||
        (status > (int)RV_STATUS_INTERNAL_ERROR)) {
        status = -1;
    }

    if (status >= 0) {
        switch ((uint8_t)status) {
        case RV_STATUS_BAD_REQUEST:
            return paket_message(PAKET_MSG_STATUS_BAD_REQUEST);
        case RV_STATUS_API_UNAVAILABLE:
            return paket_message(PAKET_MSG_STATUS_UNAVAILABLE);
        case RV_STATUS_NOT_FOUND:
            return paket_message(PAKET_MSG_STATUS_NOT_FOUND);
        case RV_STATUS_TOO_LARGE:
            return paket_message(PAKET_MSG_STATUS_TOO_LARGE);
        case RV_STATUS_INTERNAL_ERROR:
            return paket_message(PAKET_MSG_STATUS_SERVER_ERROR);
        default:
            return paket_message(PAKET_MSG_STATUS_UNKNOWN);
        }
    }
    switch (result) {
    case 0:
        return paket_message(PAKET_MSG_RESULT_OK);
    case PAKET_ERROR_ARGUMENT:
        return paket_message(PAKET_MSG_RESULT_INVALID_ARGUMENT);
    case PAKET_ERROR_TRANSPORT:
        return paket_message(PAKET_MSG_RESULT_LINK_FAILED);
    case PAKET_ERROR_PROTOCOL:
        return paket_message(PAKET_MSG_RESULT_INVALID_RESPONSE);
    case PAKET_ERROR_NOT_FOUND:
        return paket_message(PAKET_MSG_RESULT_NOT_FOUND);
    case PAKET_ERROR_TOO_LONG:
        return paket_message(PAKET_MSG_RESULT_TOO_LONG);
    case PAKET_ERROR_WRITE:
        return paket_message(PAKET_MSG_RESULT_WRITE_FAILED);
    default:
        return paket_message(PAKET_MSG_RESULT_UNKNOWN);
    }
}
