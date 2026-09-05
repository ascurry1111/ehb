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
| **Nothing may be bonded to the casting** | No adhesive, no permanent modification. Kills every bonded-target option. Mechanical clamping to the *existing handle stud* is taken as allowed — see note below. |
| **Full choir eventually** | 12+ ringers, 37-61 bells in one room. Airborne bleed between bells rules out microphones outright, and per-bell BOM cost becomes a first-order design driver. |

**Assumption worth checking:** "nothing touches it" is read here as *no
adhesive and no modification*, with clamping under the existing handle bolt
still permitted — that's normal handbell maintenance (handles are a
replaceable wear item), leaves no trace, and is the only rigid mechanical
interface the instrument offers. If even that is off the table, only the
non-contact options in section 3 survive.

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
restoring the permeability modulation. **The no-bonding constraint closes
that door**, which is a real loss — it was the closest thing to a genuine
guitar-pickup answer. What remains of magnetic sensing is eddy-current
sensing, covered in section 3.

> Caveat: if the instrument is actually an aluminium Choirchime-style tube
> rather than a bronze bell, it's still non-magnetic, but it is *much* more
> conductive — which materially improves the eddy-current option.

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

A C4-C7 set needs **usable response to at least ~8.5 kHz** just to capture
the second partial of the top bell; partials above that run past 15 kHz. A
transducer flat to ~10 kHz captures everything that matters for bells up to
about C6 and truncates the top octave's upper partials — an acceptable
first cut, and the reason low-bandwidth sensors get ruled out below.

There is also a broadband strike transient at onset (the clapper impact)
with energy well above the tuned partials. That transient is what the
trigger/velocity job keys off, so it must not be aliased or clipped away.

### Amplitude, and why mounting position dominates

Acceleration goes as `a = (2*pi*f)^2 * x`. At the rim, surface displacement
after a firm strike is on the order of tens of microns:

- `x` = 10 um at 1 kHz  ->  ~395 m/s^2  = **~40 g**
- `x` = 50 um at 1 kHz  ->  ~2000 m/s^2 = **~200 g**

At the crown, the casting is close to a node for the main ring modes —
that's precisely why the bell is held there — so motion is perhaps 20-40 dB
lower, call it **0.5-5 g**.

This drives the whole design:

- **Rim** = strongest, most representative signal. Nothing may be attached
  there, so it is reachable only by non-contact sensing.
- **Crown** = weak signal and a different spectral balance (node-adjacent
  sensing under-reports the modes that are nodal there), but it is the one
  place with a rigid, reversible mechanical interface.

**The central open technical question is whether the crown signal is good
enough**, because if it is, this becomes a $1 problem, and if it isn't, it
becomes a bracket-and-non-contact-sensor problem.

### Bell anatomy and candidate mount points

```
                 handle
                   |
             ======#======   (A) crown / handle stud  -- ALLOWED
              /           \      - threaded, on-axis, hidden, reversible
             |   clapper   |     - mass here is nearly free
             |      o      |     - weakest signal (near a node)
            /       |       \
           |       (O)       |  (B) near rim, NON-CONTACT only
          /         ^         \     - strongest signal
         |          |          |    - sensor rides a bracket cantilevered
        /     clapper strikes   \     from (A); never touches the bronze
       |________________________|   - keep ~90 deg from the strike axis
                  rim
```

- **(A) Crown / handle stud** — the handle bolts to a stud at the crown.
  The only rigid mechanical interface available under the constraints:
  threaded, on-axis, invisible, mass-tolerant, leaves no trace.
- **(B) Near the rim, without contact** — best signal, reachable only by
  a sensor held on a bracket. The bracket cantilevers down inside the cup
  from the crown stud, so the whole assembly is hidden, doesn't change the
  exterior, and doesn't interfere with damping the bell against the body.
  Must sit ~90 degrees around from the clapper's swing plane.
- **(C) Clapper head** — plastic, often replaceable. Great strike timing,
  but the clapper rebounds and decouples within milliseconds, so it yields
  the attack and not the sustain. Trigger only.
- **(D) Handle** — most convenient, worst signal. The handle assembly is
  deliberately compliant and damped to isolate the ringer's hand from the
  bell. Sensing there means sensing what the instrument was designed to
  throw away.

### Constraints imposed by the instrument

- **Mass and balance.** Ringers will reject anything that changes the
  bell's weight or balance noticeably. Target under ~25 g, kept on the
  bell's axis.
- **Damping technique.** Handbell ringing involves damping the bell against
  the body. Nothing may protrude where it interferes, or where it contacts
  the ringer. An inside-the-cup bracket is safe on both counts.
- **Bracket rigidity.** Any non-contact sensor is only as good as what
  holds it. A bracket's own resonances land directly in the signal. It
  mounts at the crown, which is the quietest point on the bell — helpful —
  but it must be stiff and its first mode should sit above the band of
  interest, or at least be characterised.
- **Feedback.** If the output is ever amplified through a speaker, contact
  and non-contact pickups are far more feedback-resistant than a mic.

### Cost scales with the choir

A 3-octave set is 37 bells; 5 octaves is 61. Every instrumented bell needs
its own sensor, micro, radio, and battery. The micro/radio/battery floor is
maybe $10-15 per bell whatever we choose, but the sensor multiplies:

| Sensor | Per bell | x37 bells | x61 bells |
|---|---|---|---|
| Piezo disc | ~$1 | ~$37 | ~$61 |
| Optical interrupter | ~$2 | ~$74 | ~$122 |
| Eddy-current (LDC1101 + coil) | ~$12 | ~$440 | ~$730 |
| IIS3DWB accelerometer | ~$20 | ~$740 | ~$1220 |

So the sensor choice roughly doubles total per-bell cost at the top end.
This doesn't bite for a single prototype, but it should steer the choice
now rather than force a redesign later.

---

## 2. Ruled out by the constraints

Recorded so they don't get re-proposed, with the reason each died.

| Approach | Killed by |
|---|---|
| Magnet or steel target bonded to the casting + coil | No bonding. **The biggest loss** — this was the true guitar-pickup answer, with real pickup-level output and pedalboard compatibility. |
| PVDF film strip taped to the casting | No bonding. |
| Microphone inside the cup | Choir. Bleed from 36 other bells makes it unusable, and it's the most feedback-prone option. |
| LIS3DH (already on hand) | Bandwidth. 5.376 kHz max ODR = ~2.6 kHz Nyquist. Misses the second partial of every bell above C5 and aliases the strike transient. Fine for its existing gesture-detection job on `../electric-handbell`; not a tone pickup. |
| Clapper-head sensor | Tone requirement. Clapper decouples on rebound — attack only, no sustain. Retained only as a possible trigger adjunct. |

---

## 3. Surviving options

| # | Approach | Senses | Contact | Per bell | Effort | Read |
|---|---|---|---|---|---|---|
| A | Piezo disc at crown stud | Strain | Clamped | ~$1 | Low | Try first |
| B | Optical edge occlusion at rim | Displacement | None | ~$2 | Medium | Best non-contact bet |
| C | Eddy-current LDC at rim | Displacement | None | ~$12 | High | Fallback |
| D | High-bandwidth accel at crown | Acceleration | Clamped | ~$20 | Medium | Clean, pricey |
| E | Passive magnet + coil at rim | Velocity | None | ~$3 | Low | Cheap long shot |

### A — Piezo disc at the crown stud

A 27 mm brass-backed piezo disc sandwiched under the handle bolt. The
cheapest thing that could possibly work, and the only option that stays
trivial at 61 bells.

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
  (a free 27 mm disc is around 4-6 kHz; clamped it shifts) which stamps a
  peak on the response; and the crown is node-adjacent, so the spectral
  balance will be skewed relative to what the bell actually sounds like.
- Clamping pressure will matter a lot and is worth treating as a variable,
  not a fixed choice.

### B — Optical edge occlusion at the rim

The most interesting option now that bonding is out, and it deserves more
attention than it would have got otherwise.

Straddle the bell's rim edge with a slotted photointerrupter (or a discrete
IR LED / photodiode pair) on a bracket cantilevered from the crown stud.
As the rim vibrates, it occludes more or less of the beam, and the
photocurrent tracks the displacement directly.

- **Nothing touches the bell**, zero mass loading, and it reads the rim —
  the strongest-signal location — which is exactly the combination the
  constraints otherwise make impossible.
- Sensitivity is promising: across a ~1 mm beam, 20 um of edge motion is
  ~2% modulation, which is easy for a low-noise transimpedance amp to
  resolve. Edge occlusion is far more sensitive to small displacements than
  reflective intensity sensing — **use occlusion, not reflectance**, and
  don't be tempted by the simpler reflective sensor.
- Bandwidth is effectively free; photodiodes and TIAs run far past audio.
- Cheap enough to scale to a full choir.
- Watch for: ambient light (use IR with an optical filter, or modulate the
  emitter and demodulate); beam alignment and gap setting per bell; the
  rim's main modes move *radially*, so orient the beam so radial motion is
  what cuts it; and bracket rigidity, since the bracket's own resonances go
  straight into the signal.

### C — Eddy-current / inductive sensing at the rim

Drive an LC tank near the bronze; eddy currents induced in the casting load
the coil, and the bell's motion modulates that loading. This is how
industrial proximity probes watch turbine shafts.

- **TI LDC1101** does this in one chip at up to ~183 kSPS — ample for audio
  bandwidth. (The multi-channel LDC1614 is far too slow, ~4 kSPS.)
- Senses displacement. Truly non-contact.
- Bronze is a mediocre conductor (~7-15% IACS, well below copper or
  aluminium), so eddy currents are weak and sensitivity suffers relative to
  the usual aluminium/steel targets.
- Highest complexity here, gap-sensitive, and by far the worst per-bell
  cost at choir scale.

The fallback if optical alignment proves impractical. Same bracket problem,
more expensive chip, but no optical-alignment or ambient-light issues.

### D — High-bandwidth MEMS accelerometer at the crown stud

The rigorous version of A: clean, digital, no analog front end to get
wrong, and it reuses firmware DNA from `../electric-handbell/firmware`.

- **ST IIS3DWB** — 26.7 kHz ODR, flat to ~6.3 kHz, +/-16 g, SPI.
  Purpose-built for vibration monitoring, and +/-16 g suits the ~0.5-5 g
  seen at the crown.
- Mount rigidity is everything: a compliant mount is a low-pass filter with
  a resonant peak. Bolt it to the crown stud metal-to-metal, not through
  rubber.
- Same node-adjacent spectral-balance limitation as A — it's a cleaner
  version of the same measurement, not a better vantage point.
- ~$20/bell makes it expensive across a choir. Good candidate for the
  prototype even if the shipping design lands elsewhere, because it gives a
  trustworthy reference measurement of what the crown actually offers.

*(Verify exact ODR/bandwidth figures against current datasheets before
ordering.)*

### E — Passive magnet + coil at the rim (cheap long shot)

Worth being precise about why this is different from the bonded-target
option that the constraints killed. With no target on the bell, a magnet
and coil near the rim still produce *something*: the bronze moving through
the static field develops motional eddy currents, whose secondary field
links the coil. Non-contact, passive, no bonding, no chip.

Honest assessment: **probably too weak.** Active eddy-current sensing (C)
drives the coil at MHz so the induced currents are large and the motion
merely modulates the coupling; the passive version relies on the bell's own
sub-0.2 m/s surface velocity to generate the currents, in a mediocre
conductor. Expect output far below a guitar pickup on steel.

But a magnet and a salvaged coil cost nothing and the test is 20 minutes.
If it produces a usable signal it is the best answer on this list by a wide
margin — passive, non-contact, no per-bell silicon. Cheap lottery ticket;
buy the ticket, don't plan around winning.

---

## 4. Recommended path

**Do not start with the microcontroller.** Decouple "does this transducer
capture a good signal" from "can the ESP32 digitize it." The first question
is answered far faster with a laptop and a scope.

### Experiment 1 — is the crown good enough? (~$15, one evening)

This is the fork the whole project hangs on. If the crown signal is
usable, this is a $1-per-bell problem with a trivial mount. If it isn't,
everything moves to a bracket and a non-contact sensor at the rim.

1. 27 mm piezo disc + TL071 buffer (10 Mohm in, 1 Mohm series, clamp
   diodes), clamped under the handle bolt.
2. Record into a laptop line input or USB interface. **Simultaneously
   record the same strikes with a reference microphone a metre away.**
3. Compare spectra and decay envelopes.

The A/B against the reference mic is the whole experiment, and it's the
step that usually gets skipped. What it answers:

- Do the pickup's partial ratios match the acoustic ones, or has
  node-adjacent mounting skewed the balance past usefulness?
- Is there an obvious mount-resonance peak stamped on the response?
- Does the decay envelope track the acoustic decay, or does the mount damp
  it early?
- How much low-frequency handling thump comes through?
- Is the strike transient clean enough to derive onset and velocity from?

Vary clamping pressure while you're in there — it's free to test and likely
matters.

Keep these recordings permanently. They are the yardstick for every option
that follows.

### Experiment 2 — the 20-minute lottery ticket

While the bell is out: hold a neodymium magnet and any salvaged coil near
the rim, straight into a mic preamp, and see whether option E produces
anything at all above the noise floor. Costs nothing, and a positive result
reshapes the project.

### Then

- **Crown spectrum is good** -> stay at the crown. Ship option A; consider
  D for the prototype to get a trustworthy reference of what the crown
  offers, then decide whether the $20 part earns its place.
- **Crown spectrum is too skewed or too weak** -> build the bracket and go
  to option B at the rim. Prototype the bracket first and characterise its
  resonances *before* trusting any signal that comes through it.
- **Optical alignment proves impractical across bells** -> option C, same
  bracket, more expensive chip.

### Deferred

Onset/velocity extraction, pitch identification, and per-bell radio in a
37-61 transmitter choir are all downstream of this and out of scope here —
but note that a choir-scale radio design is a substantial problem in its
own right and shouldn't be discovered late.

---

## 5. Open questions

1. **Does clamping under the handle bolt count as "not touching"?** Assumed
   yes above. If no, options A and D die and this becomes a non-contact
   problem only.
2. **Is there a bell available to experiment on freely?** Bracket fitting
   and beam alignment need repeated assembly/disassembly.
3. **How many bells actually get instrumented in v1?** Prototyping one bell
   permits choices that don't survive to 37.
