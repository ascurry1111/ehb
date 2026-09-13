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
| 02 | 192.168.35.202   | ac:27:6e:7e:ad:54 | ehb-c3-7ead54       | Bring-up + OTA validated |
| 03 | 192.168.35.203   | ac:27:6e:7f:01:6c | ehb-c3-7f016c       | Bring-up + OTA validated |
| 04 | 192.168.35.204   | ac:27:6e:7c:b9:e8 | ehb-c3-7cb9e8       | Bring-up + OTA validated |
| 05 | 192.168.35.205   | ac:27:6e:7e:3e:4c | ehb-c3-7e3e4c       | Bring-up + OTA validated |
| 06 | 192.168.35.206   | ac:27:6e:7c:cb:1c | ehb-c3-7ccb1c       | Bring-up + OTA validated |
| 07 | 192.168.35.207   | ac:27:6e:7c:a4:64 | ehb-c3-7ca464       | Bring-up + OTA validated |
| 08 | 192.168.35.208   | ac:27:6e:7e:fe:f0 | ehb-c3-7efef0       | Bring-up + OTA validated |
| 09 | 192.168.35.209   | ac:27:6e:7e:de:b8 | ehb-c3-7edeb8       | Bring-up + OTA validated |

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
