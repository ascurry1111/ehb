# v0.3 concurrency demo — power consumption test

Goal: measure peak current draw for board ESP32C3-01 in each of five
activities, using the Nordic PPK2, to size a power supply for all nine
boards (docs/hardware-design.md §7, measurement 1 — scoped here strictly to
the concurrency demo, not the battery-powered bell). Firmware:
`xiao_c3_node/xiao_c3_node.ino`.

**Why this matters right now:** the Elegoo Power MB V2 is only rated for
700mA per rail. Nine boards' combined peak draw may or may not fit inside
that — these numbers are what answer that question.

## 1. PPK2 setup

**Power injection: 3.3V direct to the XIAO's 3V3 pin, PPK2 in Source Meter
mode, USB disconnected.** This bypasses the XIAO's onboard regulator, so
you're measuring the true minimum current the chip+radio need — the number
that matters for sizing a shared supply, independent of whichever
regulator ends up doing the 5V→3.3V conversion in the final setup (see the
reasoning in this project's chat history / commit log if you want the full
argument). USB is disconnected during every capture below — native USB on
this chip means USB power (VBUS) would otherwise add current PPK2 can't
see, and would conflict with PPK2 also trying to source power.

Wiring:

```
PPK2 VOUT (+) ──► XIAO 3V3 pin
PPK2 GND      ──► XIAO GND pin
```

In nRF Connect for Desktop's Power Profiler app:
1. Select the PPK2 device, set mode to **Source Meter**.
2. Set voltage to **3300 mV**.
3. Start the power output, then start sampling/capture.
4. The live graph shows current in real time; you can click-drag a time
   range afterward to read min/max/average over that window (exact UI
   wording may vary by app version — look for a statistics/selection
   panel).

Nothing else needs to be connected to the board — no USB, no breadboard
power rail — just the two PPK2 leads.

## 2. The five activities

The board boots straight into activity 1 (STATE_IDLE) on power-up, so just
apply power and start capturing. Activities 2–4 are driven manually from
**nRF Connect for Mobile** on your phone (BLE), watching the live PPK2
graph as you trigger each step. Activity 5 is driven from the PC (OTA push)
instead — BLE and WiFi are never both active except in activity 1, by
design (see the state-machine note at the top of `xiao_c3_node.ino`).

The board advertises over BLE as **`ehb-c3-7ef8fc`** (board 01's hostname)
— that's what to look for in a scan.

### 1. Idle (WiFi connected + OTA listening, BLE advertising, not connected)

Apply PPK2 power and let it boot. Give it ~15s to fully join WiFi and
settle (there will be a startup transient — WiFi association — that isn't
representative of steady-state idle; ignore that initial spike and read the
current *after* it settles). This is the default/resting state whenever
no phone is connected.

### 2. App-connected (BLE connected, WiFi off, waiting)

Open nRF Connect for Mobile, scan, connect to `ehb-c3-7ef8fc`. Watch the
graph right at the connection event, then let it settle a few seconds in
the connected-idle state. Firmware immediately kills WiFi on connect (you
can confirm this — Serial would show "WiFi off" if it were plugged in;
here just trust the graph, since disabling a whole radio should show as a
visible baseline drop).

### 3. Receiving the song program (BLE write burst)

Still connected, find the characteristic ending `...0006...` (Program) in
nRF Connect's service list. Write a chunk of arbitrary bytes to it a few
times in a row (content doesn't matter — the point is a real over-the-air
GATT write, which is what actually draws current, not what's in it).
Watch the peak during the write bursts.

### 4. Playing (periodic ring/damp notifies + LED)

Still connected, find the characteristic ending `...0007...` (Control) and
write a single byte `0x01`. The board starts alternating simulated
ring/damp events every 1.5s — LED on at ring, off at damp, each paired with
a BLE notify. Let it run through several cycles (~15–20s) and watch the
peak at each notify+LED transition. Write `0x00` to stop (or just
disconnect — either returns it to idle).

### 5. Receiving an OTA update

Disconnect the phone first (board returns to STATE_IDLE, WiFi/OTA back up
— give it a few seconds to reconnect). Then, from the PC, push a real OTA
update the same way as before:

```powershell
$espota = "C:\Users\asc1111\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.11\tools\espota.exe"
& $espota -i 192.168.35.201 -a YOUR_PASSWORD_HERE -f "<build dir>\xiao_c3_node.ino.bin" -r
```

Watch the graph for the whole transfer — expect a sustained elevated
plateau (flash writes + WiFi TX) for the transfer's duration, not just a
brief spike.

## 3. Results

| # | Activity           | Peak (mA) | Notes |
|---|---------------------|-----------|-------|
| 1 | Idle                | 310       | Average maintained 62-65 mA with spikes up to 200mA. At one point it was getting up to 300mA.      |
| 2 | App-connected       | ~190      | Average is about 55mA. Spikes up around 200mA every BLE wake up interval. Small interval means lots of power spikes.      |
| 3 | Program transfer    | ~195      | There is a small but repeatable spike of around 5-10mA in addition to the normal BLE spikes.      |
| 4 | Playing             | ~195      | The BLE spikes look similar to test #3. The average in between the spikes looks to go from 57mA to 60mA while the LED is on. The LED is a negligable draw on power.       |
| 5 | OTA update          | ~275      | Once the update began. The normal spikes of 191mA goes up to the 270's. Interestingly, after the OTA update, the board restarted and there was a peak of 300mA at startup.      |

## 4. Findings

**The BLE connection interval dominates everything else.** Compare
activities 2, 3 and 4: the actual *work* barely registers. Receiving a
program over GATT adds ~5–10mA on top of the BLE spike that was happening
anyway. The LED adds ~3mA (57mA → 60mA between spikes while lit). What
costs power is the radio waking up for a connection event **at all** —
which happens on a fixed schedule whether or not there is anything to say.

At `CONN_INTERVAL_MIN`/`MAX` = 6/12 (7.5–15ms, inherited from
`feather_transmitter.ino`'s low-latency tuning) that's ~65–130 radio
wake-ups per second, each a ~130–145mA spike over a ~55mA baseline. This is
why connecting (activity 2) made the trace *busier* than merely advertising
(activity 1) — connection events are far more frequent than advertising
events.

**This is a latency-vs-power dial, currently turned all the way to
latency.** A relaxed connection interval cuts the wake-up rate
proportionally, at the cost of ring/damp notifications arriving no sooner
than the next connection event. Deliberately not resolved here — the demo's
whole premise is synchronised low-latency ringing. Recorded as an open
tension to revisit with multi-board data.

**Activity 1 (idle) is the most expensive steady state, not the least.**
It's the only one running WiFi and BLE concurrently — exactly the
coexistence cost `docs/hardware-design.md` decision #3a avoids by keeping
one stack active at a time in the final bell. It matters for the demo rig
because all nine boards sit in this state at power-up, before the app has
connected to any of them.

**Caveat on the per-board average.** The "average" figures in the table
above are read as the level *between* spikes, not necessarily the
time-weighted average including them. Depending on spike duty cycle the
true average is somewhere between ~65mA and ~90mA per board — worth pinning
down against the Power Profiler's average-over-selection readout before any
precise capacity math, since it moves the figure by roughly 30%. The supply
conclusion below holds across that whole range, so it wasn't blocking.

**The Elegoo Power MB V2 is out.** Nine boards exceed its 700mA/rail limit
on *average* draw alone, before any peak overlap, and its linear regulators
would burn the surplus as heat regardless. Replacement direction is being
worked out separately; added requirements are portability (no wall socket)
and low cost — this is a concurrency test rig, not a product.

**Still unmeasured: how much the peaks actually overlap across boards.**
A single phone is the BLE central for all nine, and one radio cannot service
two peripherals in the same instant, so the phone's link layer necessarily
interleaves connection events rather than firing them in lockstep. That
makes "9 × 300mA simultaneously" an upper bound rather than the expected
case — but it is an argument, not a measurement. Worth re-running this rig
with 2–3 boards connected at once to see the real distribution.
