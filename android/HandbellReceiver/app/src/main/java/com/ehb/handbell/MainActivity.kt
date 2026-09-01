package com.ehb.handbell

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.os.Bundle
import android.os.SystemClock
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private lateinit var tvStatus: TextView
    private lateinit var tvRingCount: TextView
    private lateinit var tvLastPeak: TextView
    private lateinit var tvLatency: TextView
    private lateinit var tvBattery: TextView
    private lateinit var rootView: android.view.View

    private lateinit var ringPlayer: RingPlayer
    private var bleClient: BleRingClient? = null
    private var ringCount = 0

    private val requestPermissions =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { results ->
            if (results.values.all { it }) {
                ensureBluetoothEnabledThenStart()
            } else {
                tvStatus.text = "Bluetooth permissions are required to receive rings."
            }
        }

    private val requestEnableBluetooth =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            startBleClient() // proceed regardless; start() itself reports "Bluetooth is off" if declined
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        rootView = findViewById(R.id.root)
        tvStatus = findViewById(R.id.tvStatus)
        tvRingCount = findViewById(R.id.tvRingCount)
        tvLastPeak = findViewById(R.id.tvLastPeak)
        tvLatency = findViewById(R.id.tvLatency)
        tvBattery = findViewById(R.id.tvBattery)

        ringPlayer = RingPlayer(this)
        ringPlayer.prepare()

        requestPermissions.launch(
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        )
    }

    @SuppressLint("MissingPermission")
    private fun ensureBluetoothEnabledThenStart() {
        val adapter = (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter != null && !adapter.isEnabled) {
            requestEnableBluetooth.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
        } else {
            startBleClient()
        }
    }

    private fun startBleClient() {
        if (bleClient != null) return
        bleClient = BleRingClient(
            context = this,
            onStatus = { status -> runOnUiThread { tvStatus.text = status } },
            onBattery = { status -> runOnUiThread { onBattery(status) } },
            onRing = { event, receivedAtElapsedMs, estimatedDetectionAtElapsedMs ->
                runOnUiThread { onRing(event, receivedAtElapsedMs, estimatedDetectionAtElapsedMs) }
            },
        ).also { it.start() }
    }

    private fun onRing(event: RingEvent, receivedAtElapsedMs: Long, estimatedDetectionAtElapsedMs: Long?) {
        ringCount++
        tvRingCount.text = ringCount.toString()
        tvLastPeak.text = String.format(Locale.US, "Last peak: %.2fg", event.peakG)
        flashBackground()

        // Play immediately — this is the latency-critical path, no extra work before it.
        ringPlayer.play(event.peakG)
        val playedAtElapsedMs = SystemClock.elapsedRealtime()

        tvLatency.text = if (estimatedDetectionAtElapsedMs == null) {
            "Latency: syncing clock…"
        } else {
            val totalMs = playedAtElapsedMs - estimatedDetectionAtElapsedMs
            val bleMs = receivedAtElapsedMs - estimatedDetectionAtElapsedMs
            val appMs = playedAtElapsedMs - receivedAtElapsedMs
            "Latency: ${totalMs}ms  (ring→phone ${bleMs}ms + phone→sound ${appMs}ms)"
        }
    }

    private fun onBattery(status: BatteryStatus) {
        tvBattery.text = when (status.state) {
            BatteryState.NO_BATTERY -> "⚡ Running on USB (no battery)"
            BatteryState.CHARGING -> "🔌 Charging — ${status.percent}%"
            BatteryState.DISCHARGING -> "🔋 ${status.percent}% • ${formatMinutes(status.estimatedMinutesRemaining)} left"
            BatteryState.UNKNOWN -> "🔋 ${status.percent}% • reading…"
        }
    }

    private fun formatMinutes(totalMinutes: Int): String {
        val hours = totalMinutes / 60
        val minutes = totalMinutes % 60
        return if (hours > 0) "${hours}h ${minutes}m" else "${minutes}m"
    }

    private fun flashBackground() {
        rootView.setBackgroundColor(0xFF3E2723.toInt())
        rootView.postDelayed({ rootView.setBackgroundColor(0xFF101010.toInt()) }, 150)
    }

    override fun onDestroy() {
        super.onDestroy()
        bleClient?.stop()
        ringPlayer.release()
    }
}
