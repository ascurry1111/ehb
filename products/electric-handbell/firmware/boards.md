# v0.3 concurrency demo — board registry

Nine Seeed XIAO ESP32C3 boards, each running `xiao_c3_node/xiao_c3_node.ino`
(docs/hardware-design.md §7 measurement 4). Referred to as ESP32C3-01 through
ESP32C3-09. IP scheme: `192.168.35.2XX`, where `XX` is the board's two-digit
ID — set as a DHCP reservation per board, keyed off its MAC address, so the
address never depends on live network discovery (which doesn't work across
subnets — see firmware/README.md).

The mDNS/OTA hostname (`ehb-c3-xxxxxx`) is auto-derived from each board's own
MAC at boot — see `xiao_c3_node.ino` — so it's recorded here for reference
only, not something to configure.

| ID | Reserved IP     | MAC               | OTA hostname       | Status |
|----|------------------|-------------------|---------------------|--------|
| 01 | 192.168.35.201   | ac:27:6e:7e:f8:fc | ehb-c3-7ef8fc       | Bring-up + OTA validated |
| 02 | 192.168.35.202   | —                 | —                   | Not yet brought up |
| 03 | 192.168.35.203   | —                 | —                   | Not yet brought up |
| 04 | 192.168.35.204   | —                 | —                   | Not yet brought up |
| 05 | 192.168.35.205   | —                 | —                   | Not yet brought up |
| 06 | 192.168.35.206   | —                 | —                   | Not yet brought up |
| 07 | 192.168.35.207   | —                 | —                   | Not yet brought up |
| 08 | 192.168.35.208   | —                 | —                   | Not yet brought up |
| 09 | 192.168.35.209   | —                 | —                   | Not yet brought up |

**To bring up a new board:** flash it over USB once (see firmware/README.md),
note the MAC and hostname it prints over serial on first boot, set a DHCP
reservation for that MAC at its intended `192.168.35.2XX` address, and fill
in its row above.

**Solder the headers -- don't press-fit them.** Board 01 booted and held WiFi
fine on press-fit headers, but its D10 signal line was intermittent (confirmed
with a multimeter: direct pressure on the board changed the reading). Same
failure mode `docs/hardware-design.md` already documents for the Feather/IMU
boards -- power/ground tolerate a loose fit, signal pins don't. A breadboard
works as a soldering jig: push the header pins into the breadboard first, set
the XIAO on top so the pins poke through its holes, solder from the top side.
