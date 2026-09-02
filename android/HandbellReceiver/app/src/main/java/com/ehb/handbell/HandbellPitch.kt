package com.ehb.handbell

/** One pitch: standard scientific pitch notation (e.g. "A5", "Bb4") plus its
 *  12-tone equal-temperament frequency (A4 = 440Hz). */
data class HandbellPitch(val name: String, val frequencyHz: Double)

/**
 * Standard handbell pitches, spanning the chromatic range of a typical
 * 5-octave handbell choir set: C3 to C8 (61 bells). Accidentals are spelled
 * as flats (Bb, not A#), matching handbell convention.
 */
object HandbellPitches {
    private val NOTE_NAMES = arrayOf("C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B")

    // MIDI note numbers: 60 = C4 (middle C), 69 = A4 = 440Hz. C3 and C8 bracket
    // the standard 5-octave set.
    private const val MIDI_C3 = 48
    private const val MIDI_C8 = 108

    val ALL: List<HandbellPitch> = (MIDI_C3..MIDI_C8).map { midi ->
        val name = NOTE_NAMES[midi % 12]
        val octave = midi / 12 - 1
        val frequencyHz = 440.0 * Math.pow(2.0, (midi - 69) / 12.0)
        HandbellPitch("$name$octave", frequencyHz)
    }

    /** A5 (880Hz) -- the pitch this app's tone was originally tuned to. */
    val DEFAULT: HandbellPitch = ALL.first { it.name == "A5" }

    fun byName(name: String): HandbellPitch? = ALL.firstOrNull { it.name == name }
}
