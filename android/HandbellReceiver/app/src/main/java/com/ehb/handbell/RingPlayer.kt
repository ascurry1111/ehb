package com.ehb.handbell

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
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
 * All tones are synthesized ONCE up front (one per DynamicLevel) and handed
 * to SoundPool, which is the low-latency-appropriate API for short,
 * frequently-retriggered sound effects on Android. No synthesis or file I/O
 * happens on the ring-event hot path — only soundPool.play().
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
        const val SAMPLE_RATE = 44100
        const val DURATION_S = 0.9
        const val BASE_FREQ_HZ = 880.0
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

    @Volatile private var soundIdByLevel: Map<DynamicLevel, Int> = emptyMap()
    @Volatile private var ready = false

    /** Synthesizes and loads one tone per DynamicLevel. Call once, off the main thread is
     *  fine (this constructor kicks off a background thread itself). */
    fun prepare() {
        Thread({
            val ids = DynamicLevel.entries.associateWith { level ->
                val samples = synthesizeBellTone(level.representativePeakG)
                val file = File.createTempFile("bell_tone_", ".wav", context.cacheDir)
                writeWavFile(file, samples, SAMPLE_RATE)
                soundPool.load(file.absolutePath, 1)
            }
            soundIdByLevel = ids
            ready = true
            Log.i(TAG, "Loaded ${ids.size} tone variants.")
        }, "RingPlayer-prepare").start()
    }

    /** Trigger playback for a ring with the given peak acceleration (in g). Cheap — safe to
     *  call directly from the BLE notification callback. */
    fun play(peakG: Float) {
        if (!ready) {
            Log.w(TAG, "play() called before tones finished loading — dropping.")
            return
        }
        val level = DynamicLevel.forPeakG(peakG)
        val soundId = soundIdByLevel[level] ?: return
        soundPool.play(soundId, level.volume, level.volume, /* priority = */ 1, /* loop = */ 0, /* rate = */ 1f)
    }

    fun release() {
        soundPool.release()
    }

    /** Same shape as bell_tone() in pc_ble_listener.py: a decaying sine + two harmonics. */
    private fun synthesizeBellTone(peakG: Double): ShortArray {
        val n = (SAMPLE_RATE * DURATION_S).toInt()
        val strength = min(1.0, max(0.15, (peakG - 1.0) / 5.0))
        val raw = DoubleArray(n)
        var maxAbs = 1e-9
        for (i in 0 until n) {
            val t = i.toDouble() / SAMPLE_RATE
            val envelope = exp(-t * (2.5 + 1.5 * (1 - strength)))
            var s = 1.00 * sin(2 * PI * BASE_FREQ_HZ * t) +
                0.35 * strength * sin(2 * PI * BASE_FREQ_HZ * 2.4 * t) +
                0.15 * strength * sin(2 * PI * BASE_FREQ_HZ * 4.1 * t)
            s *= envelope * strength
            raw[i] = s
            maxAbs = max(maxAbs, abs(s))
        }
        val out = ShortArray(n)
        for (i in 0 until n) {
            val normalized = (raw[i] / maxAbs) * 0.8
            out[i] = (normalized * 32767.0).toInt().coerceIn(-32768, 32767).toShort()
        }
        return out
    }

    private fun synthesizeBellTone(peakG: Float): ShortArray = synthesizeBellTone(peakG.toDouble())

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
