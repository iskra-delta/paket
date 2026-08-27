/*
   Adapts Partner interrupt-driven serial services to libsquid and presents one
   synchronous request/response exchange on Retro Vault channel 3. The chosen
   port uses its default line profile unless the command line overrides it.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/link.h"

#include "paket/partner_io.h"
#include "paket/retrovault.h"
#include "paket/timebase.h"

#include <squid/snet.h>
#include <squid/socket.h>
#include <squid_client/base.h>

#include <string.h>
#include <sys/bdos.h>

#define paket_squid_channel 3U
#define paket_serial_output_size 128U
/* The receive ring is flow-controlled independently of negotiated framing. */
#define paket_serial_input_size 64U
#define paket_serial_input_low_water 16U
#define paket_connect_timeout_ms 15000U
#define paket_scb_function 49U
#define paket_scb_common_base_offset 0x5dU
#define paket_common_first 0xc100U
#define paket_common_last 0xc2a5U

_Static_assert(
    paket_serial_output_size >= SQUID_PLATFORM_TX_BURST_MAX,
    "PAKET TX ring must accept one complete libsquid burst"
);

static squid_platform_t squid_platform;
static squid_timing_t squid_timing;
static squid_client_t retro_client;
static uint8_t retro_workspace[SQUID_CLIENT_WORKSPACE_SIZE];
static uint8_t squid_allocator[2][SQUID_CLIENT_WORKSPACE_SIZE + 1U];
static uint8_t squid_allocator_next;
static paket_timebase protocol_clock;
extern int serial_fault;
static int serial_open;
static int serial_selected_channel_b;
/* serial_isr.s masks the RTC from the startup initializer, before main() and
   even before CP/M's argument construction can be interrupted.  0xff means
   the early hook was not reached (and is deliberately initialized data, not
   BSS, so crt0 cannot erase a value captured by the hook). */
uint8_t paket_rtc_early_control = 0xffU;
static uint8_t rtc_interrupt_control;
static int rtc_interrupt_masked;

#define partner_sio1_data_b 0xdaU
#define partner_sio1_ctrl_b 0xdbU
#define partner_sio2_data_a 0xe0U
#define partner_sio2_ctrl_a 0xe1U
#define partner_sio2_data_b 0xe2U
#define partner_sio2_ctrl_b 0xe3U
#define partner_sio_tx_empty 0x04U
#define partner_rtc_interrupt_status 0xb0U
#define partner_rtc_interrupt_control 0xb1U
#define partner_sio_wr5_base 0x68U
#define partner_sio_wr5_rts 0x02U
#define partner_sio_wr5_dtr 0x80U
#define partner_sio_wr1_status_vector 0x04U
#define partner_sio_wr1_interrupts 0x1eU
#define partner_sio_wr3_auto_enable 0x20U

#define partner_baud_9600 0x40U
#define partner_baud_4800 0x80U
#define partner_baud_2400 0xc0U
#define partner_parity_none 0x00U
#define partner_parity_odd 0x01U
#define partner_parity_even 0x03U
#define partner_stop_bits_1 0x04U
#define partner_stop_bits_2 0x0cU

/* Storage and IM2 handlers live in serial_isr.s so the hot receive path is
   short enough to protect the Z80 SIO's three-byte hardware FIFO. */
extern volatile uint8_t paket_serial_data_port;
extern volatile uint8_t paket_serial_control_port;
extern volatile uint8_t paket_serial_wr5;
extern volatile uint8_t paket_serial_tx_vector;
extern volatile uint8_t paket_serial_rx_vector;
extern volatile uint8_t paket_serial_special_vector;
extern volatile uint8_t paket_serial_output[paket_serial_output_size];
extern volatile uint8_t paket_serial_output_head;
extern volatile uint8_t paket_serial_output_tail;
extern volatile uint8_t paket_serial_output_count;
extern volatile uint8_t paket_serial_input[paket_serial_input_size];
extern volatile uint8_t paket_serial_input_head;
extern volatile uint8_t paket_serial_input_tail;
extern volatile uint8_t paket_serial_input_count;

uint8_t paket_serial_interrupt_lock(void) PAKET_REGISTER_ABI;
void paket_serial_interrupt_unlock(uint8_t enabled) PAKET_REGISTER_ABI;
void paket_serial_prepare_common(void);
void paket_serial_install_vectors(void);
void paket_serial_restore_vectors(void);

static void *squid_allocate(size_t size)
{
    if ((size > sizeof(squid_allocator[0])) ||
        (squid_allocator_next >= 2U)) {
        return NULL;
    }
    return squid_allocator[squid_allocator_next++];
}

static void squid_deallocate(void *memory)
{
    (void)memory;
}

typedef struct {
    unsigned int number;
    uint8_t data_port;
    uint8_t control_port;
    uint8_t vector_control_port;
    uint8_t vector_offset;
    uint8_t baud_rate;
    uint8_t parity;
    uint8_t stop_bits;
} paket_port_profile;

typedef struct {
    uint8_t offset;
    uint8_t operation;
    uint16_t value;
} paket_scb_parameter;

/* Defaults are kept per physical port even where their current values match. */
static const paket_port_profile port_profiles[] = {
    /* Channel B owns each SIO's shared WR2/RR2 interrupt-vector register.
       Channel A sources occupy the upper four status-vector slots. */
    { 2U, partner_sio1_data_b, partner_sio1_ctrl_b, partner_sio1_ctrl_b, 0U,
      partner_baud_9600, partner_parity_none, partner_stop_bits_1 },
    { 3U, partner_sio2_data_a, partner_sio2_ctrl_a, partner_sio2_ctrl_b, 8U,
      partner_baud_9600, partner_parity_none, partner_stop_bits_1 },
    { 4U, partner_sio2_data_b, partner_sio2_ctrl_b, partner_sio2_ctrl_b, 0U,
      partner_baud_9600, partner_parity_none, partner_stop_bits_1 }
};

static void serial_write_wr5(uint8_t value)
{
    paket_port_out(paket_serial_control_port, 5U);
    paket_port_out(paket_serial_control_port, value);
    paket_serial_wr5 = value;
}

static int serial_common_memory_available(void)
{
    paket_scb_parameter parameter;
    bdos_ret_t returned;
    uint16_t common_base;
    uint16_t resident_floor;

    /* CP/M 3 Function 49 reports the first common byte. Page zero's BDOS JP
       operand identifies the lowest resident entry; rounding it down gives
       the resident program's first page.  The active transient owns the TPA
       between those boundaries. Keep the loader/bank gateway in the first
       common page untouched and verify our complete ISR block fits below the
       resident program before changing the Partner IM2 table. */
    parameter.offset = paket_scb_common_base_offset;
    parameter.operation = 0U;
    parameter.value = 0U;
    memset(&returned, 0, sizeof(returned));
    bdosret(paket_scb_function, (uint16_t)&parameter, &returned);
    common_base = returned.rethl;
    if ((*(volatile uint8_t *)0x0005U != 0xc3U) ||
        (common_base == 0U) || ((common_base & 0x00ffU) != 0U) ||
        (common_base > (paket_common_first - 0x0100U))) {
        return 0;
    }
    resident_floor = (uint16_t)(
        (*(volatile uint16_t *)0x0006U) & 0xff00U
    );
    return (paket_common_first >= (uint16_t)(common_base + 0x0100U)) &&
        (paket_common_last < resident_floor);
}

static void rtc_mask_interrupts(void)
{
    if (rtc_interrupt_masked) {
        return;
    }
    if (paket_rtc_early_control != 0xffU) {
        rtc_interrupt_control = paket_rtc_early_control;
        paket_rtc_early_control = 0xffU;
    } else {
        rtc_interrupt_control = paket_port_in(partner_rtc_interrupt_control);
    }
    paket_port_out(partner_rtc_interrupt_control, 0U);
    /* Reading status acknowledges a source already pending at program entry.
       CP/M's 0066h NMI path re-enters the transient program at 0100h, which
       would otherwise restart a long download on the next minute rollover. */
    (void)paket_port_in(partner_rtc_interrupt_status);
    rtc_interrupt_masked = 1;
}

static void rtc_restore_interrupts(void)
{
    if (!rtc_interrupt_masked) {
        return;
    }
    /* A minute edge can latch while B1 is masked during a long transfer.
       Acknowledge it before restoring the saved enable bits; otherwise the
       GDP BIOS repeatedly enters its non-resident 0066h NMI path after PAKET
       returns and channel-A keyboard interrupts never get CPU time. */
    (void)paket_port_in(partner_rtc_interrupt_status);
    paket_port_out(partner_rtc_interrupt_control, rtc_interrupt_control);
    rtc_interrupt_masked = 0;
}

void paket_link_process_begin(void)
{
    rtc_mask_interrupts();
}

void paket_link_process_end(void)
{
    rtc_restore_interrupts();
}

static int serial_initialize(
    const paket_port_profile *profile,
    uint8_t baud_rate,
    uint8_t parity,
    uint8_t stop_bits
)
{
    static const uint8_t setup[] = {
        0x30U, 0x18U, 0x04U, 0x00U, 0x05U,
        partner_sio_wr5_base, 0x01U, partner_sio_wr1_interrupts,
        0x03U, (uint8_t)(0xc1U | partner_sio_wr3_auto_enable)
    };
    uint8_t interrupts_enabled;
    uint8_t vector_base;
    uint8_t index;

    if (!serial_common_memory_available()) {
        return -1;
    }
    interrupts_enabled = paket_serial_interrupt_lock();
    paket_serial_prepare_common();
    paket_serial_data_port = profile->data_port;
    paket_serial_control_port = profile->control_port;
    paket_serial_wr5 = partner_sio_wr5_base;
    paket_serial_output_head = 0U;
    paket_serial_output_tail = 0U;
    paket_serial_output_count = 0U;
    paket_serial_input_head = 0U;
    paket_serial_input_tail = 0U;
    paket_serial_input_count = 0U;
    serial_selected_channel_b =
        profile->control_port == profile->vector_control_port;

    /* WR1 bit 2 in channel B is the shared status-vector enable for both
       channels. Keep it set while PAKET owns either half of the SIO so the
       selected TX/RX causes reach their distinct IM2 words. */
    paket_port_out(profile->vector_control_port, 1U);
    paket_port_out(
        profile->vector_control_port,
        partner_sio_wr1_status_vector
    );

    /* RR2 on channel B exposes the shared base vector.  With status-vector
       mode enabled its cause bits vary, so retain only the programmable bits
       and add the selected channel's TX/RX source offsets. */
    paket_port_out(profile->vector_control_port, 2U);
    vector_base = (uint8_t)(
        paket_port_in(profile->vector_control_port) & 0xf1U
    );
    paket_serial_tx_vector = (uint8_t)(
        vector_base | profile->vector_offset
    );
    paket_serial_rx_vector = (uint8_t)(
        vector_base | profile->vector_offset | 0x04U
    );
    paket_serial_special_vector = (uint8_t)(
        vector_base | profile->vector_offset | 0x06U
    );
    paket_serial_install_vectors();
    serial_open = 1;

    for (index = 0U; index < sizeof(setup); ++index) {
        uint8_t value = setup[index];

        if (index == 3U) {
            value = (uint8_t)(baud_rate | parity | stop_bits);
        }
        paket_port_out(paket_serial_control_port, value);
    }
    serial_write_wr5((uint8_t)(
        partner_sio_wr5_base | partner_sio_wr5_rts | partner_sio_wr5_dtr
    ));
    paket_serial_interrupt_unlock(interrupts_enabled);

    /* A transient entered with maskable interrupts disabled cannot use this
       driver safely.  Restore the machine rather than silently falling back
       to the FIFO-overrun-prone polling path. */
    if (interrupts_enabled == 0U) {
        paket_serial_restore_vectors();
        serial_open = 0;
        return -1;
    }
    return 0;
}

static int serial_send_character(uint8_t character)
{
    uint8_t interrupts_enabled = paket_serial_interrupt_lock();

    if (!serial_open ||
        (paket_serial_output_count >= paket_serial_output_size)) {
        serial_fault = 1;
        paket_serial_interrupt_unlock(interrupts_enabled);
        return -1;
    }
    paket_serial_output[paket_serial_output_head] = character;
    paket_serial_output_head = (uint8_t)(
        (paket_serial_output_head + 1U) &
        (paket_serial_output_size - 1U)
    );
    ++paket_serial_output_count;

    /* Writing the first byte directly rearms the TX-empty interrupt.  The
       ISR drains all successors and the SIO's Auto Enables gate them on CTS. */
    if ((paket_port_in(paket_serial_control_port) &
         partner_sio_tx_empty) != 0U) {
        paket_port_out(
            paket_serial_data_port,
            paket_serial_output[paket_serial_output_tail]
        );
        paket_serial_output_tail = (uint8_t)(
            (paket_serial_output_tail + 1U) &
            (paket_serial_output_size - 1U)
        );
        --paket_serial_output_count;
    }
    paket_serial_interrupt_unlock(interrupts_enabled);
    return 0;
}

static int serial_receive_character(void)
{
    uint8_t interrupts_enabled = paket_serial_interrupt_lock();
    uint8_t character;

    if (!serial_open || (paket_serial_input_count == 0U)) {
        paket_serial_interrupt_unlock(interrupts_enabled);
        return -1;
    }
    character = paket_serial_input[paket_serial_input_tail];
    paket_serial_input_tail = (uint8_t)(
        (paket_serial_input_tail + 1U) &
        (paket_serial_input_size - 1U)
    );
    --paket_serial_input_count;
    if ((paket_serial_input_count <= paket_serial_input_low_water) &&
        ((paket_serial_wr5 & partner_sio_wr5_rts) == 0U)) {
        serial_write_wr5((uint8_t)(
            paket_serial_wr5 | partner_sio_wr5_rts
        ));
    }
    paket_serial_interrupt_unlock(interrupts_enabled);
    return (int)character;
}

static uint8_t serial_tick(void)
{
    /* The RTC millisecond reading wraps every minute, which is not a multiple
       of the 256-step libsquid clock. Accumulate real RTC deltas so that a
       minute rollover is one ordinary tick. */
    return paket_timebase_advance(&protocol_clock, paket_partner_timer_ms());
}

static void pump_link(void)
{
    if (!serial_open) {
        serial_fault = 1;
        return;
    }
    snet_burst();
}

static int client_idle(void *context)
{
    (void)context;
    pump_link();
    return serial_fault != 0;
}

int paket_link_open(
    paket_link *link,
    unsigned int serial_port_number,
    const paket_communication *communication,
    unsigned int payload_bytes
)
{
    const paket_port_profile *profile = NULL;
    uint8_t baud_rate;
    uint8_t parity;
    uint8_t stop_bits;
    size_t profile_index = 0U;
    uint16_t started = 0U;

    if ((link == NULL) || (payload_bytes < SQUID_PAYLOAD_DEFAULT) ||
        (payload_bytes > SQUID_PAYLOAD_MAX)) {
        return -1;
    }
    for (profile_index = 0U;
         profile_index < sizeof(port_profiles) / sizeof(port_profiles[0]);
         ++profile_index) {
        if (port_profiles[profile_index].number == serial_port_number) {
            profile = &port_profiles[profile_index];
            break;
        }
    }
    if (profile == NULL) {
        return -1;
    }
    baud_rate = profile->baud_rate;
    parity = profile->parity;
    stop_bits = profile->stop_bits;
    if (communication != NULL) {
        switch (communication->baud_rate) {
        case 2400U:
            baud_rate = partner_baud_2400;
            break;
        case 4800U:
            baud_rate = partner_baud_4800;
            break;
        case 9600U:
            baud_rate = partner_baud_9600;
            break;
        default:
            return -1;
        }
        switch (communication->parity) {
        case PAKET_PARITY_NONE:
            parity = partner_parity_none;
            break;
        case PAKET_PARITY_ODD:
            parity = partner_parity_odd;
            break;
        case PAKET_PARITY_EVEN:
            parity = partner_parity_even;
            break;
        default:
            return -1;
        }
        switch (communication->stop_bits) {
        case 1U:
            stop_bits = partner_stop_bits_1;
            break;
        case 2U:
            stop_bits = partner_stop_bits_2;
            break;
        default:
            return -1;
        }
    }
    memset(link, 0, sizeof(*link));
    link->socket_fd = -1;
    serial_fault = 0;
    serial_open = 0;
    squid_allocator_next = 0U;
    memset(retro_workspace, 0, sizeof(retro_workspace));

    rtc_mask_interrupts();
    if (serial_initialize(profile, baud_rate, parity, stop_bits) != 0) {
        rtc_restore_interrupts();
        return -1;
    }

    /* xcc requires function-address members to be assigned at run time. */
    squid_platform.send_char = serial_send_character;
    squid_platform.recv_char = serial_receive_character;
    squid_platform.get_tick = serial_tick;
    squid_platform.mem_alloc = squid_allocate;
    squid_platform.mem_free = squid_deallocate;
    /* One protocol tick is 20 ms. Two seconds covers a maximum frame even at
       2400 baud, plus its ACK, CTS propagation, and scheduling jitter. */
    squid_timing.timeout_ticks = 100U;
    /* ACK immediately.  On an emulated Partner the guest may run slower than
       wall time, while squid-server's retransmission timer is real-time. */
    squid_timing.ack_delay_ticks = 0U;
    squid_timing.ping_ticks = 0U;
    squid_timing.max_retries = 3U;
    squid_timing.payload_bytes = (uint8_t)payload_bytes;
    paket_timebase_reset(&protocol_clock, paket_partner_timer_ms());
    snet_init(&squid_platform, &squid_timing);

    link->socket_fd = squid_open(
        SQUID_CLIENT_WORKSPACE_SIZE,
        SQUID_CLIENT_WORKSPACE_SIZE
    );
    if ((link->socket_fd < 0) ||
        (squid_connect(link->socket_fd, paket_squid_channel) != 0)) {
        paket_link_close(link);
        return -1;
    }

    started = paket_partner_timer_ms();
    while (!snet_link_is_up()) {
        pump_link();
        if (serial_fault ||
            (paket_elapsed_ms(started, paket_partner_timer_ms()) >=
             paket_connect_timeout_ms)) {
            paket_link_close(link);
            return -1;
        }
    }
    link->link_epoch = snet_link_epoch();
    link->opened = 1;
    squid_client_init(
        &retro_client,
        link->socket_fd,
        retro_workspace,
        SQUID_CLIENT_PACKET_MAX,
        client_idle,
        NULL
    );
    return 0;
}

void paket_link_close(paket_link *link)
{
    /* Let a delayed standalone ACK reach the peer before the UART and its
       RTS gate are closed.  Without this, a successful final response looks
       like a link failure to the server. */
    if (serial_open && snet_link_is_up() && !serial_fault) {
        uint16_t started = paket_partner_timer_ms();

        while (paket_elapsed_ms(started, paket_partner_timer_ms()) < 100U) {
            pump_link();
            if (serial_fault) {
                break;
            }
        }
    }
    if ((link != NULL) && (link->socket_fd >= 0)) {
        squid_close(link->socket_fd);
        link->socket_fd = -1;
    }
    if (serial_open) {
        uint8_t interrupts_enabled = paket_serial_interrupt_lock();

        /* Disable every selected-channel interrupt before restoring the IM2
           words.  Reset-TX-pending also releases an in-service empty source. */
        paket_port_out(paket_serial_control_port, 1U);
        paket_port_out(
            paket_serial_control_port,
            serial_selected_channel_b
                ? partner_sio_wr1_status_vector : 0U
        );
        paket_port_out(paket_serial_control_port, 0x28U);
        serial_write_wr5(partner_sio_wr5_base);
        paket_serial_restore_vectors();
        serial_open = 0;
        paket_serial_interrupt_unlock(interrupts_enabled);
    }
    if (link != NULL) {
        link->opened = 0;
    }
    rtc_restore_interrupts();
}

squid_client_t *paket_link_client(paket_link *link)
{
    if ((link == NULL) || !link->opened || serial_fault ||
        !snet_link_is_up() || (snet_link_epoch() != link->link_epoch)) {
        return NULL;
    }
    return &retro_client;
}
