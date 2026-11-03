# timeadactl

A small, one-shot NTP client for Linux. It queries an NTP server over UDP,
checks the reply, estimates UTC, and sets the system `CLOCK_REALTIME` clock.
It has no third-party library dependencies.

## Build

```sh
make
make check
```

## Use

## Help

Usage:
```
./timeadactl --help
Usage: ./timeadactl [-n] [-s server] [-p port]
  -n          query and display time, but do not set the clock
  -s server   NTP server (default: pool.ntp.org)
  -p port     UDP port (default: 123)
  -h          show this help
(also accepts long options, "--dry-run, --server, --port, --help")
```

Preview the calculated time without changing the clock: 

```sh
./timeadactl --dry-run
NTP server: pool.ntp.org (NTPv4, stratum 2)
Offset:    +31.790850 seconds
Delay:      0.077399 seconds
UTC time:   2026-11-03T23:40:25.595728688Z
Dry run: system clock was not changed.
```

Set the system clock using the default server (`pool.ntp.org`):

```sh
./timeadactl
NTP server: pool.ntp.org (NTPv4, stratum 2)
Offset:    +31.791382 seconds
Delay:      0.078044 seconds
UTC time:   2026-11-03T23:40:41.960139839Z
timeadactl: clock_settime: Operation not permitted (requires root or CAP_SYS_TIME)
```
```
./sudo timeadactl
...
System clock updated.

```

Choose another server or UDP port:

```sh
sudo ./timeadactl --server time.google.com
sudo ./timeadactl --server 127.0.0.1 --port 123
```

Options:
`-n` / `--dry-run` displays the result without setting the clock. `-s HOST` /
`--server HOST` selects the server. `-p PORT` / `--port PORT` sets the UDP port
(default `123`). `-h` / `--help` displays usage.

Setting the clock requires root or `CAP_SYS_TIME`, and outbound UDP must be
allowed to the server's NTP port. The program steps the clock immediately; it
does not slew time, update the hardware RTC, run as a daemon, or replace a time
synchronizer.

## Notes

The client sends an NTPv4 request and accepts NTPv3/v4 server replies. It checks
the reply mode and version, leap indicator, stratum, echoed originate timestamp,
and nonzero server timestamps. Its UDP socket is connected to the selected
server address, filtering datagrams from other endpoints.

The clock offset is calculated from the four NTP timestamps. The 32-bit NTP
seconds field is unfolded to the era nearest the local clock; if the local date
is wrong by roughly 68 years or more, the era can be ambiguous.

NTP without an authentication extension is not cryptographically authenticated.
These checks reject malformed or unrelated replies, but do not protect against
an on-path attacker or an attacker able to spoof the server's network endpoint.
Use a trusted NTP source on a network you trust.
