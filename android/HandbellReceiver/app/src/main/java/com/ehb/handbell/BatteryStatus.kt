package com.ehb.handbell

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Must match BatteryState in feather_transmitter.ino. */
enum class BatteryState(val wireValue: Int) {
    UNKNOWN(0),      // not enough trend history yet (first ~60s after boot/connect)
    DISCHARGING(1),  // on battery, voltage flat or falling
    CHARGING(2),     // voltage rising -- actively being charged over USB
    NO_BATTERY(3);   // no LiPo connected; running on USB power alone

    companion object {
        fun fromWire(value: Int): BatteryState = entries.firstOrNull { it.wireValue == value } ?: UNKNOWN
    }
}

/**
 * Mirrors the packed `BatteryStatus` struct in feather_transmitter.ino:
 *   uint8 percent, uint8 state, uint16 milliVolts, uint16 estimatedMinutesRemaining
 *   -- little-endian, 6 bytes total.
 *
 * See the firmware's header comment for why this is voltage-inferred rather
 * than measured: the Feather V2 has no fuel-gauge chip.
 */
data class BatteryStatus(
    val percent: Int,
    val state: BatteryState,
    val milliVolts: Int,
    val estimatedMinutesRemaining: Int,
) {
    companion object {
        const val WIRE_SIZE = 6

        fun parse(bytes: ByteArray): BatteryStatus? {
            if (bytes.size < WIRE_SIZE) return null
            val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            val percent = buf.get().toInt() and 0xFF
            val state = BatteryState.fromWire(buf.get().toInt() and 0xFF)
            val milliVolts = buf.short.toInt() and 0xFFFF
            val estimatedMinutesRemaining = buf.short.toInt() and 0xFFFF
            return BatteryStatus(percent, state, milliVolts, estimatedMinutesRemaining)
        }
    }
}
