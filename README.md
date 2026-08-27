# PAKET

`PAKET.COM` is a Retro Vault command-line package manager for the Iskra
Delta Partner. It connects through a selected Partner serial port, carries the
Retro Vault protocol over Squid wire protocol 2 on channel 3, and streams
downloads directly to a CP/M file.

## Commands

```text
PAKET                         list Partner packages
PAKET LUN*                    search by name or package ID
PAKET LUNATIK                 show package information
PAKET LUNATIK A:/1/           download to drive A, user area 1
PAKET LUNATIK A:/1/LUNA.COM   download with an explicit 8.3 name
PAKET -p 2 LUN*               search through SIO1 port B
PAKET -p 4 LUNATIK            use SIO2 port B
PAKET -c 2400-N-1 LUNATIK     override the default 9600-N-1 settings
PAKET -p 3 -c 4800-E-2 LUN*   combine port and line settings
PAKET -m 64 LUNATIK           offer a 64-byte Squid frame payload
```

Wildcards are case-insensitive. `*` matches any sequence and `?` matches one
character. An exact package name or ID selects the information command.

Paths use forward slashes. `A:/` and `A:/0/` both mean drive A, user area 0;
`A:/1/` through `A:/15/` select the other CP/M user areas. A directory target
gets an automatically derived 8.3 filename. A package containing multiple
files is stored as a ZIP. If the server offers several download choices,
PAKET currently uses the first one.

## Build

Requirements are GNU Make and Docker. The build uses the latest
`wischner/xcc-z80-idp` compiler image and compiles against the local
`../../retro-plastics/libsquid` and `../../retro-plastics/squid-server`
worktrees. PAKET owns its small Partner SIO and RTC hardware layer and does not
link against `idp-sdk`. Set `LIBSQUID_DIR` and `SQUID_SERVER_DIR` when the
dependencies are not in those sibling locations.

```sh
make
```

This runs the hosted sanitizer suite before cross-compiling. Distribution
artifacts are:

- `bin/paket.com` — CP/M executable
- `bin/fddb.img` — Partner floppy image with `0:PAKET.COM`

Use `make test` for hosted tests, `make cpm` for only the cross-build, and
`make clean` to remove generated PAKET output. All intermediate and temporary
files stay below `tests/dump/`; the repository root is never used as a build
or test scratch directory.

## Serial and server setup

The optional `-p` parameter selects the serial channel:

- `-p 2` — SIO1 port B (default)
- `-p 3` — SIO2 port A / VAX
- `-p 4` — SIO2 port B

The optional `-c` parameter has the form `baud-parity-stop`, for example
`-c 2400-N-1`. Supported baud rates are 2400, 4800, and 9600; parity is `N`,
`E`, or `O`; stop bits are 1 or 2. Data length is always 8 bits and flow
control is RTS/CTS. SIO1 port B defaults to 9600-N-1. Without `-c`, PAKET
uses the selected port's default
communication profile. All options can appear before or after package
arguments. The server must use matching line settings and map the Retro Vault
plugin to channel 3:

```text
plugin 3 /opt/squid/lib/plugins/libretrovault.so
```

`-m` configures PAKET's negotiated Squid DATA payload offer from 16 through
112 bytes (default 112). Each endpoint may choose its own maximum; the link
uses the lower value. Use `-m 16` when talking to an older wire-v2 endpoint.

PAKET's terminal messages are Slovenian and use CP/M-safe ASCII. All
user-visible strings live in `src/messages.c`, which is the single place to
edit when adding or translating a language catalog.

Console output follows the plain style of `apt`: it does not clear the screen,
change text attributes, or draw rules. Search and catalog results use a compact
aligned ID/name table and correct Slovenian package counts. The information
view presents vendor, version, platform, release year, rating, a word-wrapped
description, and each available download's format, exact byte size, and file
count. During a download one fixed-width status line counts the remaining
bytes down to zero.

The same `PAKET.COM` runs on both Partner display configurations. Output uses
ordinary printable text plus carriage return for the progress line through the
standard CP/M console; no ANSI or VT52 escape sequences are emitted. PAKET
never accesses GDP display memory or the AVDC console routines directly.

See [the emulator test guide](docs/howtos/EMULATOR_TEST.md) for TCP bridge
setup and [the compatibility notes](docs/research/COMPATIBILITY.md)
for the exact upstream revisions tested.

## Current upstream compatibility

Catalog listing, wildcard search, exact resolution, package information, and
streaming downloads are tested against squid-server. PAKET uses the supplied
optimized Z80 Squid and Retro Vault assembly clients. A one-byte packet length
preserves plugin message boundaries while Squid fragments the stream into
variable DATA frames up to the negotiated 16-to-112-byte payload, protected by
CRC-8/SMBUS.

The CP/M build has no general-purpose heap. It reserves exactly the two Squid
workspaces required by its one socket and uses a 128-byte TX ring plus a
64-byte RX ring. Wire v2 remains stop-and-wait, but negotiation raises the DATA
payload from the compatible 16-byte default to at most 112 bytes. SIO
receive/transmit service runs from dynamically installed IM2 handlers. The RX
handler lowers RTS at a high-water mark and raises it after foreground code
drains the ring; Auto Enables makes CTS gate transmission. PAKET reads the
active `I` register and patches only the selected SIO's TX, RX, and special-RX
words in Partner's existing IM2 table, saving and restoring all three words.

The rings and compact handler template are copied to `C100h`–`C2A5h`, so
interrupts remain safe while CP/M 3 maps the banked BDOS. Before touching that
range, PAKET queries the CP/M System Control Block for the common-memory base,
reads the resident BDOS entry from page zero, and verifies that the complete
block lies in the available common TPA while leaving CP/M's first common page
untouched. RTC reads use a stable seconds/hundredths snapshot; RTC NMI is
masked from the earliest C startup hook, any status latched during a transfer
is acknowledged, and the saved enable state is restored when PAKET exits.
SIO teardown also retains channel B's shared status-vector bit, so the GDP
keyboard on channel A continues to receive its distinct IM2 vector afterward.
