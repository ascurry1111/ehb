package com.ehb.concurrency

/**
 * Wire format for the BLE advertisements exchanged with the ESP32C3 boards.
 *
 * This file is the contract with `firmware/xiao_c3_node/xiao_c3_node.ino`.
 * Any change here needs the matching change there, or boards will silently
 * ignore beacons.
 *
 * See docs/concurrency-demo-design.md for the reasoning. The short version:
 *
 *  - Everything is carried as BLE *manufacturer-specific data* under company
 *    ID 0xFFFF, which is the ID reserved for testing and development. We are
 *    not a registered Bluetooth SIG member and this never ships, so 0xFFFF is
 *    the correct choice rather than borrowing somebody else's.
 *
 *  - Because 0xFFFF is reserved for everybody's dev work, a demo room may well
 *    contain other devices using it. MAGIC guards against acting on a stranger's
 *    packet.
 *
 *  - The start beacon carries a COUNTDOWN, not a timestamp. Repeat #47 says
 *    "T0 in 2000ms", #48 says "1900ms". Boards that catch different repeats
 *    still land on the same absolute instant, so catching any single beacon is
 *    sufficient and catching a late one is no worse than catching an early one.
 *    A timestamp would require a shared clock we deliberately do not have.
 */
object BeaconProtocol {

    /** Bluetooth SIG company ID reserved for testing/development. */
    const val COMPANY_ID = 0xFFFF

    /** "EH" — Electric Handbell. Guards against other 0xFFFF users. */
    const val MAGIC_0 = 0x45.toByte()
    const val MAGIC_1 = 0x48.toByte()

    /** Message types. Only the start beacon exists so far. */
    const val MSG_START_BEACON = 0x01.toByte()
    // 0x02 = ready advertisement (board -> phone), not yet implemented
    // 0x03 = event advertisement (board -> phone), not yet implemented

    /**
     * Builds a start beacon payload.
     *
     *   byte 0..1  magic "EH"
     *   byte 2     message type
     *   byte 3     run ID
     *   byte 4..5  milliseconds remaining until T0, uint16 little-endian
     *
     * Little-endian to match the ESP32's native byte order, so the firmware can
     * read the field directly rather than swapping.
     *
     * @param runId disambiguates one run from the next, so a board replaying a
     *   song can tell a fresh beacon from a stale one still in the air.
     * @param msRemaining time until T0. uint16 caps this at ~65s, far beyond
     *   the ~2s countdown we actually use.
     */
    fun buildStartBeacon(runId: Int, msRemaining: Int): ByteArray {
        require(runId in 0..255) { "runId must fit in a byte, got $runId" }
        require(msRemaining in 0..65535) { "msRemaining must fit in uint16, got $msRemaining" }
        return byteArrayOf(
            MAGIC_0,
            MAGIC_1,
            MSG_START_BEACON,
            runId.toByte(),
            (msRemaining and 0xFF).toByte(),
            ((msRemaining shr 8) and 0xFF).toByte(),
        )
    }
}
