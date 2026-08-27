        ;; PAKET-owned arbitrary Z80 port access.
        ;;
        ;; GPL 3.0 License (see: LICENSE)
        ;; Copyright (C) 2026 Tomaz Stih

        .module z80_io

        .globl  _paket_port_in
        .globl  _paket_port_out

        .area   _CODE

        ;; uint8_t paket_port_in(uint8_t port) __sdcccall(1)
_paket_port_in::
        ld      c,a
        in      a,(c)
        ret

        ;; void paket_port_out(uint8_t port, uint8_t value) __sdcccall(1)
_paket_port_out::
        ld      c,a
        ld      a,l
        out     (c),a
        ret
