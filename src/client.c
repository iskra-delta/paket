/*
   Implements PAKET's transport-independent catalog, metadata, and download
   operations. The client reuses one 253-byte packet buffer for requests and
   responses
   and streams download chunks directly to a caller-provided writer.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/client.h"

#include "paket/messages.h"

#include "paket/command.h"

#include <string.h>

#define retro_vault_protocol_version 1U
#define retro_vault_required_features 0x0fU
#define retro_vault_page_header_size 5U
#define retro_vault_download_header_size 11U
#define paket_download_chunk_size (RV_PACKET_MAX - retro_vault_download_header_size)

#define partner_platform_id "idp"
#define partner_platform_id_size 3U
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

typedef struct {
    paket_package_info *info;
    int error;
} info_visit_context;

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

static int copy_view(
    char *output,
    size_t output_size,
    const rv_text_view *view,
    int strict
)
{
    size_t copy_size = 0U;

    if ((output == NULL) || (output_size == 0U) || (view == NULL)) {
        return -1;
    }
    if (strict && ((size_t)view->size >= output_size)) {
        return -1;
    }
    copy_size = view->size;
    if (copy_size >= output_size) {
        copy_size = output_size - 1U;
    }
    if (copy_size > 0U) {
        memcpy(output, view->data, copy_size);
    }
    output[copy_size] = '\0';
    return 0;
}

static int build_catalog_request(
    uint16_t cursor,
    const char *query,
    const char *model,
    uint8_t *packet
)
{
    size_t query_size = query == NULL ? 0U : strlen(query);
    size_t model_size = model == NULL ? 0U : strlen(model);
    size_t platform_size = partner_platform_id_size;
    size_t offset = 4U + platform_size;

    packet[0] = query_size == 0U ? RV_OP_LIST : RV_OP_SEARCH;
    rv_put_u16(packet + 1U, cursor);
    packet[3] = (uint8_t)platform_size;
    memcpy(packet + 4U, partner_platform_id, platform_size);
    if ((query_size > RV_PACKET_MAX - offset - 1U) ||
        (model_size > 255U)) {
        return -1;
    }
    if (query_size > 0U) {
        packet[offset++] = (uint8_t)query_size;
        memcpy(packet + offset, query, query_size);
        offset += query_size;
    }
    if (model_size > 0U) {
        if (model_size > RV_PACKET_MAX - offset - 1U) {
            return -1;
        }
        packet[offset++] = (uint8_t)model_size;
        memcpy(packet + offset, model, model_size);
        offset += model_size;
    }
    return (int)offset;
}

static int build_info_request(
    uint16_t cursor,
    const char *package_id,
    uint8_t *packet
)
{
    size_t id_size = strlen(package_id);

    if ((id_size == 0U) || (id_size > RV_PACKET_MAX - 4U)) {
        return -1;
    }
    packet[0] = RV_OP_INFO;
    rv_put_u16(packet + 1U, cursor);
    packet[3] = (uint8_t)id_size;
    memcpy(packet + 4U, package_id, id_size);
    return (int)(4U + id_size);
}

static int build_download_request(
    uint32_t offset,
    const char *package_id,
    const char *download_id,
    uint8_t *packet
)
{
    size_t package_size = strlen(package_id);
    size_t download_size = strlen(download_id);
    size_t required = 8U + package_size + download_size;
    size_t position;

    if ((package_size == 0U) || (download_size == 0U) ||
        (required > RV_PACKET_MAX)) {
        return -1;
    }
    packet[0] = RV_OP_DOWNLOAD;
    rv_put_u32(packet + 1U, offset);
    /* Fill one protocol packet; the file writer repacks it into 128-byte
       CP/M logical records without another network transaction. */
    packet[5] = paket_download_chunk_size;
    packet[6] = (uint8_t)package_size;
    memcpy(packet + 7U, package_id, package_size);
    position = 7U + package_size;
    packet[position++] = (uint8_t)download_size;
    memcpy(packet + position, download_id, download_size);
    return (int)required;
}

static int exchange_checked(
    paket_client *client,
    size_t request_size,
    uint8_t operation
)
{
    int response_size = 0;
    int complete_size = 0;

    if ((client == NULL) || (client->exchange == NULL) ||
        (request_size == 0U) || (request_size > sizeof(client->packet))) {
        return PAKET_ERROR_ARGUMENT;
    }
    response_size = client->exchange(
        client->exchange_context,
        client->packet,
        request_size,
        client->packet,
        sizeof(client->packet)
    );
    if (response_size < 0) {
        return PAKET_ERROR_TRANSPORT;
    }
    complete_size = rv_response_size(
        client->packet,
        (size_t)response_size
    );
    if ((complete_size != response_size) || (response_size < 2) ||
        (client->packet[0] != (uint8_t)(operation | RV_RESPONSE_BIT))) {
        return PAKET_ERROR_PROTOCOL;
    }
    if (client->packet[1] != RV_STATUS_OK) {
        return PAKET_ERROR_SERVER_BASE - (int)client->packet[1];
    }
    return response_size;
}

void paket_client_init(
    paket_client *client,
    paket_exchange_fn exchange,
    void *exchange_context
)
{
    if (client == NULL) {
        return;
    }
    memset(client, 0, sizeof(*client));
    client->exchange = exchange;
    client->exchange_context = exchange_context;
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
    int request_size = 0;
    int response_size = 0;

    if (client == NULL) {
        return PAKET_ERROR_ARGUMENT;
    }
    client->packet[0] = RV_OP_CAPABILITIES;
    request_size = 1;
    response_size = exchange_checked(
        client,
        (size_t)request_size,
        RV_OP_CAPABILITIES
    );
    if (response_size < 0) {
        return response_size;
    }
    if ((response_size != 7) ||
        (client->packet[2] != retro_vault_protocol_version) ||
        ((client->packet[3] & retro_vault_required_features) !=
         retro_vault_required_features) ||
        (client->packet[4] != retro_vault_page_header_size) ||
        (client->packet[5] != retro_vault_download_header_size) ||
        (client->packet[6] == 0U)) {
        return PAKET_ERROR_PROTOCOL;
    }
    return 0;
}

static int visit_catalog_entry(
    void *context,
    const rv_catalog_view *view
)
{
    catalog_visit_context *visit = context;
    paket_catalog_entry entry;

    if ((visit == NULL) || (view == NULL)) {
        return 1;
    }
    if ((copy_view(entry.id, sizeof(entry.id), &view->id, 1) != 0) ||
        (copy_view(entry.name, sizeof(entry.name), &view->name, 0) != 0)) {
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

static int visit_catalog_packet(
    paket_client *client,
    catalog_visit_context *visit,
    uint16_t *next_cursor
)
{
    size_t offset = 5U;
    unsigned int index;

    *next_cursor = rv_get_u16(client->packet + 2U);
    for (index = 0U; index < client->packet[4]; ++index) {
        rv_catalog_view entry;

        entry.id.size = client->packet[offset++];
        entry.id.data = client->packet + offset;
        offset += entry.id.size;
        entry.name.size = client->packet[offset++];
        entry.name.data = client->packet + offset;
        offset += entry.name.size;
        if (visit_catalog_entry(visit, &entry) != 0) {
            return 1;
        }
    }
    return 0;
}

static int visit_catalog_model(
    paket_client *client,
    const char *query,
    const char *model,
    uint8_t operation,
    catalog_visit_context *visit
)
{
    uint16_t cursor = 0U;
    uint16_t next_cursor = 0U;

    for (;;) {
        int request_size = 0;
        int response_size = 0;
        int parse_result = 0;

        request_size = build_catalog_request(
            cursor,
            operation == RV_OP_SEARCH ? query : NULL,
            model,
            client->packet
        );
        if (request_size < 0) {
            return PAKET_ERROR_ARGUMENT;
        }
        response_size = exchange_checked(
            client,
            (size_t)request_size,
            operation
        );
        if (response_size < 0) {
            return response_size;
        }
        parse_result = visit_catalog_packet(client, visit, &next_cursor);
        if (visit->error != 0) {
            return visit->error;
        }
        if (parse_result < 0) {
            return PAKET_ERROR_PROTOCOL;
        }
        if (visit->stopped || (next_cursor == RV_CURSOR_END)) {
            break;
        }
        if (next_cursor == cursor) {
            return PAKET_ERROR_PROTOCOL;
        }
        cursor = next_cursor;
    }

    return 0;
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
    uint8_t operation = RV_OP_LIST;
    int result = 0;

    if ((client == NULL) || ((pattern != NULL) && (*pattern == '\0'))) {
        return PAKET_ERROR_ARGUMENT;
    }
    memset(&visit, 0, sizeof(visit));
    visit.pattern = pattern;
    visit.visitor = visitor;
    visit.visitor_context = context;
    query[0] = '\0';
    if ((pattern != NULL) &&
        (paket_search_anchor(pattern, query, sizeof(query)) > 0U)) {
        operation = RV_OP_SEARCH;
    }

    result = visit_catalog_model(
        client,
        query,
        client->partner_type == PAKET_PARTNER_GDP
            ? NULL
            : partner_crt_model_id,
        operation,
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

static int resolve_entry(
    void *context,
    const paket_catalog_entry *entry
)
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
    int result = 0;

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
        client,
        package_name,
        resolve_entry,
        &resolve,
        NULL
    );
    if (result != 0) {
        return result;
    }
    return resolve.priority > 0 ? 0 : PAKET_ERROR_NOT_FOUND;
}

static int collect_info_entry(
    void *context,
    const rv_info_view *entry
)
{
    info_visit_context *visit = context;
    paket_package_info *info = NULL;

    if ((visit == NULL) || (entry == NULL)) {
        return 1;
    }
    info = visit->info;
    switch (entry->type) {
    case RV_INFO_ID:
        visit->error = copy_view(
            info->id, sizeof(info->id), &entry->value, 1
        ) == 0 ? 0 : PAKET_ERROR_TOO_LONG;
        break;
    case RV_INFO_NAME:
        (void)copy_view(info->name, sizeof(info->name), &entry->value, 0);
        break;
    case RV_INFO_VENDOR:
        (void)copy_view(info->vendor, sizeof(info->vendor), &entry->value, 0);
        break;
    case RV_INFO_PLATFORM_ID:
        break;
    case RV_INFO_PLATFORM_NAME:
        (void)copy_view(
            info->platform_name,
            sizeof(info->platform_name),
            &entry->value,
            0
        );
        break;
    case RV_INFO_VERSION:
        (void)copy_view(
            info->version,
            sizeof(info->version),
            &entry->value,
            0
        );
        break;
    case RV_INFO_YEAR:
        if (entry->value.size != 2U) {
            visit->error = PAKET_ERROR_PROTOCOL;
        } else {
            info->release_year = rv_get_u16(entry->value.data);
        }
        break;
    case RV_INFO_RATING:
        if (entry->value.size != 1U) {
            visit->error = PAKET_ERROR_PROTOCOL;
        } else {
            info->rating = entry->value.data[0];
        }
        break;
    case RV_INFO_DESCRIPTION:
        (void)copy_view(
            info->description,
            sizeof(info->description),
            &entry->value,
            0
        );
        break;
    case RV_INFO_DOWNLOAD: {
        rv_download_view download;
        unsigned int slot = info->stored_download_count;

        if (rv_decode_download(
            entry->value.data,
            entry->value.size,
            &download
        ) != 0) {
            visit->error = PAKET_ERROR_PROTOCOL;
            break;
        }
        if (info->download_count < 255U) {
            ++info->download_count;
        }
        if (slot < PAKET_DOWNLOAD_MAX) {
            paket_download_choice *choice = &info->downloads[slot];

            if ((copy_view(
                    choice->id,
                    sizeof(choice->id),
                    &download.id,
                    1
                ) != 0) ||
                (copy_view(
                    choice->label,
                    sizeof(choice->label),
                    &download.label,
                    0
                ) != 0)) {
                visit->error = PAKET_ERROR_TOO_LONG;
                break;
            }
            (void)copy_view(
                choice->format,
                sizeof(choice->format),
                &download.format,
                0
            );
            choice->aggregate_size = download.aggregate_size;
            choice->file_count = download.file_count;
            ++info->stored_download_count;
        }
        break;
    }
    default:
        break;
    }
    return visit->error != 0;
}

static int collect_info_packet(
    paket_client *client,
    info_visit_context *visit,
    uint16_t *next_cursor
)
{
    size_t offset = 5U;
    unsigned int index;

    *next_cursor = rv_get_u16(client->packet + 2U);
    for (index = 0U; index < client->packet[4]; ++index) {
        rv_info_view entry;

        entry.type = client->packet[offset++];
        entry.value.size = client->packet[offset++];
        entry.value.data = client->packet + offset;
        offset += entry.value.size;
        if (collect_info_entry(visit, &entry) != 0) {
            return 1;
        }
    }
    return 0;
}

int paket_fetch_info(
    paket_client *client,
    const char *package_id,
    paket_package_info *info
)
{
    info_visit_context visit;
    uint16_t cursor = 0U;
    uint16_t next_cursor = 0U;

    if ((client == NULL) || (package_id == NULL) ||
        (*package_id == '\0') || (info == NULL)) {
        return PAKET_ERROR_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));
    visit.info = info;
    visit.error = 0;

    for (;;) {
        int request_size = build_info_request(
            cursor,
            package_id,
            client->packet
        );
        int response_size = 0;
        int parse_result = 0;

        if (request_size < 0) {
            return PAKET_ERROR_TOO_LONG;
        }
        response_size = exchange_checked(
            client,
            (size_t)request_size,
            RV_OP_INFO
        );
        if (response_size < 0) {
            return response_size;
        }
        parse_result = collect_info_packet(client, &visit, &next_cursor);
        if (visit.error != 0) {
            return visit.error;
        }
        if (parse_result < 0) {
            return PAKET_ERROR_PROTOCOL;
        }
        if (next_cursor == RV_CURSOR_END) {
            break;
        }
        if (next_cursor == cursor) {
            return PAKET_ERROR_PROTOCOL;
        }
        cursor = next_cursor;
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

    if ((client == NULL) || (package_id == NULL) ||
        (download_id == NULL) || (writer == NULL)) {
        return PAKET_ERROR_ARGUMENT;
    }

    for (;;) {
        int request_size = build_download_request(
            offset,
            package_id,
            download_id,
            client->packet
        );
        int response_size = 0;
        uint32_t returned_offset = 0U;
        uint32_t returned_total = 0U;
        uint8_t data_size = 0U;

        if (request_size < 0) {
            return PAKET_ERROR_TOO_LONG;
        }
        response_size = exchange_checked(
            client,
            (size_t)request_size,
            RV_OP_DOWNLOAD
        );
        if (response_size < 0) {
            return response_size;
        }
        if (response_size < 11) {
            return PAKET_ERROR_PROTOCOL;
        }
        returned_offset = rv_get_u32(client->packet + 2U);
        returned_total = rv_get_u32(client->packet + 6U);
        data_size = client->packet[10];
        if ((returned_offset != offset) ||
            ((size_t)response_size != 11U + data_size) ||
            (offset > returned_total) ||
            ((uint32_t)data_size > returned_total - offset) ||
            (have_total && (returned_total != total_size)) ||
            ((data_size == 0U) && (offset != returned_total))) {
            return PAKET_ERROR_PROTOCOL;
        }
        total_size = returned_total;
        have_total = 1;

        if (writer(
            writer_context,
            client->packet + 11U,
            data_size,
            offset,
            total_size
        ) != 0) {
            return PAKET_ERROR_WRITE;
        }
        offset += data_size;
        if (offset == total_size) {
            break;
        }
    }
    return 0;
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
