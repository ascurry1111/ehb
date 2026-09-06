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

### Anatomy — Malmark, from the maker's construction drawing

Verified against Malmark's own exploded diagram for bells G3 through C8.
This supersedes earlier guesses in this doc, which had the clapper hanging
from the crown screw. **It does not.**

A single **main assembly screw** runs down the axis and clamps the handle
stack onto the crown. Outside to inside:

```
        handle              (handle block + handle assembly screw inside)
        handle block        drops into the handguard's pocket
        handguard           square pocket on its top face, keyed hole
        lockwasher, external tooth
        === bell casting ===        (plastic isolation sleeve in the hole)
        yoke                        inside, clamped to the crown

   the square coupler rod runs from the yoke up into the handle block,
   keys everything against rotation, and never touches the casting
```

The **clapper does not hang from the screw.** It rides on a horizontal
**clapper shaft** carried in the **yoke**, running on a **bearing block**
and **bearing screw**, with a **restraining spring** and an adjustable
screw setting its return. The clapper itself is a separate stack on the end
of that shaft: indexing spring, clapper, bowed spring washer, flanged
bushing, clapper assembly screw.

Consequences for us:

- **The crown screw stack is the only external interface**, and it is
  short. There is no room in it for an electronics pod — only a thin ring.
- **The yoke is a second, better-hidden interface.** It is a rigid metal
  part already clamped inside the crown, with its own fasteners (the
  bearing screw). Mounting a sensor there sidesteps the screw-length budget
  entirely and puts it unambiguously on the bell side of any isolation
  sleeve. Needs clearance checking against the clapper shaft.
- **The isolation sleeve is the biggest open risk.** If it only lines the
  hole, fine. If it also separates the handguard from the bronze in the
  *axial* load path, then Malmark has deliberately interrupted the exact
  vibration path option A depends on, and our signal arrives damped and
  low-passed. A 30-second look once a bell is apart.
- **Two construction variants.** This drawing covers G3-C8; C#8 and up use
  fixed metal clappers. A full-range product needs both.

Elsewhere on the bell:

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
| Piezo ring, catalogue part | ~$10 | ~$370 | ~$610 |
| Eddy-current (LDC1101 + coil) | ~$12 | ~$440 | ~$730 |
| IIS3DWB accelerometer | ~$20 | ~$740 | ~$1220 |

*Revised: earlier drafts assumed ~$1 for a piezo disc. Thin catalogue rings
are ~$10 each in twos, so the piezo option is no longer trivially cheap at
choir scale — though a custom ring from PI Ceramic or CeramTec, sized to the
handguard, should come well under that in volume.*

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
| A | Piezo ring in the crown stack | Strain | ~$10 | Low | **Try first** |
| B | High-bandwidth accel on the same adapter | Acceleration | ~$20 | Medium | Clean, pricey |
| C | Eddy-current coil on an internal bracket | Displacement | ~$12 | High | Non-contact fallback |
| D | Passive magnet + coil, internal bracket | Velocity | ~$3 | Low | Cheap long shot |

### A — Piezo ring in the crown stack

The crown is already a clamped bolted joint. The sensing element is one
thin ring added to it; the electronics live on the handle, because the main
assembly screw is too short to carry a pod in the stack.

```
        main assembly screw   threads into the coupler
             |
        [    pod     ]    NEW - in the handle's open space
        [   handle   ]    existing
        [handle block]    existing, drops into a square pocket
        [ handguard  ]    existing, square pocket on its top face
        [ piezo ring ]    NEW - position 2, the sensing element
        [ lockwasher ]    existing
        === casting ===   nothing new touches it
        [    yoke    ]    existing, inside; carries the clapper shaft

        the square coupler rod runs from the yoke all the way up
        into the handle block, and never touches the casting
```

Fit: unscrew the handle, add the ring, refit. Removal reverses it. Nothing
bonded, nothing machined, no contact with bronze.

**The ring is a load washer.** A piezo ring held in compression by the screw
preload; the bell's vibration modulates the force in the joint. Cheap piezo
rings are easy to source — they are the guts of ultrasonic cleaner
transducers.

**Only the mass above the ring contributes.** The screw is a series load
path, so the ring feels the same clamp force wherever it sits, but the
*dynamic* part comes from the inertial reaction of everything above it
pushing back as the crown accelerates. `F = m*a`, so with ~50 g of handle
above it at 0.5-5 g the dynamic force is a couple of newtons peak — easily
read by a piezo. This is exactly how a compression accelerometer is built:
seismic mass on a piezo stack.

**Moving the pod to the handle does not cost signal**, because the handle
sits above the ring in the same load path. One condition: it must be
rigidly coupled, mounted near the base of the handle close to the handle
block. Out on the loop it becomes a spring-mass system with its own
resonance, and you end up measuring the pod rather than the bell.

### How the joint actually clamps

Confirmed from the bell, not the drawing:

- The **yoke/handle coupler is a square metal rod**. It rises from the yoke
  inside the bell, through the casting, through the handguard, and into the
  handle block. The handguard and handle block have **square holes**, so
  the rod is what prevents rotation.
- The **main assembly screw threads directly into the coupler** and holds
  the whole assembly together.
- The **isolation sleeve is plastic**, and **the coupler never touches the
  casting.**

So the bronze is clamped between the **yoke** on the inside and the
**handguard/lockwasher** on the outside. Those two faces are the only
places vibration crosses from bell into hardware.

This corrects an earlier rule in this doc: the lockwashers are ordinary
lockwashers, *not* an indexing feature — the square rod does that. Nothing
in the stack has to transmit torque, so the ring only needs a bore that
clears the rod's **diagonal** (round-bore rings are fine).

### Where the ring goes

Three candidate positions:

| # | Position | Load path | Wiring |
|---|---|---|---|
| 1 | Under the screw head | Bolt tension — via yoke and coupler | Easy, inside the handle |
| 2 | Below the handguard | Clamped stack — via the outer face | Must exit at the handguard rim |
| 3 | Above the handguard | Clamped stack — via the outer face | **Blocked — see below** |

**Position 2 is the working choice.** Position 3 looked better on wiring
until the handguard's real geometry showed up: its handle-facing side has a
**square pocket** that the handle block drops into, and that pocket is the
handle's anti-rotation feature.

That kills position 3 twice over:

- **No wire route.** The pocket is a close fit by design. Squeezing even a
  25 um flex through it either binds the handle block — defeating the
  anti-rotation fit — or crushes the flex against a corner. It is the worst
  route on the assembly precisely because it is doing a mechanical job.
- **The pocket caps the ring OD.** The square is roughly half the
  handguard's width, so a ring there is limited to maybe 13-17 mm OD with a
  bore still clearing the square rod. That rules out the 30x21x0.3
  catalogue part and forces a small custom ring. At position 2 the ring can
  be as wide as the handguard underside, so the catalogue part fits.

*(This supersedes an earlier revision that recommended position 3.)*

**Position 3 is recoverable, but only via a replacement handguard** — see
the productization note at the end of this section.

**Position 1 is weak on joint mechanics.** A preloaded bolted joint splits
external dynamic load between the bolt and the clamped members by relative
stiffness. The members are stiff and take most of it; the bolt is compliant
and takes a small fraction — typically 10-30%. A ring under the screw head
measures the *bolt's* share, a 3-5x disadvantage before anything else.

**But which face carries the plastic may invert that.** The outside face is
the polished, visible one — the one you would protect from a toothed
lockwasher with a plastic flange. The inside face is hidden and
cosmetically irrelevant. So it is quite possible that position 1's path
(casting -> yoke -> coupler -> screw) is the **all-metal** one, while
positions 2 and 3 sense through a plastic flange. That would trade
position 1's poor load factor against position 2's damped path, and the
comparison is genuinely too close to call from the drawing.

**Resolution: fit both and measure.** Rings cost about a dollar and the
bell is already apart. First look at which of the two clamped faces
actually has plastic on it — that alone may settle it.

**Other build rules:**

- **Never against a lockwasher.** Piezo ceramic on a tooth cracks. It needs
  a flat shim on each face, which counts against the thickness budget.
- Budget roughly 1-3 mm for ring plus shims. The main assembly screw is
  itself fair-game hardware, so a few mm longer is legitimate if the
  coupler's thread has the depth.

**Ruled out: mounting inside the bell.** A sensor on the yoke would need
its pod inside too — too small, too hard to secure — or a wire run out the
mouth and down the outside, which is not a product. If the yoke ever
becomes necessary, the coupler is fair-game hardware and a replacement with
a wire bore is the route. Custom machined part, so it is a last resort, not
a plan.

### Sourcing the ring

0.3 mm rings are available off the shelf. **Steminc** is the practical
supplier — 2-piece sets, no minimum order, sells to individuals.

| Part | OD | Bore | Thick | Price /2 |
|---|---|---|---|---|
| **SMR3021T03412** | 30 mm | **21 mm** | **0.3 mm** | $19.98 |
| SMR3021T03311 | 30 mm | 21 mm | 0.3 mm | $34.29 |
| SMR28D9T03111 | 28 mm | 9 mm | 0.3 mm | $16.02 |
| SMR28D9T03NEN111 | 28 mm | 9 mm | 0.3 mm | $9.52 |
| SMR1585T07111 | 15 mm | 8.5 mm | 0.7 mm | $19.67 |

Listing: <https://www.steminc.com/PZT/EN/producttag/2/piezo-ring>

**Start with SMR3021T03412** — 0.3 mm thick with a 21 mm bore that clears
any plausible square-rod diagonal. The 28x9 parts are cheaper but their
9 mm bore only works if the coupler is ~6 mm across the flats or less.
Measure the rod's *diagonal* before ordering, and check the handguard face
diameter — 30 mm OD may be too large on a high bell.

For production quantities, PI Ceramic
(<https://www.pi-usa.us/en/products/piezo-transducers-sensing-ultrasound/piezoelectric-ceramic-rings>)
and CeramTec (<https://www.ceramtec-industrial.com/en/piezo-designs/rings>)
do custom ring geometries matched to the actual handguard.

**Alternatives if the geometry doesn't work:**

- **Piezo plates** (<https://www.steminc.com/PZT/en/piezo-plate>) — three
  small thin plates at 120 degrees around the rod instead of a ring.
  Sidesteps both bore and OD limits.
- **PVDF film** (<https://www.te.com/en/product-CAT-PFS0003.html>) — 28 um,
  essentially free on thickness. But it **creeps under sustained
  compression**, and 14 um of relaxation is comparable to a short bolt's
  entire elastic stretch. Fine for a bench test where you re-torque each
  time; not for a product that has to stay tight.

### Front end — use a charge amplifier, not a voltage buffer

Piezo *charge* output is independent of thickness. *Voltage* is not:
`V = d33 * F * t / (eps * A)`. Thin and wide is the worst case. The
30x21x0.3 ring gives roughly **13x less voltage** than a 12x5x1 ring under
the same force — most of the signal traded away to get the thickness down.

So stop reading voltage. Use a **charge amplifier**: inverting FET-input
op-amp with a feedback capacitor, `V = -Q / Cf`. Output becomes independent
of the ring's capacitance, area, thickness *and* the cable capacitance.
About 1 nF feedback with a 100 Mohm bleed resistor puts the low corner near
1.6 Hz.

This supersedes the voltage-follower front end described in earlier
revisions of this doc — correct for a thick disc, wrong for a thin wide
ring.

**Two build details that otherwise cost a build cycle:**

- **The ring will be shorted out.** Silver electrodes on both faces, and in
  this stack both faces contact metal that is all electrically common
  through the coupler and screw. It needs an insulator on one face and a
  pickoff on the other — see below.
- **0.3 mm ceramic at 30 mm diameter is fragile.** It cracks on any
  bending. Flat parallel shims either side, moderate torque, and buy
  spares.

### Getting the signal out — use a flex circuit, not bare foil

**What is actually at risk.** In-use flexing fatigue is *not* the main
worry. A correctly preloaded joint does not move: clamp force stays far
above the dynamic load, the interfaces never separate, and the pickoff is
trapped rather than cycling. Ringing hard does not work it back and forth.

The real hazards:

- **Assembly.** Torquing the screw drags rotating parts across whatever is
  underneath, shearing or wrinkling a loose foil. Happens on the bench, not
  in performance.
- **Sharp edges.** A foil exiting over a square pocket corner gets creased
  and eventually cut. This is the one that bites.
- **Fretting.** Micro-slip at contact edges under vibration is real in
  bolted joints and abrades thin foil over time.

**Use a single-sided polyimide FPC.** It collapses three parts into one:
the polyimide base *is* the insulator (replacing the Kapton), the copper
*is* the electrode (replacing the foil tab), and the tail *is* the wire
route. Copper supported by polyimide resists creasing far better than loose
foil, the outline can be drawn with a proper radius where it leaves the
joint, and the whole thing is ~50 um. Cheap in small quantities from
JLCPCB and similar.

Leave the copper exposed (no coverlay) only in the ring contact area.

**Keep the solder joint outside the clamped zone** — solder is brittle and
cracks under preload.

### Productization note — replace the handguard

The handguard is hardware, so it is fair game. A purpose-made replacement
with a recess for the ring and a moulded channel out to the rim solves the
thickness budget and the wire exit in one part, gives the flex a chamfered
exit instead of a sharp corner, and reopens position 3.

That is a real manufacturing step, but it is one cheap disc per bell and it
removes the problem rather than working around it.

**Sequencing:** position 2 with the catalogue 30x21x0.3 ring and an FPC
pickoff needs no custom parts and can be built now. Keep the replacement
handguard for productization, once the signal is known to be worth building
around.

**Weaknesses.** The crown is node-adjacent, so the spectral balance will be
skewed relative to what the bell actually sounds like. The washer plus its
clamp has its own mechanical resonance. Bolt torque will matter a lot and
is worth treating as a variable, not a fixed choice.

Still the cheapest option at choir scale, and the only one with a credible
route to lower unit cost via a custom ring sized to the handguard.

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
this is a cheap problem with a trivial, fully removable mount. If it
isn't, everything moves to an internal bracket and a non-contact sensor.

**Use a high bell, not a low one.** Low bells are the best case on both
counts — most energy at the crown, and the lowest frequencies to push
through a plastic-isolated joint. A C6 or C7 is the design case: its
partials sit at 4 kHz and 8 kHz and it has far less energy to spare. If it
works there it works everywhere. Tune the design around a G3 and the
failure surfaces too late.

Related caveat on the "I can feel it in the handle" evidence: vibrotactile
sensitivity in the hand peaks around 200-300 Hz and falls off steeply above
~1 kHz. Feeling a low bell confirms its *fundamental* crosses the joint. It
says nothing about the 4x partial or the strike transient — and plastic
damping does its worst exactly up there, above where the hand can report.

1. Piezo ring + charge amp (1 nF feedback, 100 Mohm bleed, FET-input),
   fitted at position 2. Fit one at position 1 as well and compare.
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

*Answered: crown hardware is now known — Malmark, per the maker's
construction drawing. Target platform is Malmark. Pod goes on the handle;
only a thin ring enters the stack.*

0. **Which clamped face carries the plastic — the yoke side, the handguard
   side, or both?** This decides whether position 1 or position 2 has the
   metal path, and it is the one thing that could flip the recommendation.
   Highest-value 30-second look, next time a bell is apart.
1. **How much spare thread does the main assembly screw have** in the
   coupler? Sets the real thickness budget for ring plus shims.
2. **What is the radial clearance under the handguard** at position 2, for
   the FPC tail to exit? The crown is domed and the handguard flat, so
   there should be a wedge opening outward — confirm it.
3. **Is there a bell available to experiment on freely?** The design needs
   repeated fitting and removal.
4. **How many bells get instrumented in v1?** Prototyping one bell permits
   choices that don't survive to 37 — and the range spans two construction
   variants, since C#8 and up use fixed metal clappers.

Note that **Experiment 1 waits on none of this**: a piezo disc squashed
under the stock handle with flying leads answers the crown-signal question
before any of these are settled.
