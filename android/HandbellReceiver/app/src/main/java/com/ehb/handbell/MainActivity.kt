package com.ehb.handbell

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private lateinit var tvStatus: TextView
    private lateinit var tvRingCount: TextView
    private lateinit var tvLastPeak: TextView
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
            onRing = { event -> runOnUiThread { onRing(event) } },
        ).also { it.start() }
    }

    private fun onRing(event: RingEvent) {
        ringCount++
        tvRingCount.text = ringCount.toString()
        tvLastPeak.text = String.format(Locale.US, "Last peak: %.2fg", event.peakG)
        flashBackground()

        // Play immediately — this is the latency-critical path, no extra work before it.
        ringPlayer.play(event.peakG)
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
