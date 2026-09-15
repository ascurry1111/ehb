# Concurrency Demo — Android app

Receiver and controller for the nine-board concurrency demo. See
[`../../docs/concurrency-demo-design.md`](../../docs/concurrency-demo-design.md)
for the architecture and why it looks like this.

This is a **new app, not an extension of `HandbellReceiver`** — that one was
built to show ring count, latency, force and volume for a single connected
bell. This one scans rather than connects, handles nine boards, uploads
programs, and will render soundfont audio on a scheduled timeline.

## Status: v0.1 — the beacon spike

Right now the app does exactly one thing: broadcast a **start beacon** and
report what happened.

That is deliberate. The whole design rests on an assumption we have not
tested — that an Android app can control *when* an advertisement actually
goes out, closely enough that nine boards land on the same T0 within a few
milliseconds. If that assumption fails, the architecture changes (one of the
nine boards becomes the conductor instead), so it gets tested before anything
is built on top of it.

The app is the seed of the real thing rather than a throwaway: the advertiser
is needed either way.

**What it shows:**

- `isMultipleAdvertisementSupported` — the gate. Advertiser/peripheral role is
  a separate capability from scanning and is genuinely absent on some devices.
  If this is false, the countdown-beacon design cannot work on this phone.
- A button that broadcasts a 2000ms countdown, and a log of what the
  advertising stack reported.

**The measurement happens on the boards, not here** — they scan for the
beacon, compute T0, and pulse a GPIO, which the PPK2 captures on its digital
channels against one shared timebase. See `firmware/power-test.md` for the
PPK2 setup and `firmware/xiao_c3_node/xiao_c3_node.ino` for the listener.

## Building

**Use a JDK between 17 and 21. Not Android Studio's bundled one.**

Android Studio currently ships JBR **25**, which AGP 8.13.2 does not support —
it fails with a cryptic error whose entire message is the string `25.0.2`.
There is a usable JDK 21 at `~/.jdks/jbr-21.0.11`:

```bash
JAVA_HOME=~/.jdks/jbr-21.0.11 ./gradlew assembleDebug
```

In Android Studio, set the same under Settings → Build, Execution, Deployment
→ Build Tools → Gradle → Gradle JDK.

Unlike `HandbellReceiver`, this project includes the Gradle wrapper, so it
builds from the command line without Android Studio.

Toolchain matches `HandbellReceiver` deliberately: AGP 8.13.2, Kotlin 1.9.24,
Gradle 8.13, compileSdk 34, minSdk 31. minSdk 31 skips the legacy
pre-Android-12 Bluetooth permission model entirely.

## Running the spike

1. Flash `firmware/xiao_c3_node` to three or four boards.
2. Wire each board's D10 to a PPK2 digital input channel, commoning grounds.
3. Start a PPK2 capture.
4. Open this app, confirm advertising is supported, tap **Send start beacon**.
5. Every board pulses its LED at its own computed T0. The spread between those
   rising edges on the PPK2 trace is the number we are after.

Boards print `[BEACON] run N fired at T0, M beacons heard` over serial, which
is worth watching on at least one board to confirm it is hearing the whole
countdown rather than a single repeat.
