# Electric Handbell — Electronics Hardware Design

**Status:** in progress, v0.3 design phase. Decisions are recorded in §2 as
they're made; anything not there is still open.

**Scope:** the electronics inside the bell, only. The physical casting,
handle, and internal mounting geometry are out of scope here except where the
electronics impose a constraint on them — those are called out explicitly.
Firmware and receiver-side software are also out of scope; where a decision has
been deliberately deferred to software, this document says so rather than going
quiet on it.

---

## 1. Product context

Two use cases drive the hardware, and they pull in different directions.

**Personal practice.** One or two bells, a phone or PC as the receiver, an app
to play along with and get feedback from. Convenience matters most: the user
should be able to pick up a bell and connect to a phone with nothing else in
the bag.

**Ensemble performance.** A full bell choir, one central receiver (or a few)
processing every bell and driving standard audio outputs. Two consequences the
prototype has never had to face:

- **Quantity.** A 3-octave choir is ~11 ringers across 37 bells, and ringers
  routinely hold two and swap mid-piece. Assume **40–60 units live in one room,
  on one channel, at one time.** Every per-unit cost multiplies by 60, every
  milliamp multiplies by 60, and every collision in the air is a wrong note in
  a performance.
- **Identity.** The receiver must know *which* bell rang, not just that a bell
  rang. That requires a stable unique ID per unit, and some way for a human to
  tell which physical bell carries it — including finding the one that is
  misconfigured, in a room with 59 others.

Neither applies to a single-bell prototype. Both are now requirements.

---

## 2. Decisions made

| # | Decision | Choice | Rationale |
|---|---|---|---|
| 1 | Motion sensor type | **6-DOF IMU** (accel + gyro) | Removes the accel-only gravity-leakage failure class documented in `firmware/README.md`, and measures swing rate directly. See §3.1. |
| 2 | What quantity defines "loudness" | **Deferred to firmware** | A 6-DOF IMU captures every candidate — peak linear acceleration, integrated forward velocity, angular rate — in one data stream. Once the hardware's ranges are specified so that none of them clip, choosing between them is a tuning decision made against recorded traces, not a parts decision. The hardware obligation is range and resolution headroom. See §3.2. |
| 3a | Transports | **Both BLE and a low-latency link, one active at a time** | BLE keeps the no-dongle path for personal practice; the low-latency link serves ensemble. Never concurrent — running both stacks on one radio reintroduces the coexistence jitter recorded in `firmware/README.md`. Transport selection is a configuration function. |
| 3b | MCU variant | **Open — evaluate S3, C6 and C3 on real boards** | See §3.4 and §7. |
| 6 | Power switch | **Hard mechanical switch** for v0.3 | Simplest thing that satisfies the charge-while-off requirement. Soft latch revisited in a later design. |
| 7 | Runtime target | **2 hours of active use** | Neither a rehearsal nor a concert normally exceeds this. Capacity implications in §3.5. |
| 8 | Board strategy | **Dev boards for v0.3** | Still prototyping; custom PCB deferred. What dev boards can and cannot prove is set out in §7. |

Still open: #4 (multi-unit concurrency) and #5 (fuel gauge type). Listed in §6.

---

## 3. Requirements by functional block

### 3.1 Motion sensing — ring/damp discrimination

**Requirement.** Resolve a forward swing arrested by a sudden stop; distinguish
it from an omnidirectional soft contact (damp); reject everything else a
handbell does in normal playing — pickup, setdown, tilt, handle tap, backswing,
mart-lift, shake, thumb damp.

| Consideration | Requirement / note |
|---|---|
| **Accelerometer full-scale** | **±16g.** Observed peak linear acceleration reaches 5g, and the raw signal carries a 1g gravity offset through the same converter — so ~6g before any margin. ±8g leaves only ~33% headroom against an unmeasured worst case (hard martellato, a drop). See §5 for why this is not a theoretical concern. |
| **Gyroscope full-scale** | **±2000 dps**, pending measurement. A ring stroke rotates the bell through roughly 90° in ~0.25s — ~360°/s average, and peak runs well above average, before whatever the wrist snap at the stop adds. ±1000 dps is probably enough; ±2000 dps costs little, and we have just been burned once by choosing a range without measuring first. |
| **Resolution** | **16-bit output.** Acceleration is integrated into velocity, so bias error accumulates; 12-bit at ±16g is 8mg/LSB and too coarse to integrate cleanly. This is why v0.2 had to abandon the LIS3DH's 1.6kHz 8-bit low-power mode. |
| **Noise density** | Directly sets the smallest detectable `DAMP_CONTACT_JERK` — i.e. how soft a damp can be and still register. The LIS3DH is ~220µg/√Hz; current parts are several times quieter, and that margin is spent on soft-damp reliability. |
| **Interface** | **SPI plus a data-ready interrupt**, preferred over I2C polling. I2C at 400kHz costs ~0.15ms of blocking bus time per sample, and the polling loop adds jitter on top. This is a latency lever in the same class as the transport work. |
| **ODR headroom** | Raising the output data rate remains a deliberate, deferred latency lever — it invalidates three sample-count-based tuning constants, see the Android README. Choose a part with comfortable high-ODR headroom so the option stays open. |
| **FIFO** | Lets the MCU idle between bursts without dropping samples. Relevant to the power budget. |
| **On-chip event detection** | Some IMUs (LSM6DSOX class) carry a finite-state machine / ML core that can flag a gesture and raise an interrupt with no MCU involvement. Probably more complexity than it is worth, but it is the only path that removes the MCU from the detection latency entirely, and worth knowing about given how hard we are pushing on latency. |

**Constraint this places on the mechanical design.** The IMU must be **rigidly
coupled to the casting, at a known and repeatable orientation and radius from
the wrist pivot.** Compliance in the mount low-passes exactly the sharp stop the
detector keys on, and orientation drift between units means every unit needs
individual calibration. Stated here as an interface requirement; the mount
itself is designed elsewhere.

**Candidate parts to evaluate:** LSM6DSOX, LSM6DS3TR-C, ICM-42688-P, BMI270.

### 3.2 Loudness measurement

Separate from §3.1 because it asks a different question: *what physical
quantity best represents "how hard did they ring it," and which is most
repeatable?*

Three candidates, all derivable from one 6-DOF stream:

- **Peak linear acceleration** — what v0.2 uses. A differentiated quantity:
  noise-amplifying, clip-sensitive, and orientation-dependent.
- **Peak forward velocity** — an integral, so inherently smoother. Already
  computed and logged today as `peakFwd`; comparing it against `peakMilliG`
  over the same rings costs nothing but trace time.
- **Peak angular rate** — needs the gyro, and is the most physically analogous
  to what sets a real bell's dynamic.

Per decision #2 this is a firmware question. **The hardware obligation is only
that none of the three clip, and that all three resolve six musical dynamic
levels (pp–ff) distinguishably.** That obligation is what sets the ranges in
§3.1 — and note it is a *resolution and repeatability* requirement, not a
peak-range one, once clipping is eliminated.

### 3.3 Radio and link

**Requirement.** Carry ring / damp / dynamic to a receiver with minimum and
*predictable* latency, for up to ~60 concurrent units.

| Consideration | Note |
|---|---|
| **Silicon constrains the MCU** | ESP-NOW means Wi-Fi silicon, i.e. the ESP32 family. The transport decision effectively pre-selects the MCU vendor. |
| **Concurrency at ensemble scale** | ESP-NOW's peer table is capped (20, fewer when encrypted) — 60 unicast peers will not fit. Broadcast plus application-layer addressing scales, but then dedup and reliability become ours to own. |
| **Airtime and collisions** | 60 units, CSMA/CA, no retry coordination. A choir striking a chord is 11+ transmissions inside the same millisecond. **This is the largest technical risk in the ensemble use case**, and it is a radio-architecture question, not a firmware one. See §6/#4 and the experiment in §7. |
| **Channel selection** | 2.4GHz in a hall full of phones. A dedicated quiet channel, and the ability to change it, are requirements. |
| **Antenna** | A PCB antenna inside a plastic casting, wrapped in a human hand, swinging. Body proximity both detunes and absorbs. Antenna placement, keep-out and ground-plane geometry are first-class layout constraints — and among the things a dev board does not let us control (§7). |
| **TX power vs. battery** | v0.2 raised TX to +9dBm to suppress retries. Across 60 units that is simultaneously a power-budget and a mutual-interference decision. |
| **Transport switching** | Per decision #3a, exactly one stack active at a time, selected by configuration. The hardware requirement is silicon that supports both; the mutual exclusion is enforced in firmware. |

### 3.4 MCU

**Requirement.** Run detection at the sensor's output rate with deterministic
timing, drive the radio, manage power, and handle USB.

Selection considerations: **native USB** (removes the UART bridge, enables
clean DFU and configuration); **pre-certified module vs. bare chip** (see §4);
RAM and flash headroom; deep-sleep and wake-source support; and size and mass,
since this is swung by hand.

The three candidates, and what actually separates them:

| | ESP32-S3 | ESP32-C6 | ESP32-C3 |
|---|---|---|---|
| Core | Xtensa dual LX7 @240MHz | RISC-V @160MHz + LP core | RISC-V @160MHz |
| Wi-Fi / ESP-NOW | Wi-Fi 4 | Wi-Fi 6 | Wi-Fi 4 |
| BLE | 5.0 | 5.3 | 5.0 |
| 802.15.4 (Thread/Zigbee) | — | **yes** | — |
| USB | **full OTG** (can be HID/MIDI) | Serial/JTAG (CDC) | Serial/JTAG (CDC) |
| Role it suits | most headroom; the right chip for the **receiver dongle**, which needs HID/MIDI | most interesting for the **bell**, if 802.15.4 proves useful for concurrency | smallest and cheapest at ×60 |

Two observations worth recording:

- **The bell does not need full USB-OTG.** On the bell, USB is charge, DFU and
  configuration — CDC is sufficient, so USB does not rule out the C6 or the C3.
  The dongle is the part that wants HID or MIDI, and therefore wants the S3.
- **A two-SKU split is not yet justified.** The bell does identical detection
  work in both use cases; the only difference is which radio protocol is
  active, and all three variants do both Wi-Fi and BLE. Splitting SKUs doubles
  firmware, boards and inventory, and creates a personal-use bell that can
  never join a choir — which is exactly the upgrade path a customer would
  expect to have. If the C6's 802.15.4 turns out to solve the concurrency
  problem, the C6 is a single-SKU answer for both use cases. Evaluating all
  three on real boards first (§7) is the right way to settle this.

### 3.5 Power

**Topology.** "The battery charges whether the switch is on or off" is a
specific circuit requirement: **the charger connects directly to the battery
terminals, and the power switch sits downstream of that junction**, gating
battery→system only. A switch that breaks the battery connection outright
cannot satisfy it.

**Power path.** A separate question the requirement list implies but does not
state: with USB plugged in *and* the switch on, does the system run from USB or
from the battery? Running the load off a charging cell muddies the fuel gauge
and cycles the battery needlessly. A power-path / ideal-diode front end — VBUS
powers the system, the charger charges the cell independently — is the correct
answer, and is a real BOM line rather than something that comes free.

**Capacity.** Against decision #7 (2 hours active):

- v0.2's firmware assumes ~60mA average draw (`BATT_ASSUMED_DRAW_MA`, and its
  own comment calls that a rough figure). At 60mA, two hours is 120mAh.
- Derating for a sensible discharge floor, cell aging and temperature, call it
  ~2× → **250–350mAh minimum, 400–500mAh with real margin.**
- **The existing 500mAh cell therefore already meets the 2-hour target**, so
  capacity is not currently a design driver. That conclusion rests entirely on
  an assumed 60mA that has never been measured. Measuring actual average draw
  is on the §7 list.

**Cell selection.** Pouch vs. cylindrical; **retention and shock** — this cell
lives inside a swung, occasionally dropped object, and a pouch cell free to
move is a safety problem, not a rattle; temperature range; charge current
against the cell's C rating; and field replaceability.

**Fuel gauge.** Decision deferred (§6/#5), but the requirement is "accurate,"
which rules out what v0.2 does — a 200K/200K divider into an analog pin, with
charging state inferred from a voltage trend. The two real options are a
voltage-based ModelGauge (MAX17048 class: I2C, no sense resistor, tiny) or
coulomb counting (MAX17260 class: sense resistor in the return path, more
accurate under pulsed load, more BOM and layout care). **The pulsed load is the
crux** — a 200–300mA radio burst sags terminal voltage and reads as a
state-of-charge cliff on a voltage-only gauge. Conveniently, both candidate dev
boards (§7) carry a MAX17048 already, so the voltage-based option can be
validated against a reference measurement before anything is committed.

**Unresolved tension:** an accurate gauge wants to stay powered across an
off-cycle to keep its learned state, while "off" wants zero leakage. Either
pick one, or accept the gauge's µA-level quiescent draw as always-on.

**Brownout.** Wi-Fi TX bursts against cell ESR plus connector resistance can
dip the rail. Bulk capacitance and regulator headroom are design
considerations, not details — a brownout mid-ring is an audible failure.

### 3.6 USB-C port

External, on the casting. Charging, plus configuration and firmware update.

- **CC1/CC2 5.1kΩ pulldowns** for correct sink advertisement. The most common
  USB-C mistake; without them many chargers supply nothing at all.
- **Data** direct from the MCU's native USB — no bridge chip.
- **ESD protection** on a user-accessible connector: TVS array on D±, CC, VBUS.
- **Connector robustness and retention** — through-hole-reinforced or
  mid-mount. This port will be yanked. The connector is an electronics part
  even though the opening in the casting is a mechanical one.
- **No USB-PD needed.** Plain 5V at 500mA–1.5A is sufficient for one cell.

### 3.7 Power switch

Hard mechanical switch, per decision #6. Considerations: current rating;
leakage when off; actuation force and resistance to accidental actuation
mid-performance; and placement in the topology per §3.5, so that
charge-while-off holds.

### 3.8 Status LED

External, on the casting.

- **State count drives part count.** Power, charging, charge complete, battery
  low, link up/down, pairing/identify, fault. That is more than one discrete
  LED can carry — either a single RGB (WS2812 class, but note ~1mA quiescent
  even when dark, ×60 units) or two discrete LEDs.
- **Brightness must be adjustable** — dim enough not to distract in a darkened
  concert, legible in a lit rehearsal room.
- **An "identify me" function is worth designing in.** With 60 units, making
  one bell blink from the console is how you find the one that is
  misconfigured.

---

## 4. Cross-cutting considerations

- **Regulatory.** An intentional radiator needs FCC Part 15 / CE RED. Using a
  **pre-certified radio module** rather than a bare chip is close to mandatory
  unless full certification is being funded. This constrains §3.4 more than
  anything else in this document.
- **Battery safety and shipping.** UN38.3 and IEC 62133 apply once cells ship
  inside products.
- **Cost.** ×60 for one choir. A $5 BOM delta is $300 per set.
- **Shock and drop.** Handbells get dropped. Sensor, connectors and cell
  retention all need a survival spec.
- **Internal EMC.** Switching-regulator noise coupling into the IMU's analog
  path degrades exactly the small-signal jerk detection §3.1 depends on. Sensor
  supply filtering and layout are design constraints.
- **Serviceability.** Battery replacement, reflash without disassembly,
  per-unit serialization and labelling.
- **Mass and centre of gravity.** The electronics mass, and where it sits,
  change how the bell feels to ring — and therefore change the very gestures
  the detector is tuned against. A mass budget is an electronics deliverable
  even though the casting is designed elsewhere.

---

## 5. v0.2 baseline, and a defect it exposed

| Block | v0.2 | Gap against the above |
|---|---|---|
| MCU | Adafruit ESP32 Feather V2 (ESP32-PICO-MINI-02) | No native USB; no fuel gauge; no 802.15.4 |
| Sensor | LIS3DH, I2C, ±4g, 400Hz, 12-bit | **Clips — see below**; no gyro; polled over I2C |
| Radio | BLE only | No ensemble transport |
| Power | 500mAh LiPo, MCP73831 charger | No power path; no gauge; no switch |
| Gauge | 200K/200K divider on A13, trend inference | Not accurate in any meaningful sense |
| USB | CP2102N bridge | No native USB |
| Switch / LED | none / charger LED only, no GPIO tap | Requirements unmet |

**The ±4g clipping defect.** `feather_transmitter.ino` sets
`LIS3DH_RANGE_4_G`, while `DynamicLevel.kt` maps *linear* peaks up to 5.0g —
and the raw signal carries a 1g gravity offset through the same ±4g converter.
Each axis rails at 4g, but `peakMilliG` is a 3-axis vector magnitude, so how
much a hard ring clips depends on how the swing happens to distribute across
the axes. Two rings that feel identical but differ slightly in bell attitude
clip differently and report different peaks.

This is a strong candidate for the **"dynamic level varies noticeably between
rings that feel identical by hand"** issue recorded as unresolved in
`firmware/README.md` and in the v0.2 changelog entry — and it is a hardware
specification problem, not a tuning problem. It is the direct reason §3.1
specifies range by measurement and margin rather than by convenience.

---

## 6. Open decisions

| # | Decision | Status |
|---|---|---|
| 3b | ESP32 variant (S3 / C6 / C3), and one SKU or two | Evaluate on real boards — §7 |
| 4 | How ~60 units share the air: broadcast vs. unicast, dedup, reliability, and whether 802.15.4 helps | Open. Answerable by experiment — §7 |
| 5 | Fuel gauge: voltage-based ModelGauge vs. coulomb counting | Deferred — but now answerable by measurement at no extra cost, since both candidate Feathers carry a MAX17048 (§7, measurement 7) |
| — | IMU part selection from the §3.1 candidates | Open |
| — | Gyro full-scale range: confirm ±2000 dps against real traces | Open, needs measurement |
| — | Loudness quantity (deferred to firmware by decision #2) | Deferred |

---

## 7. v0.3 prototype plan

Dev boards, per decision #8. Being explicit about what that buys and what it
defers:

**What dev boards can prove**

- IMU selection, ranges, noise, and whether the gyro fixes gravity leakage
- Transport comparison, and the real latency distribution
- Multi-unit concurrency behaviour
- Actual average current draw, and therefore whether 500mAh really covers 2
  hours
- Charge-while-off: on a Feather the charger sits at the battery node and the
  switch gates the regulator enable, so **the requirement is satisfiable
  without a custom board** — with the caveat that regulator and gauge quiescent
  draw mean this is not a true zero-leakage off

**What dev boards cannot prove, and is therefore deferred to a custom board**

- Antenna placement, keep-out and ground-plane geometry (§3.3)
- Power-path front end (§3.5)
- Mass, centre of gravity, and board outline (§4)
- Connector and switch placement on the casting
- Internal EMC between the regulator and the IMU (§4)

### Boards and parts to obtain

Part numbers verified September 2026. Prices approximate except where noted.

**MCU boards**

| Item | Qty | ≈ each | What it answers |
|---|---|---|---|
| Adafruit ESP32-S3 Feather, 4MB flash / 2MB PSRAM (#5477) | 2 | $18 | One as the receiver dongle — the only candidate with full USB-OTG, and therefore the only one that can present as HID or MIDI; one as an S3 bell candidate |
| Adafruit ESP32-C6 Feather (#5933) | 2 | $13 | Bell candidate. Two rather than one, because testing 802.15.4/Thread as the concurrency answer needs a C6 at both ends |
| Seeed XIAO ESP32C3 | 6–8 | $9 | Concurrency traffic generators — see below |

Both Feathers carry a **MAX17048 fuel gauge on board**, which satisfies the
accurate-gauge requirement (§3.5) with no custom hardware, and turns open
decision #5 from an argument into a measurement — see the measurement list.

**On the C3's role.** As an MCU candidate it is weak: no 802.15.4, the same
Wi-Fi 4 generation as the S3, less headroom, and no Feather-form board
carrying the fuel gauge. Its real value here is as **cheap concurrency traffic
generators.** Six to eight XIAOs firing synthetic ring packets take measurement
4 from N=3 to N=8–10, and they need no IMU, no battery and no enclosure — just
USB power and a firmware loop. The "is the cheap chip adequate at ×60?" data
point comes along for free.

**IMU**

| Item | Qty | ≈ each | Note |
|---|---|---|---|
| Adafruit LSM6DSOX 6-DoF (#4438) | 2 | $12 | Start here. ±16g / ±2000 dps / 16-bit, and it breaks out **SPI (SCK, DO, SDA, CS) plus INT1**, so the SPI + data-ready-interrupt lever in §3.1 is genuinely testable rather than STEMMA QT I2C only. Also carries the MLC/FSM escape hatch noted in §3.1 |
| Adafruit LSM6DS3TR-C (#4503) | 1 | $8 | Optional. Cheaper part, same ranges, slightly noisier — worth knowing whether it suffices, since IMU cost multiplies by 60 |

**The ICM-42688-P is deferred.** It is the lowest-noise part on the §3.1
candidate list, but there is no first-party Adafruit or SparkFun breakout —
only Tindie, Elecrow and generic sellers — so it costs bring-up time for a
comparison that may not be needed. Order it only if the LSM6DSOX's noise floor
proves marginal for soft-damp detection.

**Test instrumentation**

| Item | Qty | ≈ each | What it answers |
|---|---|---|---|
| Nordic Power Profiler Kit II (Adafruit #5048) | 1 | $90 (verified) | Measurement 1. 200nA–1A range, so it captures both µA idle and the 200–300mA radio bursts that a multimeter averages away. Its digital inputs also work as a low-end logic analyzer with code-synchronized capture, covering much of the latency instrumentation |
| JST-PH 2-pin pigtail / extension leads | 2–3 | $1 | Needed to break into the battery line so the PPK2 sits between cell and board. Easy to omit, and omitting it blocks the entire power measurement |
| Piezo transducer or buzzer | 2 | $2 | The acoustic latency rig — measurement 6 |
| 8-channel USB logic analyzer | 1 | $15 | Optional, given the PPK2's digital inputs. Worth it for SPI bus decode |

**Consumables.** 500mAh LiPo cells (2–3, one per simultaneously tested unit);
STEMMA QT cables (3–4, for fast I2C bring-up before committing to SPI wiring);
male header strips (Feathers ship with headers loose, and the IMU breakouts
need soldering for SPI regardless); breadboard and jumper wires; two SPDT slide
switches, to prototype the §3.7 hard switch on the regulator-enable line and
confirm charge-while-off actually holds; USB-C cables.

**Approximate total:** ~$290 core, of which the PPK2 is $90. Trimming to one
C6, no LSM6DS3TR-C and no logic analyzer brings it to ~$215. The PPK2 and the
XIAO batch are the two purchases that unlock the measurements the open
decisions depend on, and should be the last things cut.

**Measurements to make**

1. Real average current draw, per variant, with each radio stack active —
   settles the capacity question in §3.5.
2. IMU traces of "identical" rings, to confirm the clipping diagnosis in §5 and
   to compare peak acceleration / peak velocity / peak angular rate as loudness
   proxies (decision #2).
3. Peak angular rate across ring, damp, martellato and shake — confirms the
   gyro range in §3.1.
4. **Concurrency experiment for #4:** N transmitters firing simultaneously into
   one receiver, counting losses and measuring the latency distribution as N
   scales. This converts an unanswerable architecture question into a
   measurement, and can start with three boards long before there are 60.
5. Latency distribution (p50/p95/p99), not averages — the tail is the problem.
6. **Acoustic end-to-end latency.** Fire a piezo click from a GPIO at the
   detection instant, record the click and the synthesized tone in one track,
   and measure the sample delta. This closes the `play()` → actually-audible
   term that is still blank in the Android README's latency budget, and it is
   the only measurement that captures the audio HAL. A phone voice recorder is
   adequate — both events pass through the same recording path, so the
   recorder's own latency is common-mode and cancels out of the delta.
7. **Fuel gauge validation.** The onboard MAX17048 against PPK2 ground truth
   under a pulsed radio load — a voltage-based gauge reads a TX burst's
   terminal-voltage sag as a state-of-charge cliff, and this shows whether that
   matters in practice. Answers open decision #5 at no extra hardware cost.
