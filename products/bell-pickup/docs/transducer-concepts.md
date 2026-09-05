# Bell Pickup — transducer concepts

Brainstorming notes for the sensing element: how to get a handbell's
vibration out as an electrical signal. Signal *processing* is deliberately
out of scope here — this is only about the transducer, its mounting, and
the front-end amplifier it needs.

Status: nothing built or measured yet. Every number below is an estimate
from first principles and should be replaced with a measurement.

---

## Locked constraints

Decided up front; these rule out over half the option space, so they're
stated before the options rather than after.

| Constraint | Consequence |
|---|---|
| **Tone first, trigger derived from it** | Needs ~10 kHz of honest bandwidth. Onset/velocity falls out of a good audio signal; the reverse is not true, so design for audio. |
| **Nothing we add may touch the casting** | No adhesive, no modification, no clamping to the bronze. Handle, crown bolt and clapper assembly are all fair game — they're replaceable hardware, not the instrument. |
| **It must be a removable adapter** | The thing being sold is an accessory, not a modified bell. It has to come off and leave no trace. |
| **It must not impede the bell's vibration** | Rules out anything that loads or damps the casting, and anything that obstructs damping technique or normal handling. |
| **Full choir eventually** | 12+ ringers, 37-61 bells in one room. Airborne bleed between bells rules out microphones outright, and per-bell BOM cost becomes a first-order design driver. |

**The consequence that matters most:** nothing *we add* touches the bronze,
but the **crown bolt already does** — it is original equipment, clamped
under tension against the casting. That makes it a legitimate vibration
path we can tap without modifying anything.

---

## 0. The physics that reshapes everything

**A guitar pickup will not work on a handbell casting.**

An electric guitar pickup is a permanent magnet wrapped in a coil. It works
because steel strings are *ferromagnetic*: as the string moves it changes
the reluctance of the magnetic circuit, modulating flux through the coil,
which induces a voltage. The magnet is not sensing motion directly — it is
sensing the motion of a **magnetically permeable** object.

Handbells are cast bronze (roughly 80% copper / 20% tin). Bronze is not
ferromagnetic. Hang a magnet-and-coil pickup next to a ringing handbell and
you get essentially nothing, because there is no permeability to modulate.

The usual escape is to bond a steel or magnetic target to the instrument,
restoring the permeability modulation. **The no-contact constraint closes
that door**, which is a real loss — it was the closest thing to a genuine
guitar-pickup answer, with real pickup-level output and pedalboard
compatibility. What remains of magnetic sensing is eddy-current sensing.

> Caveat: if the instrument is actually an aluminium Choirchime-style tube
> rather than a bronze bell, it's still non-magnetic, but it is *much* more
> conductive — which materially improves the eddy-current option.

---

## 1. The instrument

### Anatomy, and what the crown bolt is

Every English handbell has a single fastener through a hole in the top of
the casting. The handle bolts to it on the outside; the clapper assembly
hangs from it on the inside. Exact hardware differs between makers
(Malmark, Schulmerich, Whitechapel) — check the actual bell — but
functionally they all have one.

**This is the only rigid mechanical interface the instrument offers**, and
removing the handle is routine maintenance rather than a modification.
Everything below leans on it.

```
                 handle                     <- replaceable, fair game
                   |
             ======#======   crown bolt     <- FAIR GAME, and already
              /           \                    touching the casting
             |   clapper   |                <- replaceable, fair game
             |      o      |
            /       |       \
           |       (O)       |   inside the cup: a bracket can hang
          /         ^         \  from the crown bolt without touching
         |          |          | the bronze, provided it stops short
        /     clapper strikes   \ of the mouth plane
       |________________________|
                  lip                       <- most movement, hands off
```

- **Crown** — the top of the casting, close to a node for the main ring
  modes. That is why the bell is held there.
- **Lip / mouth** — the open bottom edge, where the casting moves most.
- **Sound bow** — the thickened band just above the lip that the clapper
  strikes.

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

A C4-C7 set needs **usable response to at least ~8.5 kHz** just to capture
the second partial of the top bell; partials above that run past 15 kHz. A
transducer flat to ~10 kHz captures everything that matters for bells up to
about C6 and truncates the top octave's upper partials — an acceptable
first cut, and the reason low-bandwidth sensors get ruled out below.

There is also a broadband strike transient at onset with energy well above
the tuned partials. That transient is what the trigger/velocity job keys
off, so it must not be aliased or clipped away.

### Amplitude, and why mounting position dominates

Acceleration goes as `a = (2*pi*f)^2 * x`. At the lip, surface displacement
after a firm strike is on the order of tens of microns:

- `x` = 10 um at 1 kHz  ->  ~395 m/s^2  = **~40 g**
- `x` = 50 um at 1 kHz  ->  ~2000 m/s^2 = **~200 g**

At the crown, motion is perhaps 20-40 dB lower, call it **0.5-5 g**.

So the crown is a weak, spectrally skewed vantage point — but it is the
only one we are allowed to bolt to, and mass there is nearly free because
it sits at a node. **Whether the crown signal is good enough is the central
open question of this whole project.**

### Cost scales with the choir

A 3-octave set is 37 bells; 5 octaves is 61. Every instrumented bell needs
its own sensor, micro, radio and battery. The micro/radio/battery floor is
maybe $10-15 per bell whatever we choose, but the sensor multiplies:

| Sensor | Per bell | x37 bells | x61 bells |
|---|---|---|---|
| Piezo disc | ~$1 | ~$37 | ~$61 |
| Eddy-current (LDC1101 + coil) | ~$12 | ~$440 | ~$730 |
| IIS3DWB accelerometer | ~$20 | ~$740 | ~$1220 |

The sensor choice roughly doubles total per-bell cost at the top end. It
doesn't bite for a single prototype, but it should steer the choice now
rather than force a redesign later.

---

## 2. Ruled out

Recorded so they don't get re-proposed, with the reason each died.

| Approach | Killed by |
|---|---|
| Magnet or steel target bonded to the casting + coil | No contact. **The biggest loss** — the true guitar-pickup answer. |
| PVDF film strip taped to the casting | No contact. |
| Piezo clamped directly to the bronze | No contact, and it would damp the casting. |
| Microphone inside the cup | Choir. Bleed from 36 other bells makes it unusable, and it's the most feedback-prone option. |
| LIS3DH (already on hand) | Bandwidth. 5.376 kHz max ODR = ~2.6 kHz Nyquist. Misses the second partial of every bell above C5 and aliases the strike transient. Fine for its existing gesture-detection job on `../electric-handbell`; not a tone pickup. |
| Clapper-head sensor | Tone requirement. Clapper decouples on rebound — attack only, no sustain. Retained only as a possible trigger adjunct. |
| **Optical edge occlusion at the lip** | **Product mechanics** — see section 4. The physics works; the bracket it requires does not survive contact with a real ringer. |

---

## 3. Surviving options

All of these mount to the crown bolt, directly or via a bracket that stays
inside the cup.

| # | Approach | Senses | Per bell | Effort | Read |
|---|---|---|---|---|---|
| A | Piezo on a crown-bolt adapter | Strain | ~$1 | Low | **Try first** |
| B | High-bandwidth accel on the same adapter | Acceleration | ~$20 | Medium | Clean, pricey |
| C | Eddy-current coil on an internal bracket | Displacement | ~$12 | High | Non-contact fallback |
| D | Passive magnet + coil, internal bracket | Velocity | ~$3 | Low | Cheap long shot |

### A — Piezo on a crown-bolt adapter

The product shape the constraints point at: a replacement crown-bolt
adapter carrying the sensor. Unscrew the handle, drop the adapter in,
reattach the handle. Fully removable, no trace on the bell, sells as an
accessory rather than a modified instrument.

It is also the best available answer to "must not impede the vibration":
the crown is near a node, and the handle assembly already puts mass there.
Mass at the node is the cheapest mass on the instrument.

- Output is large — tens of mV to several volts — and needs a **high
  impedance load**. Source capacitance is ~15-20 nF; into 1 Mohm the low
  corner is ~8 Hz, but into a typical 10-50 kohm line input the corner
  lands in the hundreds of Hz and the result sounds thin and clacky. This
  is the single most common reason DIY piezo pickups sound bad.
- Front end: non-inverting unity-gain buffer (TL071, OPA1642, or a 2N5457
  JFET follower) with ~10 Mohm to ground. Add a 1 Mohm series resistor and
  back-to-back clamp diodes to the rails — a hard strike can produce tens
  of volts.
- Weaknesses: the disc plus its mounting has its own mechanical resonance
  which stamps a peak on the response; and the crown is node-adjacent, so
  the spectral balance will be skewed relative to what the bell actually
  sounds like.
- Bolt torque will matter a lot and is worth treating as a variable, not a
  fixed choice.
- The only option that stays trivial at 61 bells.

### B — High-bandwidth MEMS accelerometer, same adapter

The rigorous version of A: clean, digital, no analog front end to get
wrong, and it reuses firmware DNA from `../electric-handbell/firmware`.

- **ST IIS3DWB** — 26.7 kHz ODR, flat to ~6.3 kHz, +/-16 g, SPI.
  Purpose-built for vibration monitoring, and +/-16 g suits the ~0.5-5 g
  seen at the crown.
- Mount rigidity is everything: a compliant mount is a low-pass filter with
  a resonant peak. Metal-to-metal against the bolt, not through rubber.
- Same node-adjacent limitation as A — a cleaner version of the same
  measurement, not a better vantage point.
- Worth using on the prototype regardless of what ships, because it gives a
  trustworthy reference measurement of what the crown actually offers.

*(Verify exact ODR/bandwidth figures against current datasheets before
ordering.)*

### C — Eddy-current coil on an internal bracket

The non-contact fallback if the crown proves too weak or too skewed.

Drive an LC tank near the bronze; eddy currents induced in the casting load
the coil, and the wall's motion modulates that loading. This is how
industrial proximity probes watch turbine shafts.

**The reason this beats optical under our constraints:** it senses the
*face* of the wall, not its edge. So the bracket can hang from the crown
bolt entirely inside the cup and stop short of the mouth plane. Nothing
protrudes, the bell still sets down mouth-down, damping technique is
unaffected, and the clapper can be cleared by sitting ~90 degrees around
from the strike axis.

- **TI LDC1101** does this in one chip at up to ~183 kSPS — ample for audio
  bandwidth. (The multi-channel LDC1614 is far too slow, ~4 kSPS.)
- Bronze is a mediocre conductor (~7-15% IACS, well below copper or
  aluminium), so eddy currents are weak and sensitivity suffers relative to
  the usual aluminium/steel targets.
- Gap-sensitive, and the bracket's own resonances land directly in the
  signal. It mounts at the crown, the quietest point on the bell, which
  helps — but it must be stiff and its first mode characterised.
- Worst per-bell cost at choir scale.

### D — Passive magnet + coil, internal bracket (cheap long shot)

With no target on the bell, a magnet and coil near the inner wall still
produce *something*: the bronze moving through the static field develops
motional eddy currents, whose secondary field links the coil. Non-contact,
passive, no bonding, no chip.

Honest assessment: **probably too weak.** Active eddy-current sensing (C)
drives the coil at MHz so the induced currents are large and the motion
merely modulates the coupling; the passive version relies on the bell's own
sub-0.2 m/s surface velocity to generate the currents, in a mediocre
conductor.

But a magnet and a salvaged coil cost nothing and the test is 20 minutes.
If it produces a usable signal it is the best answer on this list by a wide
margin — passive, non-contact, no per-bell silicon. Buy the lottery ticket;
don't plan around winning.

---

## 4. Optical edge occlusion — why it's out

Documented properly because it looked like the leading non-contact option
before the constraints were fully understood, and the reason it fails is
not obvious.

**The idea.** Run a vertical IR beam from an emitter below the bell's mouth
up to a photodiode above it, positioned so the lip partially cuts the beam.
Radial vibration of the lip modulates the light. Cheap, wide bandwidth,
zero mass loading, and it reads the lip — the strongest-signal location.

**Why the beam has to sit outside the lip.** The wall flares outward as it
descends, so a vertical beam grazing the *inner* surface at the lip runs
straight into bronze higher up. The beam must be outboard of the lip's
outer radius. That is forced by the geometry, not a design choice.

**Which forces a bracket that wraps the lip.** The emitter sits below the
mouth plane, the photodiode above and outside it. Starting from the crown
bolt, the bracket must run down inside the bell, around the lip, and back
up the outside.

**And that is what kills it:**

- Handbells are set down mouth-down on padded tables. A bracket wrapping
  the lip means the bell cannot be put down normally.
- The lip region is exactly where the ringer damps the bell against their
  body. An obstruction there breaks basic technique.
- It has to hold roughly 20 um of alignment while the bell is swung hard
  and damped, per bell, across 37-61 bells.
- A long cantilever wrapping a rim is close to the worst possible shape for
  stiffness, and the bracket's own resonances would dominate the signal.

The physics is sound. The mechanics do not survive contact with a real
ringer. Eddy-current sensing (option C) gets the same non-contact benefit
with a bracket that never leaves the cup.

---

## 5. Recommended path

**Do not start with the microcontroller.** Decouple "does this transducer
capture a good signal" from "can the ESP32 digitize it." The first question
is answered far faster with a laptop.

### Experiment 1 — is the crown good enough? (~$15, one evening)

This is the fork the whole project hangs on. If the crown signal is usable,
this is a $1-per-bell problem with a trivial, fully removable mount. If it
isn't, everything moves to an internal bracket and a non-contact sensor.

1. Piezo disc + TL071 buffer (10 Mohm in, 1 Mohm series, clamp diodes),
   sandwiched under the handle at the crown bolt.
2. Record into a laptop line input or USB interface. **Simultaneously
   record the same strikes with a reference microphone a metre away.**
3. Compare spectra and decay envelopes.

The A/B against the reference mic is the whole experiment, and it's the
step that usually gets skipped. What it answers:

- Do the pickup's partial ratios match the acoustic ones, or has
  node-adjacent mounting skewed the balance past usefulness?
- Is there an obvious mount-resonance peak stamped on the response?
- Does the decay envelope track the acoustic decay?
- How much low-frequency handling thump comes through?
- Is the strike transient clean enough to derive onset and velocity from?
- Does the bell sound any different with the adapter fitted?

Vary bolt torque while you're in there — free to test, and likely matters.

Keep these recordings permanently. They are the yardstick for everything
after.

### Experiment 2 — the 20-minute lottery ticket

While the bell is apart: hold a neodymium magnet and any salvaged coil near
the inner wall, straight into a mic preamp, and see whether option D
produces anything above the noise floor. Costs nothing, and a positive
result reshapes the project.

### Then

- **Crown spectrum is good** -> ship option A. Consider B on the prototype
  to get a trustworthy reference of what the crown offers, then decide
  whether the $20 part earns its place at choir scale.
- **Crown spectrum is too skewed or too weak** -> internal bracket, option
  C. Prototype the bracket first and characterise its resonances *before*
  trusting any signal that comes through it.

### Deferred

Onset/velocity extraction, pitch identification, and per-bell radio across
37-61 transmitters are downstream of this and out of scope here — but a
choir-scale radio design is a substantial problem in its own right and
shouldn't be discovered late.

---

## 6. Open questions

1. **Which bell is the reference?** Malmark and Schulmerich differ in crown
   hardware, and the adapter has to fit whatever it's designed around.
2. **Is there a bell available to experiment on freely?** The adapter needs
   repeated fitting and removal.
3. **How many bells get instrumented in v1?** Prototyping one bell permits
   choices that don't survive to 37.
4. **Does the adapter have to fit under the stock handle**, or can it
   replace the handle assembly entirely? Replacing it gives far more room
   for electronics and battery, at the cost of changing the feel.
