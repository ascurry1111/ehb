package com.ehb.handbell

/**
 * The six standard musical dynamic markings (pp, p, mp, mf, f, ff), mapped
 * onto the bell's realistic peak-acceleration range -- 1.6g (soft ring) to
 * 5.0g (hard ring), per observed ring-log data -- split into six equal-width
 * bands: (5.0 - 1.6) / 6 ~= 0.5667g per band.
 *
 * This is the single source of truth for the mapping: RingPlayer uses it for
 * both tone shape (brightness/decay) and playback volume, and the UI uses it
 * to display the detected dynamic, so the two can't drift out of sync.
 */
enum class DynamicLevel(
    val label: String,        // musical abbreviation, shown in the UI
    val fullName: String,     // spelled out, for reference
    val upperBoundG: Float,   // this level applies to peakG below this (exclusive)
    val representativePeakG: Float, // band midpoint; feeds the tone-shape synthesis
    val volume: Float,        // SoundPool playback volume, 0f-1f -- see note below
) {
    PIANISSIMO(  "pp", "Pianissimo",   2.1667f,          1.8833f, 0.200f),
    PIANO(       "p",  "Piano",        2.7333f,          2.4500f, 0.276f),
    MEZZO_PIANO( "mp", "Mezzo-piano",  3.3000f,          3.0167f, 0.381f),
    MEZZO_FORTE( "mf", "Mezzo-forte",  3.8667f,          3.5833f, 0.525f),
    FORTE(       "f",  "Forte",        4.4333f,          4.1500f, 0.725f),
    FORTISSIMO(  "ff", "Fortissimo",   Float.MAX_VALUE,  4.7167f, 1.000f);

    // Volumes above are spaced geometrically (equal ratio between consecutive
    // levels, 0.20 -> 1.00, ratio 5^(1/5) ~= 1.3797) rather than evenly, since
    // perceived loudness is roughly logarithmic -- equal linear steps would
    // sound bunched up at the loud end. That's ~14dB of range pp-to-ff.
    //
    // The floor was raised from 0.08 to 0.20 because pp was simply too quiet
    // to hear comfortably in practice. That's a narrower spread than a real
    // acoustic bell's, but a phone speaker isn't a bell casting: the quiet end
    // has to stay above the room, not just above silence.

    companion object {
        fun forPeakG(peakG: Float): DynamicLevel = entries.first { peakG < it.upperBoundG }
    }
}
