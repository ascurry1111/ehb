package com.ehb.concurrency

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.SystemClock
import android.widget.Button
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import java.util.Locale

/**
 * Concurrency Demo — v0.1, the beacon spike.
 *
 * This does one thing: broadcast a start beacon and report what happened. It
 * exists to answer the question that gates the whole design — how precisely
 * can an Android app control when an advertisement actually goes out? See
 * docs/concurrency-demo-design.md §10.
 *
 * The boards do the measuring: they scan for this beacon, compute T0, and
 * pulse a GPIO at T0, which the PPK2 captures on its digital channels against
 * a single shared timebase. What we need from the phone is simply a beacon
 * that is well-formed and repeated.
 *
 * This is the seed of the real app rather than a throwaway — the advertiser is
 * needed either way, so the demo app grows from here rather than starting over.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var statusView: TextView
    private lateinit var logView: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var sendButton: Button

    private var adapter: BluetoothAdapter? = null
    private var beacon: StartBeacon? = null
    private var nextRunId = 1

    private val requestAdvertisePermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) {
                log("BLUETOOTH_ADVERTISE granted")
                refreshStatus()
            } else {
                log("BLUETOOTH_ADVERTISE denied — cannot send beacons")
            }
        }

    private val beaconListener = object : StartBeacon.Listener {
        override fun onLog(message: String) = log(message)

        override fun onFinished(runId: Int, actualT0Elapsed: Long) {
            val overshoot = SystemClock.elapsedRealtime() - actualT0Elapsed
            log("Run $runId reached T0 (handler was ${overshoot}ms late)")
            sendButton.isEnabled = true
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        statusView = findViewById(R.id.status)
        logView = findViewById(R.id.log)
        logScroll = findViewById(R.id.logScroll)
        sendButton = findViewById(R.id.sendBeacon)

        adapter = (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter
        adapter?.let { beacon = StartBeacon(it) }

        sendButton.setOnClickListener { sendBeacon() }

        refreshStatus()
        if (!hasAdvertisePermission()) {
            requestAdvertisePermission.launch(Manifest.permission.BLUETOOTH_ADVERTISE)
        }
    }

    override fun onStop() {
        super.onStop()
        if (hasAdvertisePermission()) beacon?.stop()
    }

    /**
     * The gate. Peripheral/advertiser role is a separate capability from
     * scanning and is genuinely absent on some devices — if it is missing here,
     * the countdown-beacon design cannot work on this phone at all and the
     * fallback is to make one of the nine boards the conductor instead.
     */
    private fun refreshStatus() {
        val a = adapter
        val lines = buildString {
            if (a == null) {
                appendLine("No Bluetooth adapter")
            } else {
                appendLine("Bluetooth enabled: ${a.isEnabled}")
                appendLine("Advertising supported: ${a.isMultipleAdvertisementSupported}")
                appendLine("LE extended advertising: ${a.isLeExtendedAdvertisingSupported}")
            }
            append("Advertise permission: ${hasAdvertisePermission()}")
        }
        statusView.text = lines

        val ready = a != null && a.isEnabled &&
            a.isMultipleAdvertisementSupported && hasAdvertisePermission()
        sendButton.isEnabled = ready

        if (a != null && !a.isMultipleAdvertisementSupported) {
            log("This device does not support BLE advertising. The beacon design cannot run here.")
        }
    }

    private fun sendBeacon() {
        if (!hasAdvertisePermission()) {
            requestAdvertisePermission.launch(Manifest.permission.BLUETOOTH_ADVERTISE)
            return
        }
        val runId = nextRunId
        nextRunId = if (nextRunId >= 255) 1 else nextRunId + 1
        sendButton.isEnabled = false
        log("--- run $runId: 2000ms countdown ---")
        beacon?.start(runId, COUNTDOWN_MS, beaconListener)
    }

    private fun hasAdvertisePermission(): Boolean =
        ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_ADVERTISE) ==
            PackageManager.PERMISSION_GRANTED

    private fun log(message: String) {
        val stamp = String.format(Locale.US, "%.3f", SystemClock.elapsedRealtime() / 1000.0)
        logView.append("[$stamp] $message\n")
        logScroll.post { logScroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    private companion object {
        /**
         * ~20 beacons at the platform's 100ms advertising floor. Enough
         * redundancy that every board should catch several, which is what lets
         * them take a minimum rather than trusting one sample.
         */
        const val COUNTDOWN_MS = 2000
    }
}
