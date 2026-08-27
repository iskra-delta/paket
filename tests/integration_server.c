/*
   Runs PAKET's real client against squid-server over its local Unix socket.
   This exercises libsquid channel 3, Retro Vault capabilities, catalog search,
   exact package resolution, and paged INFO decoding without Partner hardware.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#define _POSIX_C_SOURCE 200809L

#include "paket/client.h"
#include "paket/retrovault.h"

#include <squid/snet.h>
#include <squid/socket.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define integration_socket_path "/tmp/squid_server.sock"
#define integration_channel 3U
#define integration_queue_size 256U

typedef struct {
    int socket_fd;
    int squid_fd;
    uint8_t epoch;
    uint8_t transmit_packet[RV_PACKET_MAX + 1U];
} integration_link;

typedef struct {
    uint32_t size;
    uint32_t total;
    uint32_t hash;
} integration_download;

static int active_socket = -1;

static uint64_t milliseconds(void)
{
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U +
        (uint64_t)now.tv_nsec / 1000000U;
}

static void sleep_five_milliseconds(void)
{
    const struct timespec delay = { 0, 5000000L };

    (void)nanosleep(&delay, NULL);
}

static int local_send(uint8_t character)
{
    return send(active_socket, &character, 1U, MSG_NOSIGNAL) == 1 ? 0 : -1;
}

static int local_receive(void)
{
    uint8_t character = 0U;
    ssize_t received = recv(active_socket, &character, 1U, MSG_DONTWAIT);

    return received == 1 ? (int)character : -1;
}

static uint8_t local_tick(void)
{
    return (uint8_t)(milliseconds() / 20U);
}

static int open_integration_link(integration_link *link)
{
    struct sockaddr_un address;
    squid_platform_t platform;
    squid_timing_t timing;
    uint64_t started = 0U;

    memset(link, 0, sizeof(*link));
    link->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    link->squid_fd = -1;
    if (link->socket_fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(
        address.sun_path,
        integration_socket_path,
        sizeof(integration_socket_path)
    );
    if (connect(
        link->socket_fd,
        (const struct sockaddr *)&address,
        sizeof(address)
    ) != 0) {
        (void)close(link->socket_fd);
        return -1;
    }
    active_socket = link->socket_fd;
    platform.send_char = local_send;
    platform.recv_char = local_receive;
    platform.get_tick = local_tick;
    platform.mem_alloc = malloc;
    platform.mem_free = free;
    timing.timeout_ticks = 200U;
    timing.ack_delay_ticks = 2U;
    timing.ping_ticks = 0U;
    timing.max_retries = 3U;
    timing.payload_bytes = 112U;
    snet_init(&platform, &timing);
    link->squid_fd = squid_open(
        integration_queue_size,
        integration_queue_size
    );
    if ((link->squid_fd < 0) ||
        (squid_connect(link->squid_fd, integration_channel) != 0)) {
        (void)close(link->socket_fd);
        active_socket = -1;
        return -1;
    }

    started = milliseconds();
    while (!snet_link_is_up()) {
        snet_burst();
        if (milliseconds() - started > 5000U) {
            return -1;
        }
        sleep_five_milliseconds();
    }
    link->epoch = snet_link_epoch();
    return 0;
}

static void close_integration_link(integration_link *link)
{
    if (link->squid_fd >= 0) {
        squid_close(link->squid_fd);
        link->squid_fd = -1;
    }
    if (link->socket_fd >= 0) {
        (void)close(link->socket_fd);
        link->socket_fd = -1;
    }
    active_socket = -1;
}

static int integration_exchange(
    void *context,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_capacity
)
{
    integration_link *link = context;
    uint64_t started = milliseconds();
    size_t expected = 0U;
    size_t received = 0U;

    if ((request_size == 0U) || (request_size > RV_PACKET_MAX)) {
        return -1;
    }
    link->transmit_packet[0] = (uint8_t)request_size;
    memcpy(link->transmit_packet + 1U, request, request_size);

    while (squid_send(
        link->squid_fd,
        link->transmit_packet,
        (uint16_t)(request_size + 1U)
    ) < 0) {
        snet_burst();
        if (milliseconds() - started > 5000U) {
            return -1;
        }
        sleep_five_milliseconds();
    }

    started = milliseconds();
    for (;;) {
        int copied = 0;
        int pending = 0;

        snet_burst();
        if (!snet_link_is_up() || (snet_link_epoch() != link->epoch)) {
            return -1;
        }
        pending = squid_rx_pending(link->squid_fd);
        if (pending < 0) {
            return -1;
        }
        if ((pending > 0) && (expected == 0U)) {
            uint8_t wire_size = 0U;

            copied = squid_recv(link->squid_fd, &wire_size, 1U);
            if ((copied != 1) || (wire_size == 0U) ||
                ((size_t)wire_size > response_capacity)) {
                return -1;
            }
            expected = wire_size;
            --pending;
        }
        if ((pending > 0) && (received < expected)) {
            size_t remaining = expected - received;

            copied = squid_recv(
                link->squid_fd,
                response + received,
                (uint16_t)remaining
            );
            if (copied < 0) {
                return -1;
            }
            received += (size_t)copied;
        }
        if ((expected > 0U) && (received == expected)) {
            return (int)received;
        }
        if ((received == response_capacity) ||
            (milliseconds() - started > 30000U)) {
            return -1;
        }
        sleep_five_milliseconds();
    }
}

static int print_entry(
    void *context,
    const paket_catalog_entry *entry
)
{
    unsigned int *count = context;

    ++*count;
    printf("%s | %s\n", entry->id, entry->name);
    return 0;
}

static int collect_download(
    void *context,
    const uint8_t *data,
    size_t data_size,
    uint32_t offset,
    uint32_t total_size
)
{
    integration_download *download = context;
    size_t index = 0U;

    if ((download == NULL) || (data == NULL) ||
        (offset != download->size)) {
        return -1;
    }
    for (index = 0U; index < data_size; ++index) {
        download->hash ^= data[index];
        download->hash *= UINT32_C(16777619);
    }
    download->size += (uint32_t)data_size;
    download->total = total_size;
    return 0;
}

int main(void)
{
    integration_link link;
    paket_client client;
    paket_catalog_entry entry;
    paket_package_info info;
    integration_download download;
    unsigned int count = 0U;
    int download_skipped = 0;
    int require_download = getenv("PAKET_REQUIRE_DOWNLOAD") != NULL;
    int result = 0;

    memset(&download, 0, sizeof(download));
    download.hash = UINT32_C(2166136261);

    if (open_integration_link(&link) != 0) {
        fputs("cannot connect to squid-server\n", stderr);
        return 1;
    }
    paket_client_init(&client, integration_exchange, &link);
    paket_client_set_partner_type(&client, PAKET_PARTNER_GDP);
    result = paket_check_capabilities(&client);
    if (result == 0) {
        result = paket_visit_catalog(
            &client,
            "LUN*",
            print_entry,
            &count,
            NULL
        );
    }
    if ((result == 0) && (count == 0U)) {
        result = PAKET_ERROR_NOT_FOUND;
    }
    if (result == 0) {
        result = paket_resolve_package(&client, "LUNATIK", &entry);
    }
    if (result == 0) {
        result = paket_fetch_info(&client, entry.id, &info);
    }
    if (result == 0) {
        printf(
            "resolved %s, platform %s, %u download(s)\n",
            info.id,
            info.platform_name,
            (unsigned int)info.download_count
        );
    }
    if ((result == 0) && (info.stored_download_count == 0U)) {
        if (require_download) {
            result = PAKET_ERROR_NOT_FOUND;
        } else {
            puts("download skipped: package has no current download");
            download_skipped = 1;
        }
    } else if (result == 0) {
        printf(
            "downloading %s (%s)\n",
            info.downloads[0].id,
            info.downloads[0].format
        );
        result = paket_fetch_download(
            &client,
            entry.id,
            info.downloads[0].id,
            collect_download,
            &download
        );
    }
    if ((result == 0) && !download_skipped) {
        printf(
            "downloaded %lu byte(s), FNV-1a %08lx\n",
            (unsigned long)download.size,
            (unsigned long)download.hash
        );
    } else if (result != 0) {
        fprintf(stderr, "integration failed: %s\n", paket_result_text(result));
    }
    close_integration_link(&link);
    return result == 0 ? 0 : 1;
}
