package com.ehb.concurrency

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertisingSet
import android.bluetooth.le.AdvertisingSetCallback
import android.bluetooth.le.AdvertisingSetParameters
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.annotation.RequiresPermission

/**
 * Broadcasts the start beacon: a countdown to T0, repeated for a couple of
 * seconds so that every board hears at least one copy.
 *
 * WHY AN ADVERTISING *SET* RATHER THAN plain startAdvertising():
 * the legacy `startAdvertising` API has no way to change the payload — you must
 * stop and restart to alter a single byte, which for a countdown means tearing
 * the advertiser down and rebuilding it ~20 times in two seconds. The
 * `AdvertisingSet` returned by `startAdvertisingSet` exposes
 * `setAdvertisingData`, which updates the payload in place.
 *
 * WHY elapsedRealtime() AND NOT currentTimeMillis():
 * T0 is a deadline, and a wall clock can jump (NTP correction, user edit,
 * timezone). `elapsedRealtime` is monotonic since boot, which is what a
 * deadline needs.
 *
 * WHAT THIS DELIBERATELY CANNOT DO:
 * we can stamp "ms remaining" at the moment we call `setAdvertisingData`, but
 * Android gives us no way to know when the radio actually emits the packet.
 * The gap between those two moments is unmeasured, and it is exactly what the
 * beacon spike exists to quantify — see docs/concurrency-demo-design.md §10.
 * Boards compensate by taking the *minimum* implied T0 across many beacons,
 * since every error source here can only ever make T0 look later than it is,
 * never earlier.
 */
class StartBeacon(private val adapter: BluetoothAdapter) {

    /** Progress and failure reports, for the UI log. */
    interface Listener {
        fun onLog(message: String)
        fun onFinished(runId: Int, actualT0Elapsed: Long)
    }

    private val handler = Handler(Looper.getMainLooper())
    private var advertisingSet: AdvertisingSet? = null
    private var listener: Listener? = null

    private var runId: Int = 0
    private var t0Elapsed: Long = 0L
    private var updatesSent = 0
    private var updatesFailed = 0
    private var running = false

    /**
     * How often we refresh the countdown payload. There is no point going much
     * below the advertising interval itself (100ms, the floor the platform
     * exposes for legacy advertising) — updates faster than the radio emits
     * would just be overwritten unseen.
     */
    private val updateIntervalMs = 100L

    private val callback = object : AdvertisingSetCallback() {
        override fun onAdvertisingSetStarted(set: AdvertisingSet?, txPower: Int, status: Int) {
            if (status != ADVERTISE_SUCCESS) {
                listener?.onLog("Advertising failed to start: ${describeStatus(status)}")
                running = false
                return
            }
            advertisingSet = set
            listener?.onLog("Advertising started at ${txPower}dBm, run $runId")
        }

        override fun onAdvertisingDataSet(set: AdvertisingSet?, status: Int) {
            if (status == ADVERTISE_SUCCESS) {
                updatesSent++
            } else {
                updatesFailed++
                // Don't log every failure — a burst would flood the UI. The
                // summary at the end reports the count.
            }
        }

        override fun onAdvertisingSetStopped(set: AdvertisingSet?) {
            listener?.onLog("Advertising stopped")
            advertisingSet = null
        }
    }

    /**
     * Starts the countdown. Advertises until T0, then stops.
     *
     * @param runId identifies this run; boards tag their events with it.
     * @param countdownMs how far ahead T0 should be. ~2000ms gives roughly 20
     *   beacons at a 100ms interval, which is ample redundancy for nine boards.
     */
    @RequiresPermission(Manifest.permission.BLUETOOTH_ADVERTISE)
    fun start(runId: Int, countdownMs: Int, listener: Listener) {
        if (running) {
            listener.onLog("Already running — ignoring")
            return
        }
        val advertiser = adapter.bluetoothLeAdvertiser
        if (advertiser == null) {
            listener.onLog("No BLE advertiser available (is Bluetooth on?)")
            return
        }

        this.listener = listener
        this.runId = runId
        this.updatesSent = 0
        this.updatesFailed = 0
        this.running = true
        this.t0Elapsed = SystemClock.elapsedRealtime() + countdownMs

        val parameters = AdvertisingSetParameters.Builder()
            // Legacy mode: this is what an ESP32 scanner picks up without
            // opting into extended advertising. Compatibility beats payload
            // size here — the beacon is 6 bytes.
            .setLegacyMode(true)
            .setConnectable(false)
            .setScannable(false)
            // Fastest interval the platform exposes (100ms). More beacons in
            // the countdown window means more chances for every board to catch
            // one, and a tighter minimum for boards that catch several.
            .setInterval(AdvertisingSetParameters.INTERVAL_MIN)
            .setTxPowerLevel(AdvertisingSetParameters.TX_POWER_HIGH)
            .build()

        advertiser.startAdvertisingSet(parameters, buildData(countdownMs), null, null, null, callback)
        scheduleNextUpdate()
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_ADVERTISE)
    fun stop() {
        if (!running) return
        running = false
        handler.removeCallbacksAndMessages(null)
        adapter.bluetoothLeAdvertiser?.stopAdvertisingSet(callback)
    }

    private fun buildData(msRemaining: Int): AdvertiseData =
        AdvertiseData.Builder()
            // No device name: it would eat most of the 31-byte legacy payload
            // and nothing needs it. Boards match on the magic bytes.
            .setIncludeDeviceName(false)
            .setIncludeTxPowerLevel(false)
            .addManufacturerData(
                BeaconProtocol.COMPANY_ID,
                BeaconProtocol.buildStartBeacon(runId, msRemaining),
            )
            .build()

    private fun scheduleNextUpdate() {
        handler.postDelayed({ tick() }, updateIntervalMs)
    }

    @Suppress("MissingPermission") // start() is annotated; we cannot reach here without it
    private fun tick() {
        if (!running) return

        val remaining = (t0Elapsed - SystemClock.elapsedRealtime()).toInt()
        if (remaining <= 0) {
            val t0 = t0Elapsed
            val sent = updatesSent
            val failed = updatesFailed
            stop()
            listener?.onLog("Countdown complete: $sent payload updates, $failed failed")
            listener?.onFinished(runId, t0)
            return
        }

        advertisingSet?.setAdvertisingData(buildData(remaining))
        scheduleNextUpdate()
    }

    private fun describeStatus(status: Int): String = when (status) {
        AdvertisingSetCallback.ADVERTISE_FAILED_ALREADY_STARTED -> "already started"
        AdvertisingSetCallback.ADVERTISE_FAILED_DATA_TOO_LARGE -> "data too large"
        AdvertisingSetCallback.ADVERTISE_FAILED_FEATURE_UNSUPPORTED -> "feature unsupported"
        AdvertisingSetCallback.ADVERTISE_FAILED_INTERNAL_ERROR -> "internal error"
        AdvertisingSetCallback.ADVERTISE_FAILED_TOO_MANY_ADVERTISERS -> "too many advertisers"
        else -> "status $status"
    }
}
