# Bell Pickup — transducer concepts

Brainstorming notes for the sensing element: how to get a handbell's
vibration out as an electrical signal. Signal *processing* is deliberately
out of scope here — this is only about the transducer, its mounting, and
the front-end amplifier it needs.

Status: nothing built or measured yet. Every number below is an estimate
from first principles and should be replaced with a measurement.

---

## 0. The constraint that reshapes everything

**A guitar pickup will not work on a handbell casting.**

An electric guitar pickup is a permanent magnet wrapped in a coil. It works
because steel strings are *ferromagnetic*: as the string moves it changes
the reluctance of the magnetic circuit, modulating flux through the coil,
which induces a voltage. The magnet is not sensing motion directly — it is
sensing the motion of a **magnetically permeable** object.

Handbells are cast bronze (roughly 80% copper / 20% tin). Bronze is not
ferromagnetic. Hang a magnet-and-coil pickup next to a ringing handbell and
you get essentially nothing, because there is no permeability to modulate.

This does not close off magnetic sensing — it means we have to either put a
magnetic target on the bell, or drive the coil actively and sense eddy
currents. Both are covered below. But the naive "clamp a humbucker to it"
plan is dead on arrival, and it's worth knowing that before ordering parts.

> Caveat: if the instrument is actually an aluminium Choirchime-style tube
> rather than a bronze bell, it's still non-magnetic, but it is *much* more
> conductive — which makes the eddy-current option (Option 6) markedly
> better.

---

## 1. What we are actually sensing

### Frequency content

English handbells are tuned so the first strong overtone sits two octaves
above the fundamental (a 4:1 ratio), unlike tower bells with their minor
third. Further partials continue above that.

| Bell | Fundamental | 2nd partial (4x) |
|---|---|---|
| C4 | 262 Hz | 1046 Hz |
| C5 | 523 Hz | 2093 Hz |
| C6 | 1047 Hz | 4186 Hz |
| C7 | 2093 Hz | 8372 Hz |

So a C4-C7 set needs **usable response to at least ~8.5 kHz** just to
capture the second partial of the top bell; partials above that run past
15 kHz. A transducer flat to ~10 kHz captures everything that matters for
bells up to about C6 and truncates the top octave's upper partials —
probably an acceptable first cut, but it's the reason a low-bandwidth
sensor gets ruled out below.

There is also a broadband strike transient at onset (the clapper impact)
with energy well above the tuned partials.

### Amplitude, and why mounting position dominates

Acceleration goes as `a = (2*pi*f)^2 * x`. At the rim, surface displacement
after a firm strike is on the order of tens of microns:

- `x` = 10 um at 1 kHz  ->  ~395 m/s^2  = **~40 g**
- `x` = 50 um at 1 kHz  ->  ~2000 m/s^2 = **~200 g**

At the crown, the casting is close to a node for the main ring modes —
that's precisely why the bell is held there — so motion is perhaps 20-40 dB
lower, call it **0.5-5 g**.

This single fact drives the whole design:

- **Rim** = strongest, most representative signal, but anything attached
  there adds mass at an antinode, which detunes and damps the bell.
- **Crown** = weak signal and a different spectral balance (node-adjacent
  sensing under-reports the modes that are nodal there), but mass there is
  nearly free and there is already a threaded stud to bolt to.

Non-contact sensing at the rim is the move that escapes the trade-off. That
is the strongest argument for Options 5 and 6.

### Bell anatomy and candidate mount points

```
                 handle
                   |
             ======#======   (A) crown / handle stud
              /           \      - threaded, no adhesive needed
             |   clapper   |     - on-axis, hidden, mass is free
             |      o      |     - weakest signal
            /       |       \
           |       (O)       |  (B) inside casting near rim
          /         ^         \     - strongest signal
         |          |          |    - mount 90 deg from strike axis
        /     clapper strikes   \   - needs adhesive OR a non-contact
       |________________________|      bracket cantilevered from (A)
                  rim
```

- **(A) Crown / handle stud** — the handle bolts to a stud at the crown.
  Best mechanical interface in the whole instrument: threaded, on-axis,
  reversible, invisible, and mass-tolerant.
- **(B) Inside the casting, near the rim** — best signal. Must sit ~90
  degrees around from the clapper's swing plane so it never gets hit. Fully
  hidden, doesn't change the exterior, doesn't interfere with damping the
  bell against the shoulder.
- **(C) Clapper head** — plastic, often replaceable, zero modification to
  the bronze. Great strike timing, but the clapper rebounds and decouples
  within milliseconds, so you get the attack and not the sustain.
- **(D) Handle** — most convenient, worst signal. The handle assembly is
  deliberately compliant and damped to isolate the ringer's hand from the
  bell. Sensing there means sensing what the instrument was designed to
  throw away.

### Constraints imposed by the instrument

- **Do not permanently modify the casting.** Handbells run $150-$1000+
  each. Adhesive on a polished bronze exterior is a hard sell; adhesive
  inside the casting is much easier to accept.
- **Mass and balance.** Ringers will reject anything that changes the
  bell's weight or balance noticeably. Target under ~25 g, kept on the
  bell's axis.
- **Damping technique.** Handbell ringing involves damping the bell against
  the body. Nothing may protrude where it interferes with that, or where it
  contacts the ringer.
- **Ensemble isolation.** In a choir of 12+ ringers, an open microphone
  hears every other bell in the room. Contact and magnetic pickups hear
  only their own bell. This alone likely disqualifies Option 7 for ensemble
  use.
- **Feedback.** Same argument: if the output is ever amplified through a
  speaker, a contact pickup is far more feedback-resistant than a mic.

---

## 2. Options

| # | Approach | Senses | Bandwidth | Touches bell? | Cost | Effort |
|---|---|---|---|---|---|---|
| 1 | Piezo disc / contact mic | Strain | Good, with mount resonance | Yes | ~$5 | Low |
| 2 | PVDF film strip | Strain | Wide, flat | Yes (tape) | ~$5 | Low |
| 3 | High-bandwidth MEMS accel | Acceleration | To 6-11 kHz | Yes (bolted) | ~$20 | Medium |
| 4 | Existing LIS3DH | Acceleration | ~2 kHz — too low | Yes | on hand | Low |
| 5 | Magnet on bell + coil | Velocity | Wide | Magnet only | ~$10 | Medium |
| 6 | Eddy-current / inductive | Displacement | Ample | **No** | ~$25 | High |
| 7 | Microphone inside the cup | Pressure | Full | **No** | ~$5 | Low |
| 8 | Clapper-mounted sensor | Impact | n/a | No (clapper) | ~$5 | Low |

### Option 1 — Piezo disc contact pickup

The obvious first try. A 27 mm brass-backed piezo disc clamped under the
handle bolt at the crown, or bonded inside the casting.

- Output is large — tens of mV to several volts — and needs a **high
  impedance load**. Source capacitance is ~15-20 nF; into 1 Mohm the low
  corner is ~8 Hz, but into a typical 10-50 kohm line input the corner
  lands in the hundreds of Hz and the result sounds thin and clacky. This
  is the single most common reason DIY piezo pickups sound bad.
- Front end: non-inverting unity-gain buffer (TL071, OPA1642, or a 2N5457
  JFET follower) with ~10 Mohm to ground. Add a 1 Mohm series resistor and
  back-to-back clamp diodes to the rails — a hard strike can produce tens
  of volts.
- Weakness: the disc plus its mounting has its own mechanical resonance
  (a free 27 mm disc is around 4-6 kHz; clamped it shifts) which stamps a
  peak on the response.
- An off-the-shelf instrument contact pickup (K&K / Schaller / Shadow
  style) is worth trying alongside a bare disc — they are voiced and
  buffered already, and set a useful baseline for "what does a competently
  made contact pickup sound like on this bell."

### Option 2 — PVDF piezo film

A laminated PVDF strip (TE LDT0-028K or similar, ~$5). Essentially zero
mass loading, flexible, tapes on, wide and comparatively flat response.

Senses surface *strain* rather than acceleration. Bronze is stiff so
surface strains are small, meaning lower output than a disc — but the
near-zero mass loading makes it the least invasive contact option, and the
best candidate for a rim-adjacent mount where a disc's mass would be a
problem.

### Option 3 — High-bandwidth MEMS accelerometer

The rigorous version of Option 1, and the natural upgrade path given the
firmware DNA already in `../electric-handbell/firmware`.

- **ST IIS3DWB** — 26.7 kHz ODR, flat to ~6.3 kHz, +/-16 g, SPI.
  Purpose-built for vibration monitoring. Good crown-mount choice: +/-16 g
  suits the ~0.5-5 g seen at the crown, and 6.3 kHz covers everything up to
  about C6.
- **ADI ADXL1002** — analog out, +/-50 g, 11 kHz bandwidth. **ADXL1005** —
  +/-100 g, 23 kHz. These suit a rim mount where 40-200 g is expected, but
  need an external ADC.

Mount rigidity is everything: a compliant mount is a low-pass filter with a
resonant peak. Bolt it to the crown stud through a printed clamp with a
metal-to-metal interface, not through rubber.

*(Verify exact ODR/bandwidth figures against current datasheets before
ordering.)*

### Option 4 — The LIS3DH already on hand

Worth stating explicitly so it doesn't get assumed into the design: the
LIS3DH tops out at 5.376 kHz ODR, so ~2.6 kHz Nyquist before its
anti-aliasing behaviour is even considered. That is **fine for its current
job** (ring-gesture detection on the electric handbell) and **not adequate
for tone capture** — it would miss the second partial of every bell above
C5 and alias the strike transient back into the audible range.

Still useful as a trigger/gesture sensor in this product. Not useful as the
pickup.

### Option 5 — Magnet on the bell + coil ("the actual guitar pickup answer")

Since the bell won't modulate a magnetic circuit, give it something that
will. Two variants:

**5a — Magnet on the bell, coil on a bracket.** Bond a small neodymium
magnet (3 mm, ~0.2 g) to the inside of the casting near the rim, and
cantilever a coil from the crown stud to sit ~1 mm away. Now it is a
genuine moving-magnet transducer: output is proportional to dPhi/dt, so it
senses **velocity** and has the same natural high-frequency tilt that gives
guitar pickups their brightness — which suits a bell's upper partials.

Rough output estimate: bell surface velocity is `v = 2*pi*f*x` = ~0.13 m/s
for 20 um at 1 kHz. A guitar string moves at more like 1-2 m/s, so expect
roughly 20 dB less than a guitar pickup — i.e. **millivolts, dynamic-mic
territory**, needing ~40 dB of preamp. Entirely workable.

**5b — Steel target on the bell, real guitar pickup on the bracket.** Bond
a thin steel disc instead of a magnet and aim an actual single-coil or
humbucker at it. Restores the permeability modulation, so you get real
guitar-pickup output levels and character, and can plug straight into a
guitar amp or pedalboard.

Both variants put a fraction of a gram on the bell, hide everything inside
the casting, and leave the exterior and the handling untouched. 5b is the
most literal answer to "like an electric guitar pickup," and the most
interesting one for the product story if the goal is running a bell through
effects.

The catch for both: **something has to be bonded to the bronze.** Inside
the casting, with a thin stiff adhesive (3M VHB or similar), this is far
more palatable than anything on the exterior — but it is still a
modification and needs a removability plan.

### Option 6 — Eddy-current / inductive sensing (truly non-contact)

The only option that touches the bell *not at all*. Drive an LC tank near
the bronze; eddy currents induced in the casting load the coil, and the
bell's motion modulates that loading. This is how industrial proximity
probes watch turbine shafts.

- **TI LDC1101** does this in one chip at up to ~183 kSPS — ample for audio
  bandwidth. (The multi-channel LDC1614 is far too slow, ~4 kSPS.)
- Senses **displacement**, not velocity or acceleration.
- Bronze is a mediocre conductor (~7-15% IACS, well below copper or
  aluminium), so eddy currents are weak and sensitivity suffers relative to
  the usual aluminium/steel targets.
- Highest complexity of anything here, and gap-sensitive.

Filed as the ambitious branch: zero modification, zero mass loading, rim
placement, no compromise to the instrument at all. Worth pursuing if the
no-adhesive constraint turns out to be absolute.

### Option 7 — Microphone in the cup

The simplest thing that produces audio. A MEMS or electret mic on a bracket
inside the casting.

Critical detail: **the inside of a ringing handbell is brutally loud** —
well over 110 dB SPL. A standard MEMS mic (AOP ~120 dB) clips instantly and
sounds like garbage. This needs a high-AOP part (>=130 dB SPL) or an
electret with a deliberately padded bias.

Disqualified for ensemble use by bleed from other bells, and poor for
amplified use by feedback — but it is the fastest way to get *a* signal,
and a useful reference recording to judge the other options against.

### Option 8 — Clapper-mounted sensor

Put a piezo or accelerometer in the clapper head. Nothing touches the
bronze at all, the head is plastic and often replaceable, and the strike
timing is perfect.

But the clapper rebounds off the casting within milliseconds and is then
mechanically decoupled — you get the attack transient, not the sustained
tone. **Excellent trigger, useless as a tone pickup.** Note also that
clapper mass and spring tension are part of how the bell feels to ring, so
added mass there is not free the way crown mass is.

---

## 3. Recommended path

**Do not start with the microcontroller.** Decouple "does this transducer
capture a good signal" from "can the ESP32 digitize it." The first question
is answered far faster with a laptop.

### Experiment 1 — baseline, ~$15, one evening

1. 27 mm piezo disc + TL071 buffer (10 Mohm in, 1 Mohm series, clamp
   diodes).
2. Mount it two ways: clamped under the handle bolt at the crown, and
   temporarily stuck to the shoulder with putty.
3. Record into a laptop line input or USB interface. Simultaneously record
   the same strikes with a decent microphone a metre away.
4. Compare spectra and decay envelopes in Audacity or similar.

**The A/B against a reference mic is the whole point**, and it's the step
that usually gets skipped. What it answers:

- Do the pickup's partial ratios match the acoustic ones, or is the balance
  badly skewed?
- Is there an obvious mount-resonance peak stamped on the response?
- Does the decay envelope track the acoustic decay, or does the mount damp
  it early?
- How much low-frequency handling thump comes through?
- How much does the mount audibly change the bell?

That single measurement tells us whether Option 1 is already good enough,
or whether the crown really is too node-adjacent and we need a non-contact
sensor down near the rim.

### Then, depending on the result

- **Spectrum decent, just coloured** -> stay contact-based, move to Option 3
  (IIS3DWB) for a clean, rigid, digital version.
- **Crown signal too weak or too skewed** -> Option 5, non-contact coil at
  the rim. 5b if adhesive is acceptable and guitar-amp compatibility is
  appealing; 5a if minimum mass matters more.
- **Nothing may touch the bell at all** -> Option 6, and accept the
  complexity.

Keep the reference mic recordings from Experiment 1 permanently — they are
the yardstick for everything after.

---

## 4. Open questions

1. **Tone or trigger?** Capturing the bell's actual voice for processing
   (effects, amplification) is a very different requirement from detecting
   onset + velocity to fire a sample. The former needs ~10 kHz of honest
   bandwidth; the latter is satisfied by almost anything on this list,
   including hardware already on hand.
2. **Is bonding anything to the casting acceptable?** This gates Options 5
   and 2 entirely, and pushes toward 6 if the answer is no.
3. **One bell or a choir?** Ensemble use rules out the microphone and
   raises the bar on isolation.
4. **Whose bell is the prototype?** A cheap import bell to experiment
   destructively on is worth having before touching a Malmark or
   Schulmerich.
