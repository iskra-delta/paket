# Test PAKET with idp-emu

The automated regression boots both original Partner P and Partner G CP/M
systems and talks to idp-emu's internal Squid endpoint. An external
squid-server bridge remains useful for manual serial testing.

## Automated two-model regression

Build PAKET, copy each system hard disk below `idp-emu/tests/dump/`, update
`PAKET.COM` in those disposable copies, and run idp-emu's
`test_partner_paket_flow`. The test supplies a local HTTP fixture, searches a
model-specific package, downloads it to CP/M user area 1, and checks both HTTP
requests. It never uses a root-level testing or temporary directory.

## Build and mount the floppy

Build PAKET first:

```sh
make
```

Launch idp-emu with the generated image as physical floppy 0. Original GDP
CP/M exposes that floppy as drive `B:`. The command-line bridge makes the
serial route reproducible without changing the GUI routing panel:

```sh
../idp-emu/bin/idp-emu \
  --model gdp \
  --rom ../idp-emu/roms/partner_gdp.rom \
  --fd0 bin/fddb.img \
  --hdd ../idp-emu/disks/hdd-partner-g-system.img \
  --nvram ../idp-emu/tests/dump/emulator-partner-cmos.bin \
  --sio-tcp 2 6601 6602
```

The equivalent GUI setup is **Devices → Device Routing**, **SIO1 Port B**:

- Attach: `TCP Bridge`
- Data TCP Port: `6601`
- Control TCP Port: `6602`
- CTS follows data connection: enabled

SIO1 Port B is xcc's `SIO_PORT_LPT` and PAKET's default port 2. PAKET asserts
RTS and DTR, so the bridge's RTS requirement can remain enabled. Port 3 maps
to SIO2 Port A (`SIO_PORT_VAX`) and port 4 maps to SIO2 Port B.

## Bridge TCP to a pseudo-TTY

squid-server accepts a serial device rather than a TCP endpoint. One way to
join it to the emulator is a raw pseudo-TTY created by `socat`:

```sh
socat -d -d \
  pty,raw,echo=0,link=/tmp/paket-vax,wait-slave \
  tcp:127.0.0.1:6601,forever,interval=1
```

Use this squid-server configuration, replacing plugin paths if necessary:

```text
serial_device /tmp/paket-vax
serial_baud 9600
serial_databits 8
serial_parity none
serial_stopbits 1
serial_flow none
system_plugin /opt/squid/lib/plugins/libsquidsys.so
plugin 3 /opt/squid/lib/plugins/libretrovault.so
```

Start squid-server in the foreground so link and plugin errors remain visible:

```sh
squid-server --console --config /path/to/paket-serial.conf
```

The bridge may be started before the emulator because the TCP side retries.
Start squid-server before invoking PAKET so its HELLO frames are already
available when the guest opens SIO1B.

## Run commands

At the CP/M prompt:

```text
A>B:
B>PAKET LUN*
B>PAKET -p 3 LUNATIK
B>PAKET -p 4 -c 2400-N-1 LUNATIK A:/1/
```

Port 2 is the default, so `PAKET LUN*` and `PAKET -p 2 LUN*` are equivalent.
Omit `-c` to use the selected port's default profile. SIO1 port B defaults to
9600-N-1. When `-c` is present, configure
squid-server to the same baud, parity, and stop-bit values.

Use the device-routing window to verify that SIO1 Port B says
`TCP 6601 / ctl 6602`, has CTS and DCD asserted, and shows increasing RX/TX
byte counters. See `docs/research/COMPATIBILITY.md` for the exact integrated
revisions and end-to-end verification results.
