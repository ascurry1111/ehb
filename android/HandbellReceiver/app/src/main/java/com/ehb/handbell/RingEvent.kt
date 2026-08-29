package com.ehb.handbell

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Mirrors the packed `RingEvent` struct in feather_transmitter.ino:
 *   uint32 ringId, uint16 peakMilliG, uint32 timestampMs — little-endian, 10 bytes total.
 */
data class RingEvent(
    val ringId: Long,
    val peakMilliG: Int,
    val timestampMs: Long,
) {
    val peakG: Float get() = peakMilliG / 1000f

    companion object {
        const val WIRE_SIZE = 10

        /** Returns null if [bytes] isn't a valid RingEvent payload. */
        fun parse(bytes: ByteArray): RingEvent? {
            if (bytes.size < WIRE_SIZE) return null
            val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            val ringId = buf.int.toLong() and 0xFFFFFFFFL
            val peakMilliG = buf.short.toInt() and 0xFFFF
            val timestampMs = buf.int.toLong() and 0xFFFFFFFFL
            return RingEvent(ringId, peakMilliG, timestampMs)
        }
    }
}
