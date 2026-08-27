# Integrated compatibility

PAKET was built and tested on 2026-08-26 against:

- libsquid wire protocol 2, commit
  `50039278d4f74f24891e6e78166f2cee96f3a316`
- squid-server commit
  `bac605387ad152e4e92cb03644388b4836cb4eaa`
- the idp-emu worktree based on commit
  `198d6d6d1039041bf7089650d50fe586f26df726`
- `wischner/xcc-z80-idp:latest`, xcc 2.3.2

The relevant fixes are currently in those local worktrees, on top of the
listed commits.

## Protocol and package size

The client implements Retro Vault protocol version 1 on Squid channel 3. It
checks server capabilities, walks paged LIST, SEARCH, and INFO responses,
validates lengths and cursors, and streams DOWNLOAD data directly into a CP/M
file. The package therefore does not have to fit in Partner memory.

PAKET directly assembles the optimized Z80 `libsquid` core and squid-server's
Z80 client, helper, and Retro Vault modules using xcc's `sdcccall(1)` ABI.
PAKET and squid-server put a one-byte request or response length in the Squid
byte stream. Requests and responses can cross any number of variable
negotiated 16-to-112-byte DATA frames without losing their boundary. Wire protocol 2 uses
CRC-8/SMBUS on every DATA and control frame and alternating ACK0/ACK1 tags.

## Serial and clock behavior

SIO1 port B is the default PAKET port and runs at 9600-N-1. The emulator uses
the Partner's 153600 Hz SIO clock and the programmed x1/x16/x32/x64 divisor,
data width, parity, and stop bits to pace each character against the 4 MHz
CPU. It models the three-character receive FIFO, the transmitter holding and
shift registers, RR0/RR1 ready state, delayed RTS release, CTS/DCD, and bytes
lost while a cable is disconnected.

PAKET installs receive, transmit, and special-receive handlers into CP/M's
active IM2 table and restores the original vectors on exit. TX uses a 128-byte
software ring and RX uses 64 bytes. RX lowers RTS at 48 bytes and raises it again at 16;
the SIO Auto Enables bit gates TX with CTS. Before a CP/M 3 BDOS call, PAKET
drains TX and lowers RTS so no banked handler can be invoked while the transient
bank is unavailable.

Interactive idp-emu sessions synchronize the MM58167A RTC, including its
hundredths and thousandths counters, to host wall time. PAKET derives its
20 ms libsquid clock from accumulated RTC deltas. The accumulation is
continuous across the RTC's 60-second millisecond window, so a minute rollover
does not become a false retransmission timeout. The connection deadline is 15
seconds, a Retro Vault response may take 50 seconds, and a libsquid DATA retry
uses a two-second timeout.

## Verification results

The hosted PAKET suite performs 184 checks under AddressSanitizer and UBSan.
The emulator regression boots original Partner P and Partner G ROM/CP/M media
from disposable hard-disk copies. Each model searches a deterministic local
Retro Vault catalog through emulated SIO1B and the internal Squid endpoint,
downloads a 4096-byte COM payload into CP/M user area 1, and requires both the
success message and HTTP download request. Separate live-catalog boot tests
also verify that both ROM and CP/M combinations still start PAKET.

The cross-build uses full warnings plus `-Werror`; the hosted build uses the
same warnings plus AddressSanitizer and UBSan. `PAKET.COM` is 31328 bytes for
this revision and has SHA-256
`0b8365ea10a6fd05eaaae07df7226619b2030cc081e434af73751997db8129bf`.
