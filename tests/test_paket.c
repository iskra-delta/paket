/*
   Exercises PAKET protocol encoding/decoding, wildcard and CP/M path logic,
   paged client operations, metadata collection, and streamed downloads under
   hosted address/undefined-behavior sanitizers.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/client.h"
#include "paket/command.h"
#include "paket/messages.h"
#include "paket/platform.h"
#include "paket/retrovault.h"
#include "paket/timebase.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned int checks;
    unsigned int failures;
} test_state;

typedef struct {
    unsigned int calls;
    unsigned int all_model_requests;
    unsigned int crt_model_requests;
    uint32_t download_total;
} mock_server;

typedef struct {
    uint8_t bytes[64];
    size_t size;
    uint32_t total;
} mock_writer;

static test_state tests;

static void expect_true(int condition, const char *message)
{
    ++tests.checks;
    if (!condition) {
        ++tests.failures;
        printf("FAIL: %s\n", message);
    }
}

static void expect_text(
    const char *actual,
    const char *expected,
    const char *message
)
{
    expect_true(
        (actual != NULL) && (strcmp(actual, expected) == 0),
        message
    );
}

static void test_platform_detection(void)
{
    expect_true(
        paket_detect_partner_type() == PAKET_PARTNER_CRT,
        "host platform detection fallback"
    );
}

static void test_serial_options(void)
{
    char *default_arguments[] = { "PAKET", "LUNATIK", NULL };
    char *leading_arguments[] = { "PAKET", "-P", "2", "LUN*", NULL };
    char *trailing_arguments[] = {
        "PAKET", "LUNATIK", "A:/1/", "-p", "4", "-C", "4800-E-2",
        NULL
    };
    char *communication_arguments[] = {
        "PAKET", "-c", "2400-n-1", "LUNATIK", "-p", "3", "-m", "64",
        NULL
    };
    char *missing_arguments[] = { "PAKET", "-p", NULL };
    char *invalid_arguments[] = { "PAKET", "-p", "5", NULL };
    char *repeated_arguments[] = {
        "PAKET", "-p", "2", "-p", "3", NULL
    };
    char *missing_communication[] = { "PAKET", "-c", NULL };
    char *invalid_baud[] = { "PAKET", "-c", "1200-N-1", NULL };
    char *invalid_parity[] = { "PAKET", "-c", "2400-X-1", NULL };
    char *invalid_stop[] = { "PAKET", "-c", "9600-N-3", NULL };
    char *invalid_format[] = { "PAKET", "-c", "9600-N-1-X", NULL };
    char *repeated_communication[] = {
        "PAKET", "-c", "2400-N-1", "-c", "9600-N-1", NULL
    };
    char *invalid_payload_low[] = { "PAKET", "-m", "15", NULL };
    char *invalid_payload_high[] = { "PAKET", "-m", "113", NULL };
    char *invalid_payload_text[] = { "PAKET", "-m", "64x", NULL };
    char *repeated_payload[] = {
        "PAKET", "-m", "64", "-M", "112", NULL
    };
    paket_options options;
    int argument_count = 2;

    expect_true(
        paket_parse_options(
            &argument_count,
            default_arguments,
            &options
        ) == 0,
        "default serial options"
    );
    expect_true(options.serial_port == 2U, "default SIO1B port");
    expect_true(
        options.payload_bytes == 64U,
        "default Squid payload stays within Partner RX flow-control window"
    );
    expect_true(!options.has_communication, "selected port uses its default");
    expect_true(argument_count == 2, "default arguments preserved");

    argument_count = 4;
    expect_true(
        paket_parse_options(
            &argument_count,
            leading_arguments,
            &options
        ) == 0,
        "leading serial option"
    );
    expect_true(
        (options.serial_port == 2U) && (argument_count == 2) &&
        (strcmp(leading_arguments[1], "LUN*") == 0),
        "leading option compacted"
    );

    argument_count = 7;
    expect_true(
        paket_parse_options(
            &argument_count,
            trailing_arguments,
            &options
        ) == 0,
        "trailing serial options"
    );
    expect_true(
        (options.serial_port == 4U) && (argument_count == 3) &&
        (strcmp(trailing_arguments[2], "A:/1/") == 0),
        "trailing options compacted"
    );
    expect_true(
        options.has_communication &&
        (options.communication.baud_rate == 4800U) &&
        (options.communication.parity == PAKET_PARITY_EVEN) &&
        (options.communication.stop_bits == 2U),
        "even parity communication parsed"
    );

    argument_count = 8;
    expect_true(
        paket_parse_options(
            &argument_count,
            communication_arguments,
            &options
        ) == 0,
        "combined serial options"
    );
    expect_true(
        (options.serial_port == 3U) && options.has_communication &&
        (options.communication.baud_rate == 2400U) &&
        (options.communication.parity == PAKET_PARITY_NONE) &&
        (options.communication.stop_bits == 1U) &&
        (options.payload_bytes == 64U) &&
        (argument_count == 2),
        "case-insensitive communication and payload parsed"
    );

    argument_count = 2;
    expect_true(
        paket_parse_options(
            &argument_count,
            missing_arguments,
            &options
        ) != 0,
        "missing port rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(
            &argument_count,
            invalid_arguments,
            &options
        ) != 0,
        "invalid port rejected"
    );
    argument_count = 5;
    expect_true(
        paket_parse_options(
            &argument_count,
            repeated_arguments,
            &options
        ) != 0,
        "repeated port rejected"
    );

    argument_count = 2;
    expect_true(
        paket_parse_options(
            &argument_count,
            missing_communication,
            &options
        ) != 0,
        "missing communication rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(&argument_count, invalid_baud, &options) != 0,
        "invalid baud rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(&argument_count, invalid_parity, &options) != 0,
        "invalid parity rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(&argument_count, invalid_stop, &options) != 0,
        "invalid stop bits rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(&argument_count, invalid_format, &options) != 0,
        "invalid communication format rejected"
    );
    argument_count = 5;
    expect_true(
        paket_parse_options(
            &argument_count,
            repeated_communication,
            &options
        ) != 0,
        "repeated communication rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(&argument_count, invalid_payload_low, &options) != 0,
        "payload below legacy frame size rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(&argument_count, invalid_payload_high, &options) != 0,
        "payload above extended frame size rejected"
    );
    argument_count = 3;
    expect_true(
        paket_parse_options(&argument_count, invalid_payload_text, &options) != 0,
        "non-numeric payload rejected"
    );
    argument_count = 5;
    expect_true(
        paket_parse_options(&argument_count, repeated_payload, &options) != 0,
        "repeated payload rejected"
    );
}

static void test_rtc_timebase(void)
{
    paket_timebase clock;
    unsigned int index;

    expect_true(
        paket_elapsed_ms(59990U, 10U) == 20U,
        "RTC millisecond difference wraps at one minute"
    );
    expect_true(
        paket_elapsed_ms(1234U, 4321U) == 3087U,
        "RTC millisecond difference without wrap"
    );

    paket_timebase_reset(&clock, 59990U);
    expect_true(
        paket_timebase_advance(&clock, 5U) == 0U,
        "protocol clock preserves partial tick across minute"
    );
    expect_true(
        paket_timebase_advance(&clock, 10U) == 1U,
        "protocol clock advances normally across minute"
    );
    expect_true(
        paket_timebase_advance(&clock, 29U) == 1U,
        "protocol clock carries sub-tick remainder"
    );
    expect_true(
        paket_timebase_advance(&clock, 30U) == 2U,
        "protocol clock completes carried tick"
    );

    paket_timebase_reset(&clock, 1000U);
    for (index = 0U; index < 256U; ++index) {
        (void)paket_timebase_advance(
            &clock,
            (uint16_t)(1000U + (index + 1U) * 20U)
        );
    }
    expect_true(clock.tick == 0U, "protocol clock wraps exactly at 256 ticks");
}

static void test_message_catalog(void)
{
    paket_message_id message_id = PAKET_MSG_RATING_CURIOUS;

    while (message_id < PAKET_MSG_COUNT) {
        const char *message = paket_message(message_id);

        expect_true(
            (message != NULL) && (message[0] != '\0'),
            "Slovenian message present"
        );
        message_id = (paket_message_id)(message_id + 1);
    }
    expect_text(
        paket_message((paket_message_id)-1),
        "neznano stanje streznika",
        "invalid message fallback"
    );
}

static void test_wildcards(void)
{
    char anchor[11];

    expect_true(paket_has_wildcards("LUN*") != 0, "detect star");
    expect_true(paket_has_wildcards("LUNATIK") == 0, "exact has no star");
    expect_true(
        paket_wildcard_match("LUN*", "Lunatik") != 0,
        "case-insensitive star match"
    );
    expect_true(
        paket_wildcard_match("?AH", "Sah") != 0,
        "question-mark match"
    );
    expect_true(
        paket_wildcard_match("URA", "Lunatik") == 0,
        "wildcard rejection"
    );
    expect_true(
        paket_search_anchor("*KONTRABANT?LONG", anchor, sizeof(anchor)) == 10U,
        "search anchor frame cap"
    );
    expect_text(anchor, "KONTRABANT", "longest search anchor");
}

static void test_paths(void)
{
    char path[PAKET_TARGET_SIZE];
    char exact_path[17];

    expect_true(
        paket_make_target_path(
            "A:/",
            "LUNATIK",
            "COM",
            1U,
            path,
            sizeof(path)
        ) == 0,
        "root target accepted"
    );
    expect_text(path, "A:LUNATIK.COM[0]", "root maps to user zero");
    expect_true(
        paket_make_target_path(
            "A:/",
            "LUNATIK",
            "COM",
            1U,
            exact_path,
            sizeof(exact_path)
        ) == 0,
        "exact-size target buffer accepted"
    );
    expect_true(
        paket_make_target_path(
            "A:/",
            "LUNATIK",
            "COM",
            1U,
            exact_path,
            sizeof(exact_path) - 1U
        ) != 0,
        "short target buffer rejected"
    );

    expect_true(
        paket_make_target_path(
            "a:/1/",
            "LUNATIK",
            "COM",
            1U,
            path,
            sizeof(path)
        ) == 0,
        "user target accepted"
    );
    expect_text(path, "A:LUNATIK.COM[1]", "folder maps to user one");

    expect_true(
        paket_make_target_path(
            "B:/15/CUSTOM.BIN",
            "IGNORED",
            "COM",
            1U,
            path,
            sizeof(path)
        ) == 0,
        "explicit target accepted"
    );
    expect_text(path, "B:CUSTOM.BIN[15]", "explicit CP/M path");

    expect_true(
        paket_make_target_path(
            "A:/2/",
            "idp-package-long",
            "ADF",
            2U,
            path,
            sizeof(path)
        ) == 0,
        "multi-file target accepted"
    );
    expect_text(path, "A:IDPPACKA.ZIP[2]", "8.3 and multi-file ZIP");

    expect_true(
        paket_make_target_path(
            "A:/16/",
            "LUNATIK",
            "COM",
            1U,
            path,
            sizeof(path)
        ) != 0,
        "user sixteen rejected"
    );
    expect_true(
        paket_make_target_path(
            "A:/1/TOO-LONG-NAME.COM",
            "LUNATIK",
            "COM",
            1U,
            path,
            sizeof(path)
        ) != 0,
        "long explicit name rejected"
    );
}

static void test_protocol(void)
{
    uint8_t request[RV_PACKET_MAX];
    const uint8_t list_response[] = {
        0x81U, 0x00U, 0xffU, 0xffU, 0x01U,
        0x07U, 'l', 'u', 'n', 'a', 't', 'i', 'k',
        0x07U, 'L', 'u', 'n', 'a', 't', 'i', 'k'
    };
    int size = 0;
    size_t index = 0U;

    size = rv_build_list(
        0U,
        "idp",
        NULL,
        request,
        sizeof(request)
    );
    expect_true(size == 7, "model-agnostic list request size");
    expect_true(
        (request[0] == RV_OP_LIST) && (request[3] == 3U) &&
        (memcmp(request + 4U, "idp", 3U) == 0),
        "model-agnostic list omits the optional field"
    );

    size = rv_build_search(
        0x1234U,
        "idp",
        "LUNATIK",
        "p",
        request,
        sizeof(request)
    );
    expect_true(size == 17, "model-filtered search request size");
    expect_true(
        (request[0] == RV_OP_SEARCH) &&
        (request[1] == 0x34U) && (request[2] == 0x12U) &&
        (request[3] == 3U) &&
        (memcmp(request + 4U, "idp", 3U) == 0) &&
        (request[7] == 7U) &&
        (request[15] == 1U) && (request[16] == 'p'),
        "platform and model search request fields"
    );

    size = rv_build_download(
        0x12345678UL,
        20U,
        "lunatik",
        "complete",
        request,
        sizeof(request)
    );
    expect_true(size == 23, "download request size");
    expect_true(
        rv_get_u32(request + 1U) == 0x12345678UL,
        "download request offset"
    );

    for (index = 0U; index < sizeof(list_response); ++index) {
        expect_true(
            rv_response_size(list_response, index) == 0,
            "partial catalog response waits"
        );
    }
    expect_true(
        rv_response_size(list_response, sizeof(list_response)) ==
            (int)sizeof(list_response),
        "complete catalog response size"
    );
}

static size_t append_text_tlv(
    uint8_t *response,
    size_t offset,
    uint8_t type,
    const char *text
)
{
    size_t size = strlen(text);

    response[offset++] = type;
    response[offset++] = (uint8_t)size;
    memcpy(response + offset, text, size);
    return offset + size;
}

static int mock_catalog_response(
    uint8_t operation,
    uint8_t *response,
    size_t response_capacity
)
{
    const char *id = "lunatik";
    const char *name = "Lunatik";
    size_t id_size = strlen(id);
    size_t name_size = strlen(name);
    size_t required = 7U + id_size + name_size;

    if (required > response_capacity) {
        return -1;
    }
    response[0] = (uint8_t)(operation | RV_RESPONSE_BIT);
    response[1] = RV_STATUS_OK;
    rv_put_u16(response + 2U, RV_CURSOR_END);
    response[4] = 1U;
    response[5] = (uint8_t)id_size;
    memcpy(response + 6U, id, id_size);
    response[6U + id_size] = (uint8_t)name_size;
    memcpy(response + 7U + id_size, name, name_size);
    return (int)required;
}

static int mock_info_response(uint8_t *response, size_t response_capacity)
{
    const char *download_id = "complete";
    const char *label = "Complete";
    const char *format = "COM";
    size_t offset = 5U;
    size_t value_start = 0U;

    if (response_capacity < 96U) {
        return -1;
    }
    response[0] = RV_OP_INFO | RV_RESPONSE_BIT;
    response[1] = RV_STATUS_OK;
    rv_put_u16(response + 2U, RV_CURSOR_END);
    response[4] = 6U;
    offset = append_text_tlv(response, offset, RV_INFO_ID, "lunatik");
    offset = append_text_tlv(response, offset, RV_INFO_NAME, "Lunatik");
    offset = append_text_tlv(response, offset, RV_INFO_PLATFORM_ID,
        "idp");
    response[offset++] = RV_INFO_YEAR;
    response[offset++] = 2U;
    rv_put_u16(response + offset, 2026U);
    offset += 2U;
    response[offset++] = RV_INFO_RATING;
    response[offset++] = 1U;
    response[offset++] = 4U;
    response[offset++] = RV_INFO_DOWNLOAD;
    value_start = offset++;
    response[offset++] = (uint8_t)strlen(download_id);
    memcpy(response + offset, download_id, strlen(download_id));
    offset += strlen(download_id);
    response[offset++] = (uint8_t)strlen(label);
    memcpy(response + offset, label, strlen(label));
    offset += strlen(label);
    response[offset++] = (uint8_t)strlen(format);
    memcpy(response + offset, format, strlen(format));
    offset += strlen(format);
    rv_put_u32(response + offset, 45U);
    offset += 4U;
    response[offset++] = 1U;
    response[value_start] = (uint8_t)(offset - value_start - 1U);
    return (int)offset;
}

static int mock_exchange(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    mock_server *server = context;

    ++server->calls;
    if ((request == NULL) || (request_size == 0U)) {
        return -1;
    }
    switch (request[0]) {
    case RV_OP_CAPABILITIES:
        if (response_capacity < 7U) {
            return -1;
        }
        response[0] = RV_OP_CAPABILITIES | RV_RESPONSE_BIT;
        response[1] = RV_STATUS_OK;
        response[2] = 1U;
        response[3] = 0x0fU;
        response[4] = 5U;
        response[5] = 11U;
        response[6] = 243U;
        return 7;
    case RV_OP_LIST:
    case RV_OP_SEARCH: {
        size_t offset = 4U;
        uint8_t platform_size = request[3];

        expect_true(
            (platform_size == 3U) && (request_size >= 7U) &&
            (memcmp(request + offset, "idp", 3U) == 0),
            "client uses idp platform namespace"
        );
        offset += platform_size;
        if (request[0] == RV_OP_SEARCH) {
            if (offset >= request_size) {
                return -1;
            }
            offset += 1U + request[offset];
        }
        if (offset == request_size) {
            ++server->all_model_requests;
            return mock_catalog_response(
                request[0], response, response_capacity
            );
        }
        expect_true(
            (request_size - offset == 2U) &&
            (request[offset] == 1U) && (request[offset + 1U] == 'p'),
            "CRT client uses exact p model filter"
        );
        ++server->crt_model_requests;
        if (response_capacity < 5U) {
            return -1;
        }
        response[0] = (uint8_t)(request[0] | RV_RESPONSE_BIT);
        response[1] = RV_STATUS_OK;
        rv_put_u16(response + 2U, RV_CURSOR_END);
        response[4] = 0U;
        return 5;
    }
        return mock_catalog_response(request[0], response, response_capacity);
    case RV_OP_INFO:
        return mock_info_response(response, response_capacity);
    case RV_OP_DOWNLOAD: {
        uint32_t offset = rv_get_u32(request + 1U);
        uint8_t chunk = 0U;
        unsigned int index = 0U;

        expect_true(request[5] == 244U, "full-packet download request");
        if (offset > server->download_total) {
            return -1;
        }
        chunk = (uint8_t)(server->download_total - offset);
        if (chunk > 17U) {
            chunk = 17U;
        }
        if (response_capacity < 11U + chunk) {
            return -1;
        }
        response[0] = RV_OP_DOWNLOAD | RV_RESPONSE_BIT;
        response[1] = RV_STATUS_OK;
        rv_put_u32(response + 2U, offset);
        rv_put_u32(response + 6U, server->download_total);
        response[10] = chunk;
        for (index = 0U; index < chunk; ++index) {
            response[11U + index] = (uint8_t)(offset + index);
        }
        return (int)(11U + chunk);
    }
    default:
        return -1;
    }
}

static int collect_download(
    void *context,
    const uint8_t *data,
    size_t data_size,
    uint32_t offset,
    uint32_t total_size
)
{
    mock_writer *writer = context;

    if ((writer == NULL) || (offset != writer->size) ||
        (writer->size + data_size > sizeof(writer->bytes))) {
        return -1;
    }
    memcpy(writer->bytes + writer->size, data, data_size);
    writer->size += data_size;
    writer->total = total_size;
    return 0;
}

static void test_client(void)
{
    paket_client client;
    paket_catalog_entry entry;
    paket_package_info info;
    mock_server server;
    mock_writer writer;
    unsigned int index = 0U;

    memset(&server, 0, sizeof(server));
    memset(&writer, 0, sizeof(writer));
    server.download_total = 45U;
    paket_client_init(&client, mock_exchange, &server);

    expect_true(
        paket_check_capabilities(&client) == 0,
        "capability negotiation"
    );
    expect_true(
        paket_resolve_package(&client, "LUNATIK", &entry) ==
            PAKET_ERROR_NOT_FOUND,
        "CRT p filter returns no current packages"
    );
    expect_true(server.crt_model_requests == 1U, "CRT issued one p request");
    paket_client_set_partner_type(&client, PAKET_PARTNER_GDP);
    expect_true(
        paket_resolve_package(&client, "LUNATIK", &entry) == 0,
        "GDP package name resolution"
    );
    expect_true(
        server.all_model_requests == 1U,
        "GDP omitted model and issued one request"
    );
    expect_text(entry.id, "lunatik", "resolved package slug");
    expect_true(
        paket_fetch_info(&client, entry.id, &info) == 0,
        "package info collection"
    );
    expect_text(info.name, "Lunatik", "info name");
    expect_true(info.release_year == 2026U, "info release year");
    expect_true(info.rating == 4U, "info rating");
    expect_true(info.download_count == 1U, "info download count");
    expect_text(info.downloads[0].id, "complete", "download ID");

    expect_true(
        paket_fetch_download(
            &client,
            entry.id,
            info.downloads[0].id,
            collect_download,
            &writer
        ) == 0,
        "streamed download"
    );
    expect_true(writer.size == 45U, "download byte count");
    expect_true(writer.total == 45U, "download total");
    for (index = 0U; index < writer.size; ++index) {
        if (writer.bytes[index] != (uint8_t)index) {
            expect_true(0, "download byte sequence");
            break;
        }
    }
    if (index == writer.size) {
        expect_true(1, "download byte sequence");
    }
}

int main(void)
{
    test_platform_detection();
    test_serial_options();
    test_rtc_timebase();
    test_message_catalog();
    test_wildcards();
    test_paths();
    test_protocol();
    test_client();

    if (tests.failures != 0U) {
        printf(
            "%u of %u checks failed\n",
            tests.failures,
            tests.checks
        );
        return 1;
    }
    printf("all %u checks passed\n", tests.checks);
    return 0;
}
