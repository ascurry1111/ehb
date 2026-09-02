package com.ehb.handbell

/**
 * Procedural synthesis voices, all sharing the natural-decay-after-a-strike
 * shape the bell already has -- no sustained/blown voices (organ, trumpet,
 * clarinet) by design, since those don't fit the "strike and decay" physical
 * model this whole app is built around.
 *
 * These are synthesizer approximations, not sampled recordings -- see the
 * per-instrument generators in RingPlayer.kt for what each one actually is
 * and how convincing (or not) it's likely to sound.
 */
enum class Instrument(val label: String) {
    BELL("Bell"),
    PIANO("Piano"),
    GUITAR("Guitar"),
    ELECTRIC_GUITAR("Electric Guitar"),
}
