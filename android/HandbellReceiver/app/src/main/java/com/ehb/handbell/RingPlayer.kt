package com.ehb.handbell

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * Plays a synthesized bell tone on each ring, with velocity sensitivity —
 * harder swings get a brighter/longer tone AND are audibly louder — mapped
 * onto the six musical dynamic levels in DynamicLevel.kt.
 *
 * A real handbell keeps ringing after the strike until it naturally damps out
 * or the ringer stops it against their body, so each tone is a multi-second
 * natural decay rather than a short fixed blip -- see DURATION_S and the
 * decay-rate constants below. damp() cuts it short on demand, driven either
 * by the firmware's damp gesture (see BLE_CHAR_DAMP_UUID in
 * feather_transmitter.ino) or by the manual Damp button in the app.
 *
 * Tones are synthesized at a chosen HandbellPitch (default A5, 880Hz -- what
 * this tone was originally tuned to) and handed to SoundPool, which is the
 * low-latency-appropriate API for short, frequently-retriggered sound
 * effects on Android. No synthesis or file I/O happens on the ring-event hot
 * path — only soundPool.play(). setPitch() re-synthesizes all six dynamic
 * levels at a new frequency on a background thread; the OLD tones stay
 * playable until the new ones are ready, so a ring mid-change never drops --
 * it just plays once more at the previous pitch, then switches.
 *
 * NOTE on loudness: the synthesized buffer itself is normalized to the same
 * peak amplitude for every level (see synthesizeBellTone) — that's a
 * deliberate choice to use the full 16-bit range for audio quality at every
 * level, NOT the mechanism for volume differences. Loudness comes entirely
 * from DynamicLevel.volume, passed to SoundPool.play() below. (An earlier
 * version scaled the waveform by strength before that per-buffer
 * normalization, which just renormalized it straight back out — the buckets
 * had different timbre but were all played back at the same volume, which is
 * why they were hard to tell apart.)
 */
class RingPlayer(private val context: Context) {

    private companion object {
        const val TAG = "RingPlayer"
        // Lower than CD quality on purpose: the tone's harmonic content tops out
        // around 4kHz (see synthesizeBellTone), so 22050Hz is well above Nyquist
        // for it, and halving the sample rate roughly halves both the synthesis
        // time and cache file size -- which matters more now that buffers run
        // several seconds instead of under one.
        const val SAMPLE_RATE = 22050

        // Buffer length. Generous relative to how long the decay actually takes
        // (see BASE_DECAY_RATE below) so every level reaches near-silence well
        // before the buffer ends -- avoids any click from an abrupt cutoff at a
        // non-zero sample, on top of the explicit fade-to-zero tail below.
        const val DURATION_S = 9.0

        // Decay rate (per second) of the amplitude envelope: exp(-rate * t).
        // Louder dynamics decay more slowly (ring out longer), same as a real
        // bell has more energy to dissipate from a harder strike.
        //   rate = BASE_DECAY_RATE + DECAY_RATE_SPREAD * (1 - strength)
        // With this bell's actual strength range (~0.18 pp to ~0.74 ff, see
        // synthesizeBellTone), that puts the "effectively silent" point
        // (-40dB, envelope 0.01) at roughly 3.4s for pp and 5.5s for ff.
        const val BASE_DECAY_RATE = 0.6
        const val DECAY_RATE_SPREAD = 0.9

        // Safety-net linear fade over the last stretch of every buffer, so the
        // sample value is exactly zero at the end regardless of how the decay
        // math above worked out -- belt and suspenders against any click.
        const val TAIL_FADE_S = 0.05

        // Damp fade: quick enough to feel like an immediate stop (matching a
        // real handbell being pressed to the body), but long enough that
        // stopping mid-waveform doesn't produce an audible click.
        const val DAMP_FADE_STEPS = 5
        const val DAMP_FADE_STEP_MS = 8L
    }

    private val soundPool = SoundPool.Builder()
        .setMaxStreams(4)
        .setAudioAttributes(
            AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .setFlags(AudioAttributes.FLAG_LOW_LATENCY)
                .build()
        )
        .build()

    private val fadeHandler = Handler(Looper.getMainLooper())

    @Volatile private var soundIdByLevel: Map<DynamicLevel, Int> = emptyMap()
    @Volatile private var ready = false

    // Guards against a rapid string of pitch changes racing each other: only
    // the synthesis pass that's still current when it finishes gets applied.
    private val pitchGeneration = AtomicInteger(0)

    // The bell is physically monophonic -- only one casting, one vibration
    // state -- so a new ring replaces whatever's currently sounding rather
    // than layering another independent voice on top of it.
    @Volatile private var activeStreamId: Int? = null
    @Volatile private var activeVolume: Float = 0f

    /** Synthesizes and loads one tone per DynamicLevel at the given pitch. Call once,
     *  off the main thread is fine (this kicks off a background thread itself). */
    fun prepare(pitch: HandbellPitch = HandbellPitches.DEFAULT) {
        synthesizeAllAsync(pitch.frequencyHz)
    }

    /** Re-synthesizes all six tones at a new pitch, on a background thread. Safe to call
     *  repeatedly in quick succession -- only the last call's result gets applied. */
    fun setPitch(pitch: HandbellPitch) {
        synthesizeAllAsync(pitch.frequencyHz)
    }

    private fun synthesizeAllAsync(baseFreqHz: Double) {
        val generation = pitchGeneration.incrementAndGet()
        Thread({
            val ids = DynamicLevel.entries.associateWith { level ->
                val samples = synthesizeBellTone(level.representativePeakG.toDouble(), baseFreqHz)
                val file = File.createTempFile("bell_tone_", ".wav", context.cacheDir)
                writeWavFile(file, samples, SAMPLE_RATE)
                soundPool.load(file.absolutePath, 1)
            }
            if (generation == pitchGeneration.get()) {
                // Still current. Swap in the new tones and drop the old ones --
                // but only now, so play() always has a usable set.
                val old = soundIdByLevel
                soundIdByLevel = ids
                ready = true
                old.values.forEach { soundPool.unload(it) }
                Log.i(TAG, "Loaded ${ids.size} tone variants at ${baseFreqHz}Hz.")
            } else {
                // Superseded by a newer pitch change before this one finished --
                // discard rather than swap in stale tones.
                ids.values.forEach { soundPool.unload(it) }
            }
        }, "RingPlayer-prepare").start()
    }

    /** Trigger playback for a ring with the given peak acceleration (in g). Cheap — safe to
     *  call directly from the BLE notification callback. */
    fun play(peakG: Float) {
        if (!ready) {
            Log.w(TAG, "play() called before tones finished loading — dropping.")
            return
        }
        // A new strike replaces the currently sounding tone -- see the
        // monophonic note on activeStreamId above. Stop unconditionally
        // (no fade): the new tone's own attack transient masks any click,
        // and this is the ring-event hot path, so keep it to one cheap call.
        activeStreamId?.let { soundPool.stop(it) }
        fadeHandler.removeCallbacksAndMessages(null) // cancel any in-flight damp fade

        val level = DynamicLevel.forPeakG(peakG)
        val soundId = soundIdByLevel[level] ?: return
        activeStreamId = soundPool.play(soundId, level.volume, level.volume, /* priority = */ 1, /* loop = */ 0, /* rate = */ 1f)
        activeVolume = level.volume
    }

    /** Stop whatever's currently ringing, quickly but without a click -- the software
     *  equivalent of touching the casting. Driven by the firmware's damp gesture or
     *  the app's manual Damp button. Safe to call when nothing is playing (no-op). */
    fun damp() {
        // Deliberately leaves activeStreamId set until the fade actually finishes
        // (rather than nulling it here) -- so if play() is called again mid-fade,
        // it still finds this stream and hard-stops it, instead of the pending
        // fade steps below getting cancelled and leaking a stuck, quiet-but-not-
        // silent stream.
        val streamId = activeStreamId ?: return
        val startVolume = activeVolume
        fadeHandler.removeCallbacksAndMessages(null)
        for (step in 1..DAMP_FADE_STEPS) {
            fadeHandler.postDelayed({
                val v = startVolume * (1f - step.toFloat() / DAMP_FADE_STEPS)
                soundPool.setVolume(streamId, v, v)
                if (step == DAMP_FADE_STEPS) {
                    soundPool.stop(streamId)
                    if (activeStreamId == streamId) activeStreamId = null
                }
            }, step * DAMP_FADE_STEP_MS)
        }
    }

    fun release() {
        fadeHandler.removeCallbacksAndMessages(null)
        soundPool.release()
    }

    /** Same shape as bell_tone() in pc_ble_listener.py: a decaying sine + two harmonics,
     *  extended with a much longer natural decay -- see the constants above. */
    private fun synthesizeBellTone(peakG: Double, baseFreqHz: Double): ShortArray {
        val n = (SAMPLE_RATE * DURATION_S).toInt()
        val strength = min(1.0, max(0.15, (peakG - 1.0) / 5.0))
        val decayRate = BASE_DECAY_RATE + DECAY_RATE_SPREAD * (1 - strength)
        val raw = DoubleArray(n)
        var maxAbs = 1e-9
        for (i in 0 until n) {
            val t = i.toDouble() / SAMPLE_RATE
            val envelope = exp(-t * decayRate)
            var s = 1.00 * sin(2 * PI * baseFreqHz * t) +
                0.35 * strength * sin(2 * PI * baseFreqHz * 2.4 * t) +
                0.15 * strength * sin(2 * PI * baseFreqHz * 4.1 * t)
            s *= envelope * strength
            raw[i] = s
            maxAbs = max(maxAbs, abs(s))
        }

        // Explicit fade-to-zero over the tail, on top of the natural exponential
        // decay -- guarantees no click at the buffer's end regardless of how
        // quiet the decay math actually got by then.
        val tailSamples = (SAMPLE_RATE * TAIL_FADE_S).toInt().coerceAtMost(n)
        for (i in 0 until tailSamples) {
            val fade = i.toDouble() / tailSamples
            raw[n - tailSamples + i] *= fade
        }

        val out = ShortArray(n)
        for (i in 0 until n) {
            val normalized = (raw[i] / maxAbs) * 0.8
            out[i] = (normalized * 32767.0).toInt().coerceIn(-32768, 32767).toShort()
        }
        return out
    }

    /** Minimal 16-bit mono PCM WAV writer — no external deps needed. */
    private fun writeWavFile(file: File, samples: ShortArray, sampleRate: Int) {
        val dataSize = samples.size * 2
        val byteRate = sampleRate * 2
        RandomAccessFile(file, "rw").use { raf ->
            raf.setLength(0)
            fun writeIntLE(v: Int) {
                raf.write(v and 0xFF); raf.write((v shr 8) and 0xFF)
                raf.write((v shr 16) and 0xFF); raf.write((v shr 24) and 0xFF)
            }
            fun writeShortLE(v: Int) {
                raf.write(v and 0xFF); raf.write((v shr 8) and 0xFF)
            }
            raf.writeBytes("RIFF")
            writeIntLE(36 + dataSize)
            raf.writeBytes("WAVE")
            raf.writeBytes("fmt ")
            writeIntLE(16)             // PCM fmt chunk size
            writeShortLE(1)            // PCM format
            writeShortLE(1)            // mono
            writeIntLE(sampleRate)
            writeIntLE(byteRate)
            writeShortLE(2)            // block align (channels * bitsPerSample/8)
            writeShortLE(16)           // bits per sample
            raf.writeBytes("data")
            writeIntLE(dataSize)
            for (s in samples) writeShortLE(s.toInt() and 0xFFFF)
        }
    }
}
