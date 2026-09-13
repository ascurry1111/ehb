# Concurrency Demo — Design

**Status:** design in progress, nothing built yet beyond the node firmware
and the power rig. Decisions get recorded here as they're made; anything not
written down is still open.

**Scope:** the nine-board concurrency demo only. The bell hardware itself,
ring detection, and the eventual production transport are out of scope — see
`hardware-design.md` for those. Where this demo deliberately diverges from
what the real product will do, that's called out rather than glossed.

---

## 1. What this demo is, and what it isn't

Nine XIAO ESP32C3 boards each act as an independent "bell." Each receives a
different program — a sequence of wait/ring/wait/damp steps with a pitch per
ring — and all nine run their programs simultaneously so that together they
play a piece, the way a handbell ensemble does. An Android app receives the
ring and damp events from each board and renders the actual audio.

**What it demonstrates:** that N independent transmitters can run
concurrently, and that a single receiver can take events from all of them
and turn them into coherent music.

**What it deliberately does not demonstrate.** In the real product a ring is
triggered by a motion sensor at an unpredictable instant, and the end-to-end
latency from gesture to sound is the thing that matters. Here every event is
scheduled in advance and rendered through a playout buffer (§5), which
trades latency away to buy timing precision. That is a legitimate compromise
for a demo about *concurrency* — but it means this rig does not exercise the
latency path.

**The real thing still needs testing**, and this same rig can do it later
with fewer than seven boards, where direct GATT connections still fit inside
Android's limit (§3).

---

## 2. Hardware as built

- 9x Seeed XIAO ESP32C3 on two breadboards, soldered headers
- One LED plus current-limiting resistor per board on D10 (active-HIGH)
- USB-C breakout feeding the middle of one power rail, slide switch on the feed
- Bulk electrolytics distributed across both breadboards, 0.1uF ceramic local
  to each board
- 5V from a USB power bank into each board's 5V pin, onboard LDO per board
- Per-board identity, reserved IPs and OTA hostnames: see `../firmware/boards.md`

Measured draw for the full rig: ~0.51A average, 0.85A max at idle. Power is
closed as a design risk — see `../firmware/power-test.md` §5.

---

## 3. Constraints that drove this design

**Android caps concurrent GATT connections at 7** (`BTA_GATTC_CONN_MAX` in
Bluedroid, plus the phone's own controller limit on top). Confirmed
empirically: nRF Connect refused the eighth board every time, regardless of
which seven were already connected. This rules out the original plan of
holding nine simultaneous connections. Full writeup in
`../firmware/power-test.md` §6.

For reference, other platforms are not obviously better. iOS has no
documented cap and manages ~20 in practice on current hardware, but is
strongly device-dependent (some older iPhones reportedly ~6); watchOS and
visionOS cap at 2 per app; Windows is around 7 theoretical with 3–4 commonly
reported. **BLE direct-to-phone is a 2–4 bell technology.** Anything at
ensemble scale needs a receiver — which is what `hardware-design.md` §3.3 and
open decision #4 already anticipated from the airtime side. This is that same
conclusion arriving from the host-stack side.

**A fast BLE connection interval is expensive.** At 7.5–15ms the radio wakes
65–130 times a second, and that wake-up — not the data — is essentially the
entire power cost (`../firmware/power-test.md` §4). Dropping connections
during playback removes this cost completely.

---

## 4. Architecture: sequential push, connectionless playback

Three phases.

### Phase 1 — Program distribution (connected, one board at a time)

For each board in turn: connect, clock-sync, push its program, disconnect.
Never more than one connection open, so Android's limit is irrelevant and the
design scales well past nine.

### Phase 2 — Synchronized start

Every board begins its program at the same absolute instant, T0. See §6 —
this is the part with a real wrinkle in it.

### Phase 3 — Playback (connectionless)

Boards broadcast ring/damp events as BLE advertisements. The phone runs a
single continuous scan and never connects to anything. No connection limit,
no connection-interval power cost, and it scales to as many boards as fit in
the air.

---

## 5. The core principle: play on timestamp, not on receipt

**If the phone plays a note when the advertisement arrives, timing accuracy
is capped by transport jitter — and BLE advertising jitter is bad.** Every
advertising event carries a mandatory 0–10ms random delay (the spec requires
it, so devices don't collide persistently), on top of the advertising
interval, the three-channel sweep, and whenever the scanner happens to be
listening. That is easily 20–30ms, independently per board. Two bells ringing
at the same instant would land up to 30ms apart — right at the threshold
where desync becomes audible.

So arrival time must not be what determines playback time:

1. **Shared clock.** During each board's Phase 1 connection, read its `Time`
   characteristic (`6e400004-...`, already defined in v0.2 for exactly this
   purpose) several times and average the round trips to derive that board's
   clock offset.
2. **Events carry timestamps.** Each ring/damp advertisement includes the
   board's own clock value for when the event occurred, not merely the fact
   that it occurred.
3. **The phone renders through a playout buffer.** On receipt: convert to
   phone time, then schedule the note for `event_time + buffer` (~100ms).
   Anything arriving within the buffer window lands at exactly the right
   instant, regardless of how long it took to get there.

**This is affordable here precisely because latency is not a demo
requirement.** A playout buffer spends fixed latency to buy timing precision,
and fixed latency is the one thing this demo has already declared it doesn't
care about.

**Relative timing between bells then depends only on clock accuracy, not on
the transport.** Two bells scheduled for the same instant land in the same
audio buffer.

**And redundancy becomes free.** Because timing comes from the timestamp
rather than from arrival, each event can be broadcast 3–5 times over ~50ms
with a sequence number and deduped on the phone, at zero cost to timing
accuracy. That removes the main weakness of connectionless delivery: a
dropped advertisement no longer means a dropped note.

---

## 6. Starting together

The mechanism: **there is no "start" command. T0 travels with the program in
Phase 1**, and each board independently waits until its own clock reaches T0.
A board cannot fail to hear the start signal, because there is no start
signal to miss.

The wrinkle: **T0 has to still be in the future when the *last* board is
pushed.** Phase 1 is sequential, and a BLE connect plus service discovery
plus write plus disconnect is realistically 2–4 seconds per board — call it
20–40 seconds for nine. Two ways to handle that:

**Option A — budget the push time up front.** The phone picks
`T0 = now + (boards x expected_push_time) + margin` before Phase 1 begins and
pushes that same T0 to every board. If distribution finishes early, everyone
simply waits. If a push fails and the retries blow the budget, abort and
restart the sequence with a fresh T0.

- Simplest, and depends on no broadcast whatsoever.
- Cost: a fixed "preparing" pause of up to a minute before the piece starts,
  even when distribution went quickly.

**Option B — broadcast T0 once distribution completes.** Phase 1 carries only
the program; once all nine are loaded, the phone broadcasts "start at T0"
with T0 a couple of seconds out, repeated heavily.

- Better live experience: the wait is only as long as distribution actually
  took.
- **Late delivery is harmless**, which is the same insight as §5 — an
  absolute timestamp tolerates jitter in a way an imperative "go now" does
  not. A board needs to hear only *one* copy at *any* point before T0.
- Failure mode: a board that hears none of them never plays. With heavy
  repetition over a couple of seconds this is unlikely, but it is a non-zero
  risk that Option A does not have.

**Recommendation: start with Option A.** It is simpler and has no failure
mode, and an awkward pause is acceptable while we are still proving the
concept. Move to B if the pause turns out to spoil the demo.

**Do not combine them** by pushing a fallback T0 and letting a later
broadcast override it. A board that misses the override would play at the
wrong time, which is a worse and more confusing failure than a board that
does not play at all.

---

## 7. Advertisement payload

Fits comfortably in a legacy advertisement (~26 usable bytes of
manufacturer-specific data), so extended advertising is not needed:

| Field | Bytes | Note |
|---|---|---|
| Board ID | 1 | 1–9 here, with room to grow |
| Sequence | 1 | wraps; used with board ID for dedup |
| Event type | 1 | ring / damp |
| Pitch | 1 | a MIDI note number covers the full range |
| Timestamp | 4 | board clock, ms |

About 8 bytes, leaving room for a dynamic level later — which is what the
real product will need.

---

## 8. Android app requirements

**Scanning.** One continuous scan, `SCAN_MODE_LOW_LATENCY`,
`setReportDelay(0)`, `CALLBACK_TYPE_ALL_MATCHES`. Do not stop and restart
scanning repeatedly — Android throttles apps that start more than 5 scans in
30 seconds. Dedup by `(board_id, sequence)` in app code rather than relying
on the stack's duplicate filtering, whose behaviour varies by device.

**Audio scheduling — this is the one that can quietly undo everything
above.** Notes must be scheduled into a mixer timeline at explicit frame
positions (AudioTrack/Oboe), not played fire-and-forget with `play()` on
receipt. Fire-and-forget reintroduces the audio pipeline's own jitter and
throws away the precision the playout buffer just bought. Two notes scheduled
to the same frame are sample-accurate.

This couples to the soundfont / instrument-selection requirement: the
synthesis approach and the scheduling approach need to be decided together.

---

## 9. Error budget

Target: relative timing between any two bells well under 30ms, the point at
which desync becomes audible. Aiming for under 10ms.

| Source | Contribution | Notes |
|---|---|---|
| Transport jitter | **0** | absorbed by the playout buffer |
| Clock sync accuracy | ±2–5ms | improvable with round-trip averaging |
| Crystal drift | ±2–5ms per board over 2 min | ESP32 modules typically ±20–40ppm; two boards drifting opposite ways roughly doubles it |
| Audio scheduling | 0, or 10–30ms+ | 0 if scheduled to a mixer timeline; large and variable if `play()` is called per event |

Drift is the only term that scales with duration. Fine for a demo-length
piece; anything long would need periodic re-sync.

---

## 10. Open questions

- Program format and size — not yet designed
- Soundfont / synthesis approach, and how it couples to audio scheduling (§8)
- Whether Option A's pause is tolerable in practice, or B is needed (§6)
- Clock-sync accuracy over the sequential-push flow — currently estimated,
  not measured
- Observed advertisement loss rate with nine boards broadcasting, and whether
  3–5 repeats is the right redundancy
- Power bank not yet tested with the full rig (`../firmware/power-test.md` §5)
