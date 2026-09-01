package com.ehb.handbell

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.os.Bundle
import android.os.SystemClock
import android.widget.ArrayAdapter
import android.widget.ListView
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
    private lateinit var tvClearLog: TextView
    private lateinit var lvRingLog: ListView
    private lateinit var rootView: android.view.View

    private lateinit var ringPlayer: RingPlayer
    private lateinit var ringLogAdapter: ArrayAdapter<String>
    private var bleClient: BleRingClient? = null
    private var ringCount = 0

    /** Raw per-ring data, newest first -- kept separately from the adapter's
     *  display strings so toggling detail level can re-render history. */
    private data class RingRecord(
        val ringId: Long,
        val peakG: Float,
        val dynamicLevel: DynamicLevel,
        val totalMs: Long?,
        val bleMs: Long?,
        val appMs: Long?,
    )

    private val ringHistory = ArrayDeque<RingRecord>()
    private var lastRing: RingRecord? = null

    // Tap tvLatency to flip this -- deliberately no visible toggle control.
    private var showLatencyDetails = false

    private companion object {
        const val MAX_LOG_ENTRIES = 200
    }

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
        tvClearLog = findViewById(R.id.tvClearLog)
        lvRingLog = findViewById(R.id.lvRingLog)

        ringLogAdapter = ArrayAdapter(this, R.layout.item_ring_log, R.id.tvRingLogItem, mutableListOf<String>())
        lvRingLog.adapter = ringLogAdapter

        tvLatency.setOnClickListener {
            showLatencyDetails = !showLatencyDetails
            lastRing?.let { tvLatency.text = "Latency: ${formatLatency(it, showLatencyDetails)}" }
            refreshLogDisplay()
        }

        tvClearLog.setOnClickListener {
            ringHistory.clear()
            ringLogAdapter.clear()
        }

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
            onMute = { runOnUiThread { ringPlayer.mute() } },
        ).also { it.start() }
    }

    private fun onRing(event: RingEvent, receivedAtElapsedMs: Long, estimatedDetectionAtElapsedMs: Long?) {
        ringCount++
        val dynamicLevel = DynamicLevel.forPeakG(event.peakG)
        tvRingCount.text = ringCount.toString()
        tvLastPeak.text = String.format(Locale.US, "Last peak: %.2fg (%s)", event.peakG, dynamicLevel.label)
        flashBackground()

        // Play immediately — this is the latency-critical path, no extra work before it.
        ringPlayer.play(event.peakG)
        val playedAtElapsedMs = SystemClock.elapsedRealtime()

        val record = if (estimatedDetectionAtElapsedMs == null) {
            RingRecord(event.ringId, event.peakG, dynamicLevel, null, null, null)
        } else {
            RingRecord(
                ringId = event.ringId,
                peakG = event.peakG,
                dynamicLevel = dynamicLevel,
                totalMs = playedAtElapsedMs - estimatedDetectionAtElapsedMs,
                bleMs = receivedAtElapsedMs - estimatedDetectionAtElapsedMs,
                appMs = playedAtElapsedMs - receivedAtElapsedMs,
            )
        }
        lastRing = record
        tvLatency.text = "Latency: ${formatLatency(record, showLatencyDetails)}"

        ringHistory.addFirst(record)
        if (ringHistory.size > MAX_LOG_ENTRIES) ringHistory.removeLast()
        ringLogAdapter.insert(formatLogLine(record, showLatencyDetails), 0)
        if (ringLogAdapter.count > MAX_LOG_ENTRIES) {
            ringLogAdapter.remove(ringLogAdapter.getItem(ringLogAdapter.count - 1))
        }
    }

    private fun formatLatency(r: RingRecord, details: Boolean): String = when {
        r.totalMs == null -> "syncing clock…"
        details -> "${r.totalMs}ms  (ring→phone ${r.bleMs}ms + phone→sound ${r.appMs}ms)"
        else -> "${r.totalMs}ms"
    }

    private fun formatLogLine(r: RingRecord, details: Boolean): String =
        String.format(
            Locale.US, "#%-4d %5.2fg %-2s  %s",
            r.ringId, r.peakG, r.dynamicLevel.label, formatLatency(r, details),
        )

    private fun refreshLogDisplay() {
        ringLogAdapter.clear()
        ringLogAdapter.addAll(ringHistory.map { formatLogLine(it, showLatencyDetails) })
    }

    private fun onBattery(status: BatteryStatus) {
        tvBattery.text = when (status.state) {
            BatteryState.NO_BATTERY -> "⚡ USB, no battery"
            BatteryState.CHARGING -> "🔌 Charging ${status.percent}%"
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
