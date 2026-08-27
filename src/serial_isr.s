        ;; Interrupt-driven Partner Z80 SIO transport for PAKET.
        ;;
        ;; Common RAM owns the software rings.  The SIO RX ISR fills
        ;; the input ring and lowers RTS at the high-water mark; the TX ISR
        ;; drains the output ring whenever CTS permits the transmitter to run.
        ;; IM2 vector words are installed dynamically in CP/M's active I page
        ;; and restored before PAKET returns to the CCP.
        ;;
        ;; GPL 3.0 License (see: LICENSE)
        ;; Copyright (C) 2026 Tomaz Stih

        .module paket_serial_isr
        .optsdcc -mz80 sdcccall(1)

        .globl  _paket_serial_interrupt_lock
        .globl  _paket_serial_interrupt_unlock
        .globl  _paket_serial_prepare_common
        .globl  _paket_serial_install_vectors
        .globl  _paket_serial_restore_vectors
        .globl  _paket_serial_rx_isr
        .globl  _paket_serial_tx_isr

        .globl  _paket_serial_data_port
        .globl  _paket_serial_control_port
        .globl  _paket_serial_wr5
        .globl  _paket_serial_tx_vector
        .globl  _paket_serial_rx_vector
        .globl  _paket_serial_special_vector
        .globl  _paket_serial_output
        .globl  _paket_serial_output_head
        .globl  _paket_serial_output_tail
        .globl  _paket_serial_output_count
        .globl  _paket_serial_input
        .globl  _paket_serial_input_head
        .globl  _paket_serial_input_tail
        .globl  _paket_serial_input_count
        .globl  _serial_fault
        .globl  _paket_rtc_early_control

        .equ SERIAL_TX_RING_SIZE,   128
        .equ SERIAL_TX_RING_MASK,   127
        .equ SERIAL_RX_RING_SIZE,    64
        .equ SERIAL_RX_RING_MASK,    63
        .equ SERIAL_RX_HIGH_WATER,   48
        .equ SIO_WR5_RTS,            0x02
        .equ SIO_RESET_TX_PENDING,   0x28

        ;; CP/M 3 switches the lower 48 KiB while executing banked BDOS and
        ;; BIOS code.  Every byte touched by an interrupt therefore lives in
        ;; the common 0xC000..0xFFFF window.  The COM loader must not fill the
        ;; gap up to that address because C000h contains a CP/M bank gateway,
        ;; so the compact handler template below is copied here at run time.
        .equ SERIAL_COMMON_BASE,                    0xc100
        .equ _paket_serial_data_port,              SERIAL_COMMON_BASE + 0
        .equ _paket_serial_control_port,           SERIAL_COMMON_BASE + 1
        .equ _paket_serial_wr5,                    SERIAL_COMMON_BASE + 2
        .equ _paket_serial_tx_vector,              SERIAL_COMMON_BASE + 3
        .equ _paket_serial_rx_vector,              SERIAL_COMMON_BASE + 4
        .equ _paket_serial_special_vector,         SERIAL_COMMON_BASE + 5
        .equ _paket_serial_output,                 SERIAL_COMMON_BASE + 6
        .equ _paket_serial_output_head,            SERIAL_COMMON_BASE + 134
        .equ _paket_serial_output_tail,            SERIAL_COMMON_BASE + 135
        .equ _paket_serial_output_count,           SERIAL_COMMON_BASE + 136
        .equ _paket_serial_input,                  SERIAL_COMMON_BASE + 137
        .equ _paket_serial_input_head,             SERIAL_COMMON_BASE + 201
        .equ _paket_serial_input_tail,             SERIAL_COMMON_BASE + 202
        .equ _paket_serial_input_count,            SERIAL_COMMON_BASE + 203
        .equ paket_serial_saved_tx_vector,         SERIAL_COMMON_BASE + 204
        .equ paket_serial_saved_rx_vector,         SERIAL_COMMON_BASE + 206
        .equ paket_serial_saved_special_vector,    SERIAL_COMMON_BASE + 208
        .equ paket_serial_vectors_installed,       SERIAL_COMMON_BASE + 210
        .equ paket_serial_restore_interrupts,      SERIAL_COMMON_BASE + 211
        .equ _serial_fault,                        SERIAL_COMMON_BASE + 212
        .equ SERIAL_COMMON_CODE,                   SERIAL_COMMON_BASE + 214

        .area _CODE

;; Return 1 in A when maskable interrupts were enabled, then disable them.
_paket_serial_interrupt_lock::
        ld      a,i
        di
        ld      a,#0
        ret     po
        inc     a
        ret

;; Restore an interrupt state returned by paket_serial_interrupt_lock().
;; The uint8_t argument arrives in A.
_paket_serial_interrupt_unlock::
        or      a,a
        ret     z
        ei
        ret

;; A=vector, H=active I page, DE=new handler, IX=saved word.
paket_serial_patch_vector:
        ld      l,a
        ld      a,(hl)
        ld      0(ix),a
        ld      (hl),e
        inc     hl
        ld      a,(hl)
        ld      1(ix),a
        ld      (hl),d
        ret

;; A=vector, H=active I page, IX=saved word.
paket_serial_unpatch_vector:
        ld      l,a
        ld      a,0(ix)
        ld      (hl),a
        inc     hl
        ld      a,1(ix)
        ld      (hl),a
        ret

_paket_serial_install_vectors::
        ;; IX is callee-preserved by sdcccall(1).
        push    ix
        call    _paket_serial_interrupt_lock
        ld      (paket_serial_restore_interrupts),a
        ld      a,i
        ld      h,a

        ld      a,(_paket_serial_tx_vector)
        ld      de,#_paket_serial_tx_isr
        ld      ix,#paket_serial_saved_tx_vector
        call    paket_serial_patch_vector

        ld      a,(_paket_serial_rx_vector)
        ld      de,#_paket_serial_rx_isr
        ld      ix,#paket_serial_saved_rx_vector
        call    paket_serial_patch_vector

        ld      a,(_paket_serial_special_vector)
        ld      de,#_paket_serial_rx_isr
        ld      ix,#paket_serial_saved_special_vector
        call    paket_serial_patch_vector

        ld      a,#1
        ld      (paket_serial_vectors_installed),a
        ld      a,(paket_serial_restore_interrupts)
        pop     ix
        jp      _paket_serial_interrupt_unlock

_paket_serial_restore_vectors::
        ld      a,(paket_serial_vectors_installed)
        or      a,a
        ret     z
        ;; IX is callee-preserved by sdcccall(1).
        push    ix
        call    _paket_serial_interrupt_lock
        ld      (paket_serial_restore_interrupts),a
        ld      a,i
        ld      h,a

        ld      a,(_paket_serial_tx_vector)
        ld      ix,#paket_serial_saved_tx_vector
        call    paket_serial_unpatch_vector

        ld      a,(_paket_serial_rx_vector)
        ld      ix,#paket_serial_saved_rx_vector
        call    paket_serial_unpatch_vector

        ld      a,(_paket_serial_special_vector)
        ld      ix,#paket_serial_saved_special_vector
        call    paket_serial_unpatch_vector

        xor     a
        ld      (paket_serial_vectors_installed),a
        ld      a,(paket_serial_restore_interrupts)
        pop     ix
        jp      _paket_serial_interrupt_unlock

;; Save every register set the xcc-generated foreground may be using.
paket_serial_common_template:
paket_serial_rx_template:
        push    af
        push    bc
        push    de
        push    hl
        ex      af,af'
        push    af
        exx
        push    bc
        push    de
        push    hl
        push    ix
        push    iy

        ;; Reading the data register acknowledges the SIO receive source.
        ld      a,(_paket_serial_data_port)
        ld      c,a
        in      a,(c)
        ld      e,a
        ld      a,(_paket_serial_input_count)
        cp      #SERIAL_RX_RING_SIZE
        jr      nc,paket_serial_rx_full

        ld      a,(_paket_serial_input_head)
        ld      l,a
        ld      h,#0
        ld      bc,#_paket_serial_input
        add     hl,bc
        ld      (hl),e
        ld      a,(_paket_serial_input_head)
        inc     a
        and     #SERIAL_RX_RING_MASK
        ld      (_paket_serial_input_head),a
        ld      a,(_paket_serial_input_count)
        inc     a
        ld      (_paket_serial_input_count),a
        cp      #SERIAL_RX_HIGH_WATER
        jr      c,paket_serial_rx_done

        ;; Lower RTS once, leaving sixteen ring slots for bytes already in
        ;; the peer UART/driver when the flow-control transition propagates.
        ld      a,(_paket_serial_wr5)
        bit     1,a
        jr      z,paket_serial_rx_done
        and     #0xfd
        ld      (_paket_serial_wr5),a
        ld      e,a
        ld      a,(_paket_serial_control_port)
        ld      c,a
        ld      a,#5
        out     (c),a
        ld      a,e
        out     (c),a
        jr      paket_serial_rx_done

paket_serial_rx_full:
        ;; Consume the hardware byte to release the SIO, but make the link
        ;; fail visibly instead of silently corrupting a packet.
        ld      hl,#_serial_fault
        ld      (hl),#1
        inc     hl
        ld      (hl),#0
        ld      a,(_paket_serial_wr5)
        and     #0xfd
        ld      (_paket_serial_wr5),a
        ld      e,a
        ld      a,(_paket_serial_control_port)
        ld      c,a
        ld      a,#5
        out     (c),a
        ld      a,e
        out     (c),a

paket_serial_rx_done:
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        exx
        pop     af
        ex      af,af'
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti

paket_serial_tx_template:
        push    af
        push    bc
        push    de
        push    hl
        ex      af,af'
        push    af
        exx
        push    bc
        push    de
        push    hl
        push    ix
        push    iy

        ld      a,(_paket_serial_output_count)
        or      a,a
        jr      z,paket_serial_tx_empty
        ld      a,(_paket_serial_output_tail)
        ld      l,a
        ld      h,#0
        ld      bc,#_paket_serial_output
        add     hl,bc
        ld      e,(hl)
        ld      a,(_paket_serial_output_tail)
        inc     a
        and     #SERIAL_TX_RING_MASK
        ld      (_paket_serial_output_tail),a
        ld      hl,#_paket_serial_output_count
        dec     (hl)
        ld      a,(_paket_serial_data_port)
        ld      c,a
        ld      a,e
        out     (c),a
        jr      paket_serial_tx_done

paket_serial_tx_empty:
        ;; Suppress repeated TX-empty interrupts.  The next foreground write
        ;; to the data register rearms the source in the Z80 SIO.
        ld      a,(_paket_serial_control_port)
        ld      c,a
        ld      a,#SIO_RESET_TX_PENDING
        out     (c),a

paket_serial_tx_done:
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        exx
        pop     af
        ex      af,af'
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti

paket_serial_common_template_end:

        ;; The exported ISR symbols are the run-time common addresses, not
        ;; the banked template addresses stored in PAKET.COM.
        .equ _paket_serial_rx_isr, SERIAL_COMMON_CODE
        .equ _paket_serial_tx_isr, SERIAL_COMMON_CODE + (paket_serial_tx_template - paket_serial_common_template)

_paket_serial_prepare_common::
        ld      hl,#paket_serial_common_template
        ld      de,#SERIAL_COMMON_CODE
        ld      bc,#paket_serial_common_template_end-paket_serial_common_template
        ldir
        ret

        ;; crt0 calls through the complete _GSINIT area before it queries BDOS
        ;; or constructs argc/argv.  Capture and mask the Partner RTC here so
        ;; a minute edge can never enter CP/M's non-handler at 0066h during C
        ;; startup.  Deliberately do not RET: this is an initializer fragment
        ;; which falls through to the next linked fragment / _GSFINAL return.
        .area   _GSINIT
        ld      c,#0xb1
        in      a,(c)
        ld      (_paket_rtc_early_control),a
        xor     a
        out     (c),a
        ld      c,#0xb0
        in      a,(c)
