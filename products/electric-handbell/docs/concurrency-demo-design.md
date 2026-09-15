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
closed as a design risk — see `../firmware/power-test.md` §5. Confirmed on
the real USB power bank: all nine boards up, on WiFi and advertising, stable
over 30 minutes with seven connected and blinking.

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

1. **Events are timestamped relative to T0**, not in absolute board-clock
   terms. A board reports "ring at T0 + 4523ms", not "ring at my-clock
   1699283". Since the phone chose T0 (§6), it can interpret every board's
   events without knowing anything about that board's clock.
2. **No per-board clock sync is needed at all.** An earlier draft of this
   design had the phone read each board's `Time` characteristic
   (`6e400004-...`, defined in v0.2 for exactly this) during upload and
   average round trips to derive a clock offset. Deriving T0 from the start
   beacon instead (§6) removes that step entirely — and with it a whole class
   of staleness bug, since offsets measured at upload time go stale as clocks
   drift apart, which the replay workflow makes much worse.
3. **The phone renders through a playout buffer.** On receipt: schedule the
   note for `T0 + event_offset + buffer` (~100ms). Anything arriving within
   the buffer window lands at exactly the right instant, regardless of how
   long it took to get there.

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

**Decision: "Upload song" and "Start song" are two distinct operations in the
app.** Upload distributes programs (Phase 1) and is a bounded task with a
visible start and end. Start synchronises and begins playback, and takes only
a few seconds.

The earlier alternative — baking T0 into the program push and having each
board wait for its own clock to reach it — was rejected. It required picking
an arbitrary, conservative wait up front (the push budget for nine boards is
20–40 seconds, so T0 would need a margin beyond that), which makes the user
sit through a pause with no indication of why. Worse, it offers no way to
**replay a song without re-uploading it**, which separating the two
operations gives for free.

### Why the start beacon must carry a countdown, not a timestamp

The phone broadcasts a start beacon repeatedly for ~2 seconds. Each repeat
carries **the time remaining until T0**, decremented as it goes: repeat #47
says "T0 in 2000ms", #48 says "T0 in 1900ms".

This matters. If beacons simply said "start 2000ms from when you hear this",
a board catching #47 and a board catching #48 would land 100ms apart. With a
countdown, both compute the same absolute instant, so **catching any single
beacon is sufficient and catching a later one is no worse than catching an
early one.** That is the same insight as §5 — an absolute instant tolerates
delivery jitter in a way an imperative "go now" never can.

Boards that catch several beacons (most will, at ~20 repeats over 2s) can
refine their estimate rather than trusting one sample. Taking the minimum
implied delay across samples converges on the truth, since transport delay is
always positive.

### The beacon is also what synchronises the boards to each other

A broadcast is a *shared* reference event: every board in the room hears the
same beacon at effectively the same instant. That makes it an excellent
**relative** sync mechanism — which is precisely what musical timing needs —
even though it is a mediocre absolute one.

This is why upload-time clock sync was dropped (§5). Clock offsets measured
during upload go stale: at ±40ppm, a 45-minute gap between upload and a
replay is ~108ms of drift, well outside the 30ms budget, and boards drift
apart from one another too. Re-deriving everything from a fresh beacon at
start time makes the age of the upload irrelevant.

### Readiness handshake

The few seconds "Start song" takes should be doing real work, not counting
down arbitrarily:

1. App broadcasts countdown beacons for ~2s, carrying a **run ID** and T0.
2. Each board that hears one broadcasts a short "ready, run N" advertisement.
3. App collects readiness from all nine and shows which have armed.
4. At T0, everyone starts.

This turns the pause into a genuine pre-flight check, and makes the one real
weakness of this approach — a board that hears no beacon simply never plays —
*visible before the piece starts* rather than discovered halfway through.
If a board hasn't armed, the app can warn or abort and retry before T0
arrives.

### Run ID

Every beacon and every event advertisement carries a run ID. Without it,
replaying a song is ambiguous: the phone cannot tell a ring from run 2 from a
late-arriving straggler from run 1, and a board already mid-song cannot tell
a stale beacon from a new one. The run ID also gives the app a free
diagnostic — a board that never emits events for the current run didn't
start.

### Board state across runs

For replay to work, a board must **retain its program after playing it
through** and return to a "loaded, idle" state ready to accept a new T0.
Upload is only needed again when the song itself changes.

---

## 7. Advertisement payload

Fits comfortably in a legacy advertisement (~26 usable bytes of
manufacturer-specific data), so extended advertising is not needed:

**Event advertisement** (ring / damp):

| Field | Bytes | Note |
|---|---|---|
| Board ID | 1 | 1–9 here, with room to grow |
| Run ID | 1 | disambiguates replays (§6) |
| Sequence | 1 | wraps; used with board and run ID for dedup |
| Event type | 1 | ring / damp |
| Pitch | 1 | a MIDI note number covers the full range |
| Offset from T0 | 3 | ms; 3 bytes covers ~4.6 hours |

About 8 bytes, leaving room for a dynamic level later — which is what the
real product will need.

**Start beacon** (phone to boards): run ID, and milliseconds remaining until
T0. **Ready advertisement** (board to phone): board ID and run ID.

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
| Transport jitter (events) | **0** | absorbed by the playout buffer |
| Start beacon spread | ±5–10ms, unmeasured | boards landing on slightly different T0 because they caught different beacons and the phone can't control emission timing precisely. **The dominant term, and the least understood** — see §10 |
| Crystal drift | ~0.2ms over a 2-min piece | only accrues since T0, not since upload, now that timestamps are T0-relative |
| Audio scheduling | 0, or 10–30ms+ | 0 if scheduled to a mixer timeline; large and variable if `play()` is called per event |

Note what moved. Drift used to be the term that scaled with duration; making
timestamps relative to T0 collapsed it to near-nothing, because it only
accumulates over the length of one piece rather than since upload. The
budget is now dominated by how tightly the start beacon lands, which is the
thing to measure first.

---

## 10. Open questions

- **How precisely can an Android app control advertising emission timing?**
  This is the biggest open risk. The countdown beacon (§6) assumes the app
  can stamp "time remaining" reasonably close to when the packet actually
  goes out, but Android's advertiser doesn't expose emission timing and
  updating advertising payload has its own latency. If the spread turns out
  too wide to hit the budget, the fallback is to designate one of the nine
  boards as a conductor — an ESP32 has far tighter control over its own
  advertising than an Android app does — and have the phone simply tell it
  when to run the countdown. That stays within the existing hardware, unlike
  a receiver dongle.
- Measured start-beacon spread across nine boards — currently estimated at
  ±5–10ms, which is now the dominant error term (§9)
- Program format and size — not yet designed
- Soundfont / synthesis approach, and how it couples to audio scheduling (§8)
- Observed advertisement loss rate with nine boards broadcasting, and whether
  3–5 repeats is the right redundancy
- How a board behaves if it hears a beacon for a run it is already playing,
  or a program upload mid-run
